#include "kira_wifi_max.h"

#include <WiFi.h>
#include <esp_wifi.h>

#include "kira_runtime.h"
#include "kira_next_config.h"


namespace {


// ------------------------------------------------------------
// RADIO / LINK TUNING
// ------------------------------------------------------------

constexpr uint32_t RSSI_SAMPLE_MS =
  1000UL;

constexpr uint32_t DNS_WARM_TTL_MS =
  5UL * 60UL * 1000UL;


// These are the providers KIRA already uses.
// hostByName() also warms lwIP's DNS cache.
const char* PREWARM_HOSTS[] = {
  "api.groq.com",
  "generativelanguage.googleapis.com",
  "api.duckduckgo.com",
  "en.wikipedia.org",
  "openrouter.ai"
};

constexpr size_t PREWARM_HOST_COUNT =
  sizeof(PREWARM_HOSTS) /
  sizeof(PREWARM_HOSTS[0]);


struct DnsWarmEntry {
  IPAddress ip;
  uint32_t resolvedAtMs;
  bool valid;
};


struct ProviderHealthEntry {
  const char* name;
  uint32_t successes;
  uint32_t failures;
  uint32_t lastLatencyMs;
  int lastHttpCode;
  uint32_t lastUpdateMs;
};


ProviderHealthEntry providerHealth[] = {
  {"Groq STT", 0, 0, 0, 0, 0},
  {"Gemini AI", 0, 0, 0, 0, 0},
  {"Groq AI", 0, 0, 0, 0, 0},
  {"OpenRouter AI", 0, 0, 0, 0, 0},
  {"Groq TTS", 0, 0, 0, 0, 0},
  {"Public Web", 0, 0, 0, 0, 0}
};

constexpr size_t PROVIDER_COUNT =
  sizeof(providerHealth) /
  sizeof(providerHealth[0]);


DnsWarmEntry dnsWarm[
  PREWARM_HOST_COUNT
] = {};


bool ready =
  false;

bool connectedNow =
  false;

bool internetNow =
  false;


float smoothedRssi =
  -100.0f;

bool haveSmoothedRssi =
  false;


KiraWifiMaxMode mode =
  KIRA_WIFI_RECOVERY;


uint32_t lastRssiSampleMs =
  0;

uint32_t linkStartedMs =
  0;

uint32_t lastInternetOkMs =
  0;

uint32_t lastInternetLatencyMs =
  0;


uint32_t reconnectAttempts =
  0;

uint32_t reconnectSuccesses =
  0;

uint32_t scanCount =
  0;


uint32_t dnsWarmHits =
  0;

uint32_t dnsWarmMisses =
  0;

uint32_t dnsWarmFailures =
  0;


uint32_t requestCount =
  0;

uint32_t requestContentions =
  0;

uint32_t requestFailures =
  0;


SemaphoreHandle_t requestMutex =
  nullptr;

volatile int activePriority =
  -1;

volatile uint16_t activeRequests =
  0;


TaskHandle_t prewarmTaskHandle =
  nullptr;

volatile bool prewarmRequested =
  false;


KiraRuntimeState previousRuntimeState =
  KIRA_STATE_BOOT;


// ------------------------------------------------------------
// MODE HELPERS
// ------------------------------------------------------------

const char* modeName(
  KiraWifiMaxMode m
) {
  switch(
    m
  ) {
    case KIRA_WIFI_PERFORMANCE:
      return "PERFORMANCE";

    case KIRA_WIFI_NORMAL:
      return "NORMAL";

    case KIRA_WIFI_RANGE:
      return "RANGE";

    case KIRA_WIFI_WEAK:
      return "WEAK";

    case KIRA_WIFI_RECOVERY:
      return "RECOVERY";
  }

  return "UNKNOWN";
}


KiraWifiMaxMode initialModeForRssi(
  int rssi
) {
  if(
    !connectedNow
  ) {
    return
      KIRA_WIFI_RECOVERY;
  }

  if(
    rssi >= -55
  ) {
    return
      KIRA_WIFI_PERFORMANCE;
  }

  if(
    rssi >= -68
  ) {
    return
      KIRA_WIFI_NORMAL;
  }

  if(
    rssi >= -76
  ) {
    return
      KIRA_WIFI_RANGE;
  }

  if(
    rssi >= -84
  ) {
    return
      KIRA_WIFI_WEAK;
  }

  return
    KIRA_WIFI_RECOVERY;
}


// Hysteresis prevents mode flapping around a threshold.
KiraWifiMaxMode nextModeWithHysteresis(
  KiraWifiMaxMode current,
  int rssi
) {
  if(
    !connectedNow
  ) {
    return
      KIRA_WIFI_RECOVERY;
  }


  switch(
    current
  ) {
    case KIRA_WIFI_PERFORMANCE:
      if(rssi < -60) return KIRA_WIFI_NORMAL;
      return current;


    case KIRA_WIFI_NORMAL:
      if(rssi > -53) return KIRA_WIFI_PERFORMANCE;
      if(rssi < -71) return KIRA_WIFI_RANGE;
      return current;


    case KIRA_WIFI_RANGE:
      if(rssi > -64) return KIRA_WIFI_NORMAL;
      if(rssi < -79) return KIRA_WIFI_WEAK;
      return current;


    case KIRA_WIFI_WEAK:
      if(rssi > -72) return KIRA_WIFI_RANGE;
      if(rssi < -86) return KIRA_WIFI_RECOVERY;
      return current;


    case KIRA_WIFI_RECOVERY:
      if(rssi > -80) return KIRA_WIFI_RANGE;
      return current;
  }


  return
    initialModeForRssi(
      rssi
    );
}


void updateMode() {
  if(
    !connectedNow
  ) {
    if(
      mode !=
      KIRA_WIFI_RECOVERY
    ) {
      mode =
        KIRA_WIFI_RECOVERY;

      Serial.println(
        "[WIFI MAX] mode -> RECOVERY"
      );
    }

    return;
  }


  int rssi =
    kiraWifiMaxSmoothedRssi();


  KiraWifiMaxMode next =
    haveSmoothedRssi
      ? nextModeWithHysteresis(
          mode,
          rssi
        )
      : initialModeForRssi(
          rssi
        );


  if(
    next ==
    mode
  ) {
    return;
  }


  mode =
    next;


  Serial.print(
    "[WIFI MAX] mode -> "
  );

  Serial.print(
    modeName(
      mode
    )
  );

  Serial.print(
    " | smoothed RSSI="
  );

  Serial.print(
    rssi
  );

  Serial.println(
    " dBm"
  );
}


// ------------------------------------------------------------
// PROVIDER HEALTH
// ------------------------------------------------------------

ProviderHealthEntry* findProvider(
  const char* name
) {
  if(
    !name ||
    !name[0]
  ) {
    return
      nullptr;
  }


  for(
    size_t i=0;
    i<PROVIDER_COUNT;
    i++
  ) {
    if(
      strcmp(
        providerHealth[i].name,
        name
      ) == 0
    ) {
      return
        &providerHealth[i];
    }
  }


  return
    nullptr;
}


// ------------------------------------------------------------
// VOICE-CRITICAL STATE
// ------------------------------------------------------------

bool runtimeVoiceCritical() {
  KiraRuntimeState s =
    kiraRuntimeState();


  switch(
    s
  ) {
    case KIRA_STATE_WAKE_DETECTED:
    case KIRA_STATE_LISTENING:
    case KIRA_STATE_ROUTING:
    case KIRA_STATE_TRANSCRIBING:
    case KIRA_STATE_THINKING:
    case KIRA_STATE_SPEAKING:
      return true;

    default:
      return false;
  }
}


// ------------------------------------------------------------
// DNS PREWARM TASK
// ------------------------------------------------------------

// Prewarming is intentionally allowed while the user is WAKE/LISTENING.
// That is the entire latency optimization: DNS work happens while speech is
// still being captured. The moment STT/AI/TTS obtains a request lease,
// prewarming yields immediately.
bool prewarmAllowed() {
  if(
    WiFi.status() !=
    WL_CONNECTED
  ) {
    return false;
  }

  if(
    activeRequests > 0
  ) {
    return false;
  }

  if(
    mode ==
      KIRA_WIFI_WEAK ||
    mode ==
      KIRA_WIFI_RECOVERY
  ) {
    return false;
  }

  KiraRuntimeState s =
    kiraRuntimeState();

  return
    s ==
      KIRA_STATE_WAKE_DETECTED ||
    s ==
      KIRA_STATE_LISTENING ||
    s ==
      KIRA_STATE_IDLE;
}


void prewarmTask(
  void*
) {
  for(;;) {
    if(
      !prewarmRequested
    ) {
      vTaskDelay(
        pdMS_TO_TICKS(
          100
        )
      );

      continue;
    }


    prewarmRequested =
      false;


    if(
      !prewarmAllowed()
    ) {
      continue;
    }


    Serial.println(
      "[WIFI MAX] DNS prewarm started"
    );


    uint32_t now =
      millis();


    for(
      size_t i=0;
      i<PREWARM_HOST_COUNT;
      i++
    ) {
      if(
        !prewarmAllowed()
      ) {
        break;
      }


      if(
        dnsWarm[i].valid &&
        now -
          dnsWarm[i].resolvedAtMs <
          DNS_WARM_TTL_MS
      ) {
        dnsWarmHits++;
        continue;
      }


      dnsWarmMisses++;


      IPAddress ip;

      int ok =
        WiFi.hostByName(
          PREWARM_HOSTS[i],
          ip
        );


      if(
        ok == 1
      ) {
        dnsWarm[i].ip =
          ip;

        dnsWarm[i].resolvedAtMs =
          millis();

        dnsWarm[i].valid =
          true;
      }
      else {
        dnsWarmFailures++;
      }


      vTaskDelay(
        pdMS_TO_TICKS(
          10
        )
      );
    }


    Serial.println(
      "[WIFI MAX] DNS prewarm finished"
    );
  }
}


// ------------------------------------------------------------
// RADIO STATUS
// ------------------------------------------------------------

int readTxPowerQuarterDbm() {
  int8_t power =
    0;

  if(
    esp_wifi_get_max_tx_power(
      &power
    ) != ESP_OK
  ) {
    return
      -1;
  }

  return
    power;
}


uint8_t readChannel() {
  if(
    WiFi.status() !=
    WL_CONNECTED
  ) {
    return
      0;
  }

  return
    (uint8_t)WiFi.channel();
}


} // namespace



bool kiraWifiMaxBegin() {
  // HARD ASSOCIATION FIREWALL:
  // Wi-Fi MAX must never initialize before the standard KIRA
  // Wi-Fi association has already succeeded.
  if(
    WiFi.status() !=
      WL_CONNECTED
  ) {
    return false;
  }


  if(
    ready
  ) {
    return true;
  }


  requestMutex =
    xSemaphoreCreateMutex();


  // Do NOT force the MAX radio profile before Wi-Fi association.
  // The network module enables it immediately after WL_CONNECTED.
#if KIRA_WIFI_DNS_PREWARM_ENABLED
  if(
    prewarmTaskHandle ==
    nullptr
  ) {
    BaseType_t ok =
      xTaskCreatePinnedToCore(
        prewarmTask,
        "KIRA-WIFI-WARM",
        4096,
        nullptr,
        1,
        &prewarmTaskHandle,
        0
      );

    if(
      ok !=
      pdPASS
    ) {
      prewarmTaskHandle =
        nullptr;

      Serial.println(
        "[WIFI MAX] DNS prewarm task unavailable"
      );
    }
  }
#else
  prewarmTaskHandle = nullptr;
  prewarmRequested = false;
#endif


  connectedNow =
    WiFi.status() ==
    WL_CONNECTED;


  if(
    connectedNow
  ) {
    int rssi =
      WiFi.RSSI();

    smoothedRssi =
      (float)rssi;

    haveSmoothedRssi =
      true;

    linkStartedMs =
      millis();

    mode =
      initialModeForRssi(
        rssi
      );
  }
  else {
    mode =
      KIRA_WIFI_RECOVERY;
  }


  previousRuntimeState =
    kiraRuntimeState();


  ready =
    true;


  Serial.println();
  Serial.println(
    "[WIFI MAX] UNIFIED ENGINE READY"
  );

  Serial.println(
    "[WIFI STABILITY V3] post-association engine | reliability-first profile"
  );

#if KIRA_WIFI_DNS_PREWARM_ENABLED
  Serial.println(
    "[WIFI STABILITY V3] RSSI + DNS prewarm + request QoS + health diagnostics retained"
  );
#else
  Serial.println(
    "[WIFI STABILITY V4] DNS prewarm OFF | RSSI + request QoS retained"
  );
#endif


  return true;
}



void kiraWifiMaxApplyRadioProfile() {
  // KIRA Wi-Fi Stability V3:
  // keep the ESP32 driver's normal radio policy and preserve its reconnect
  // safety net. RSSI/QoS/DNS/provider features remain active elsewhere.

  if(
    WiFi.status() !=
      WL_CONNECTED
  ) {
    return;
  }

  WiFi.setAutoReconnect(
    true
  );

  // Conservative power profile: this is the same direction used by the
  // known-good association path and reduces unnecessary radio power spikes.
  WiFi.setSleep(
    true
  );

  Serial.println(
    "[WIFI STABILITY V3] auto-reconnect=ON | conservative radio defaults"
  );
}



void kiraWifiMaxService() {
  if(
    !ready
  ) {
    return;
  }


  bool link =
    WiFi.status() ==
    WL_CONNECTED;


  int rawRssi =
    link
      ? WiFi.RSSI()
      : -100;


  kiraWifiMaxObserveConnection(
    link,
    rawRssi
  );


  uint32_t now =
    millis();


  if(
    link &&
    (
      lastRssiSampleMs == 0 ||
      now -
        lastRssiSampleMs >=
        RSSI_SAMPLE_MS
    )
  ) {
    lastRssiSampleMs =
      now;


    if(
      !haveSmoothedRssi
    ) {
      smoothedRssi =
        (float)rawRssi;

      haveSmoothedRssi =
        true;
    }
    else {
      // EWMA: 25% new sample / 75% history.
      smoothedRssi =
        (
          smoothedRssi *
          0.75f
        ) +
        (
          (float)rawRssi *
          0.25f
        );
    }


    updateMode();
  }


  KiraRuntimeState runtime =
    kiraRuntimeState();


  bool enteringVoiceTurn =
    (
      runtime ==
        KIRA_STATE_WAKE_DETECTED ||
      runtime ==
        KIRA_STATE_LISTENING
    ) &&
    runtime !=
      previousRuntimeState;


  if(
    enteringVoiceTurn
  ) {
    kiraWifiMaxTriggerPrewarm();
  }


  previousRuntimeState =
    runtime;
}



void kiraWifiMaxObserveConnection(
  bool connected,
  int rssi
) {
  bool changed =
    connected !=
    connectedNow;


  connectedNow =
    connected;


  if(
    changed &&
    connected
  ) {
    linkStartedMs =
      millis();

    smoothedRssi =
      (float)rssi;

    haveSmoothedRssi =
      true;

    mode =
      initialModeForRssi(
        rssi
      );

    kiraWifiMaxApplyRadioProfile();
  }


  if(
    changed &&
    !connected
  ) {
    linkStartedMs =
      0;

    haveSmoothedRssi =
      false;

    smoothedRssi =
      -100.0f;

    mode =
      KIRA_WIFI_RECOVERY;
  }
}



void kiraWifiMaxObserveInternet(
  bool available,
  uint32_t latencyMs
) {
  internetNow =
    available;


  if(
    available
  ) {
    lastInternetOkMs =
      millis();

    lastInternetLatencyMs =
      latencyMs;
  }
}



void kiraWifiMaxNoteReconnect(
  bool success
) {
  reconnectAttempts++;


  if(
    success
  ) {
    reconnectSuccesses++;
  }
}



void kiraWifiMaxNoteScan() {
  scanCount++;
}



void kiraWifiMaxTriggerPrewarm() {
#if !KIRA_WIFI_DNS_PREWARM_ENABLED
  // Stability V4: no background DNS work during or around voice turns.
  prewarmRequested = false;
  return;
#else
  if(
    prewarmTaskHandle == nullptr ||
    WiFi.status() != WL_CONNECTED
  ) {
    return;
  }

  prewarmRequested = true;
#endif
}



bool kiraWifiMaxVoiceCritical() {
  if(
    runtimeVoiceCritical()
  ) {
    return true;
  }


  return
    activeRequests > 0 &&
    activePriority >=
      KIRA_NET_PRIORITY_TTS;
}



bool kiraWifiMaxBackgroundAllowed() {
  if(
    WiFi.status() !=
    WL_CONNECTED
  ) {
    return false;
  }


  if(
    kiraWifiMaxVoiceCritical()
  ) {
    return false;
  }


  return
    mode !=
      KIRA_WIFI_WEAK &&
    mode !=
      KIRA_WIFI_RECOVERY;
}



KiraWifiMaxMode kiraWifiMaxMode() {
  return
    mode;
}



const char* kiraWifiMaxModeName() {
  return
    modeName(
      mode
    );
}



int kiraWifiMaxSmoothedRssi() {
  if(
    !haveSmoothedRssi
  ) {
    return
      WiFi.status() ==
        WL_CONNECTED
          ? WiFi.RSSI()
          : -100;
  }


  return
    (int)(
      smoothedRssi >= 0
        ? smoothedRssi + 0.5f
        : smoothedRssi - 0.5f
    );
}



uint32_t kiraWifiMaxAdaptiveTimeout(
  uint32_t baseMs
) {
  // Strong links do not get artificially shortened timeouts because
  // provider/server latency is independent of RSSI.
  //
  // Weak links get more patience instead of unnecessary reconnects.
  switch(
    mode
  ) {
    case KIRA_WIFI_RANGE:
      return
        baseMs +
        baseMs / 3UL;


    case KIRA_WIFI_WEAK:
      return
        baseMs +
        baseMs / 2UL;


    case KIRA_WIFI_RECOVERY:
      return
        baseMs * 2UL;


    default:
      return
        baseMs;
  }
}



uint32_t kiraWifiMaxAssociationTimeout() {
  switch(
    mode
  ) {
    case KIRA_WIFI_PERFORMANCE:
      return 5000UL;

    case KIRA_WIFI_NORMAL:
      return 6500UL;

    case KIRA_WIFI_RANGE:
      return 8500UL;

    case KIRA_WIFI_WEAK:
      return 11000UL;

    case KIRA_WIFI_RECOVERY:
      return 12000UL;
  }

  return 7000UL;
}



uint32_t kiraWifiMaxReconnectInterval() {
  switch(
    mode
  ) {
    case KIRA_WIFI_PERFORMANCE:
      return 7000UL;

    case KIRA_WIFI_NORMAL:
      return 6000UL;

    case KIRA_WIFI_RANGE:
      return 5000UL;

    case KIRA_WIFI_WEAK:
      return 4000UL;

    case KIRA_WIFI_RECOVERY:
      return 3000UL;
  }

  return 6000UL;
}



uint32_t kiraWifiMaxHealthInterval() {
  switch(
    mode
  ) {
    case KIRA_WIFI_PERFORMANCE:
    case KIRA_WIFI_NORMAL:
      return 90000UL;

    case KIRA_WIFI_RANGE:
      return 150000UL;

    case KIRA_WIFI_WEAK:
      return 240000UL;

    case KIRA_WIFI_RECOVERY:
      return 300000UL;
  }

  return 120000UL;
}



int kiraWifiMaxHealthScore() {
  if(
    !connectedNow
  ) {
    return 0;
  }


  int score =
    100;


  int rssi =
    kiraWifiMaxSmoothedRssi();


  if(rssi < -55) score -= 5;
  if(rssi < -65) score -= 10;
  if(rssi < -72) score -= 15;
  if(rssi < -80) score -= 20;
  if(rssi < -86) score -= 20;


  if(
    !internetNow
  ) {
    score -= 25;
  }


  if(
    lastInternetLatencyMs > 250
  ) {
    score -= 5;
  }

  if(
    lastInternetLatencyMs > 600
  ) {
    score -= 10;
  }

  if(
    lastInternetLatencyMs > 1200
  ) {
    score -= 10;
  }


  uint32_t failedReconnects =
    reconnectAttempts >=
      reconnectSuccesses
        ? reconnectAttempts -
          reconnectSuccesses
        : 0;


  if(
    failedReconnects > 0
  ) {
    score -=
      min(
        20,
        (int)failedReconnects * 3
      );
  }


  if(
    requestFailures > 0
  ) {
    score -=
      min(
        15,
        (int)requestFailures
      );
  }


  if(
    score < 0
  ) {
    score = 0;
  }

  if(
    score > 100
  ) {
    score = 100;
  }


  return score;
}



KiraWifiMaxSnapshot kiraWifiMaxSnapshot() {
  KiraWifiMaxSnapshot s={};


  s.connected =
    connectedNow;

  s.internetAvailable =
    internetNow;


  s.rawRssi =
    connectedNow
      ? WiFi.RSSI()
      : -100;

  s.smoothedRssi =
    kiraWifiMaxSmoothedRssi();


  s.mode =
    mode;


  s.channel =
    readChannel();

  s.txPowerQuarterDbm =
    readTxPowerQuarterDbm();


  s.linkAgeMs =
    (
      connectedNow &&
      linkStartedMs
    )
      ? millis() -
        linkStartedMs
      : 0;


  s.lastInternetOkAgeMs =
    lastInternetOkMs
      ? millis() -
        lastInternetOkMs
      : 0;


  s.reconnectAttempts =
    reconnectAttempts;

  s.reconnectSuccesses =
    reconnectSuccesses;

  s.scanCount =
    scanCount;


  s.dnsWarmHits =
    dnsWarmHits;

  s.dnsWarmMisses =
    dnsWarmMisses;

  s.dnsWarmFailures =
    dnsWarmFailures;


  s.requestCount =
    requestCount;

  s.requestContentions =
    requestContentions;

  s.requestFailures =
    requestFailures;


  s.lastInternetLatencyMs =
    lastInternetLatencyMs;


  s.healthScore =
    kiraWifiMaxHealthScore();


  return s;
}



bool kiraWifiMaxRequestBegin(
  KiraNetPriority priority,
  const char* provider,
  uint32_t waitMs
) {
  requestCount++;


  if(
    requestMutex ==
    nullptr
  ) {
    return false;
  }


  // Critical voice traffic gets a bounded wait.
  // Background/web traffic never waits long enough to delay a turn.
  if(
    waitMs == 0
  ) {
    switch(
      priority
    ) {
      case KIRA_NET_PRIORITY_STT:
        waitMs=1500;
        break;

      case KIRA_NET_PRIORITY_AI:
        waitMs=1200;
        break;

      case KIRA_NET_PRIORITY_TTS:
        waitMs=1000;
        break;

      case KIRA_NET_PRIORITY_WEB:
        waitMs=250;
        break;

      case KIRA_NET_PRIORITY_BACKGROUND:
        waitMs=0;
        break;

      case KIRA_NET_PRIORITY_RECOVERY:
        waitMs=1500;
        break;
    }
  }


  TickType_t waitTicks =
    waitMs
      ? pdMS_TO_TICKS(
          waitMs
        )
      : 0;


  bool locked =
    xSemaphoreTake(
      requestMutex,
      waitTicks
    ) == pdTRUE;


  if(
    !locked
  ) {
    requestContentions++;

    if(
      priority <=
        KIRA_NET_PRIORITY_WEB
    ) {
      return false;
    }

    // Critical requests are still allowed to continue even if the
    // advisory lease could not be obtained. We never block KIRA forever.
    return false;
  }


  activeRequests++;

  if(
    (int)priority >
    activePriority
  ) {
    activePriority =
      (int)priority;
  }


  (void)provider;

  return true;
}



void kiraWifiMaxRequestEnd(
  KiraNetPriority priority,
  const char* provider,
  int httpCode,
  bool success,
  uint32_t elapsedMs
) {
  ProviderHealthEntry* p =
    findProvider(
      provider
    );


  if(
    p
  ) {
    p->lastHttpCode =
      httpCode;

    p->lastLatencyMs =
      elapsedMs;

    p->lastUpdateMs =
      millis();


    if(
      success
    ) {
      p->successes++;
    }
    else {
      p->failures++;
    }
  }


  if(
    !success
  ) {
    requestFailures++;
  }


  if(
    requestMutex
  ) {
    if(
      activeRequests > 0
    ) {
      activeRequests--;
    }


    if(
      activeRequests == 0
    ) {
      activePriority =
        -1;
    }


    // Give the mutex only when this scope actually obtained it.
    // KiraWifiRequestScope tracks that and calls this function only
    // for a held lease.
    xSemaphoreGive(
      requestMutex
    );
  }


  (void)priority;
}



const char* kiraWifiMaxProviderHealth(
  const char* provider
) {
  ProviderHealthEntry* p =
    findProvider(
      provider
    );


  if(
    !p
  ) {
    return
      "UNKNOWN";
  }


  if(
    p->lastHttpCode ==
    429
  ) {
    return
      "RATE_LIMITED";
  }


  if(
    p->lastUpdateMs == 0
  ) {
    return
      "UNTESTED";
  }


  if(
    p->failures == 0
  ) {
    return
      "HEALTHY";
  }


  if(
    p->successes == 0 &&
    p->failures >= 2
  ) {
    return
      "DEGRADED";
  }


  if(
    p->failures >
    p->successes
  ) {
    return
      "SLOW/DEGRADED";
  }


  return
    "HEALTHY";
}



void kiraWifiMaxPrintDiagnostics() {
  KiraWifiMaxSnapshot s =
    kiraWifiMaxSnapshot();


  Serial.println();
  Serial.println(
    "---------------- KIRA WIFI MAX ----------------"
  );

  Serial.print("Adaptive mode         : ");
  Serial.println(kiraWifiMaxModeName());

  Serial.print("RSSI raw              : ");
  Serial.print(s.rawRssi);
  Serial.println(" dBm");

  Serial.print("RSSI smoothed         : ");
  Serial.print(s.smoothedRssi);
  Serial.println(" dBm");

  Serial.print("Channel               : ");
  Serial.println(s.channel);

  Serial.print("TX power max          : ");
  if(
    s.txPowerQuarterDbm >= 0
  ) {
    Serial.print(
      s.txPowerQuarterDbm /
      4.0f,
      2
    );
    Serial.println(" dBm");
  }
  else {
    Serial.println("unknown");
  }

  Serial.println("Wi-Fi sleep           : OFF");
  Serial.println("Bandwidth             : HT20");

  Serial.print("Link age              : ");
  Serial.print(s.linkAgeMs);
  Serial.println(" ms");

  Serial.print("Internet latency      : ");
  Serial.print(s.lastInternetLatencyMs);
  Serial.println(" ms");

  Serial.print("Reconnect attempts    : ");
  Serial.println(s.reconnectAttempts);

  Serial.print("Reconnect successes   : ");
  Serial.println(s.reconnectSuccesses);

  Serial.print("Controlled scans      : ");
  Serial.println(s.scanCount);

  Serial.print("DNS warm hits         : ");
  Serial.println(s.dnsWarmHits);

  Serial.print("DNS warm misses       : ");
  Serial.println(s.dnsWarmMisses);

  Serial.print("DNS warm failures     : ");
  Serial.println(s.dnsWarmFailures);

  Serial.print("Network requests      : ");
  Serial.println(s.requestCount);

  Serial.print("Request contentions   : ");
  Serial.println(s.requestContentions);

  Serial.print("Request failures      : ");
  Serial.println(s.requestFailures);

  Serial.print("Network health        : ");
  Serial.print(s.healthScore);
  Serial.println(" / 100");


  Serial.println("Provider memory:");

  for(
    size_t i=0;
    i<PROVIDER_COUNT;
    i++
  ) {
    Serial.print("  ");
    Serial.print(providerHealth[i].name);
    Serial.print(" : ");
    Serial.print(
      kiraWifiMaxProviderHealth(
        providerHealth[i].name
      )
    );

    Serial.print(
      " | last="
    );

    Serial.print(
      providerHealth[i].lastLatencyMs
    );

    Serial.print(
      " ms | HTTP="
    );

    Serial.println(
      providerHealth[i].lastHttpCode
    );
  }

  Serial.println(
    "-------------------------------------------------"
  );
}



// ============================================================
// KiraWifiRequestScope
// ============================================================

KiraWifiRequestScope::KiraWifiRequestScope(
  KiraNetPriority priority,
  const char* provider,
  uint32_t waitMs
) :
  priority_(
    priority
  ),
  provider_(
    provider
  ),
  startedMs_(
    millis()
  ),
  httpCode_(
    -1
  ),
  success_(
    false
  ),
  resultExplicit_(
    false
  ),
  leaseHeld_(
    false
  )
{
  leaseHeld_ =
    kiraWifiMaxRequestBegin(
      priority_,
      provider_,
      waitMs
    );
}


KiraWifiRequestScope::~KiraWifiRequestScope() {
  // If the request never supplied an explicit result, infer success only
  // from a known 2xx HTTP code.
  bool finalSuccess =
    resultExplicit_
      ? success_
      : (
          httpCode_ >= 200 &&
          httpCode_ < 300
        );


  if(
    leaseHeld_
  ) {
    kiraWifiMaxRequestEnd(
      priority_,
      provider_,
      httpCode_,
      finalSuccess,
      millis() -
        startedMs_
    );
  }
  else {
    // No mutex lease was held, but still preserve provider statistics.
    ProviderHealthEntry* p =
      findProvider(
        provider_
      );

    if(
      p
    ) {
      p->lastHttpCode =
        httpCode_;

      p->lastLatencyMs =
        millis() -
        startedMs_;

      p->lastUpdateMs =
        millis();

      if(
        finalSuccess
      ) {
        p->successes++;
      }
      else {
        p->failures++;
      }
    }

    if(
      !finalSuccess
    ) {
      requestFailures++;
    }
  }
}


void KiraWifiRequestScope::setHttpCode(
  int code
) {
  httpCode_ =
    code;
}


void KiraWifiRequestScope::setSuccess(
  bool success
) {
  success_ =
    success;

  resultExplicit_ =
    true;
}
