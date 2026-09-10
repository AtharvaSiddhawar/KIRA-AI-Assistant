#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <Preferences.h>

#include "kira_network_v2.h"
#include "kira_wifi_max.h"
#include "kira_ai_web.h"
#include "kira_next_config.h"
#include "elli_visual.h"

String normalizeInput(String s);
void elliSay(const String& s);

namespace {

String ssid1, pass1, ssid2, pass2;
bool started = false;
bool forcedOffline = false;
bool exhibitionMode = false;
Preferences netPrefs;
bool prefsReady = false;
String lastConnectedSsid;

bool wifiMaxArmed = false;

bool internetAvailable = false;
uint32_t currentLatencyMs = 0;
uint8_t consecutiveHealthFails = 0;
uint32_t lastReconnectAttempt = 0;
uint32_t lastHealthCheck = 0;

uint32_t linkLossStartedMs = 0;
uint32_t lastRadioRecoveryMs = 0;
uint8_t reconnectNudges = 0;

// SAFE NETWORK MODE
// -----------------
// The previous Phase 5 build downloaded a 64 KB test object and repeatedly
// compared both Wi-Fi networks. That path is intentionally disabled here.
// KIRA now uses only a tiny internet reachability/latency check.
// KIRA Wi-Fi Stability V3
// Reliability first: automatic reconnect + escalation + radio-only recovery.
const uint32_t AUTO_RECONNECT_GRACE_MS      = 4000UL;
const uint32_t RECONNECT_NUDGE_INTERVAL_MS  = 5000UL;
const uint32_t FALLBACK_ASSOC_AFTER_MS      = 15000UL;
const uint32_t RADIO_RECOVERY_AFTER_MS      = 35000UL;
const uint32_t RADIO_RECOVERY_COOLDOWN_MS   = 30000UL;
const uint32_t HEALTH_INTERVAL_MS           = 120000UL;
const uint8_t HEALTH_FAILS_BEFORE_OFFLINE   = 2;

const char* CHECK_URL = "http://connectivitycheck.gstatic.com/generate_204";

bool configured(const String& s) {
  String t = s;
  t.trim();
  return t.length() > 0;
}

bool sameConfiguredNetwork(const String& a, const String& b) {
  return configured(a) && configured(b) && a == b;
}

bool waitForConnect(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(120);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool quickInternetCheck(uint32_t& latencyMs) {
  latencyMs = 0;
  if (WiFi.status() != WL_CONNECTED) return false;

  // Very small HTTP 204 request. No bulk download/speed test.
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(1800);
  http.setTimeout(2200);

  uint32_t start = millis();
  if (http.begin(client, CHECK_URL)) {
    int code = http.GET();
    latencyMs = millis() - start;
    http.end();

    if (code == 204) return true;
  }

  // Tiny fallback: DNS + TCP connect only. This avoids a large transfer.
  IPAddress ip;
  uint32_t fallbackStart = millis();
  if (WiFi.hostByName("one.one.one.one", ip) == 1) {
    WiFiClient probe;
    probe.setTimeout(1000);
    if (probe.connect(ip, 80, 1000)) {
      probe.stop();
      latencyMs = millis() - fallbackStart;
      return true;
    }
  }

  return false;
}

bool connectRadio(const String& ssid, const String& pass, uint32_t timeoutMs, bool verbose) {
  if (!configured(ssid)) return false;

  if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid) {
    return true;
  }

  if (verbose) {
    Serial.print("[NETWORK V2 SAFE] Connecting to: ");
    Serial.println(ssid);
  }

  WiFi.disconnect(false, false);
  delay(80);
  WiFi.begin(ssid.c_str(), pass.c_str());

  if (!waitForConnect(timeoutMs)) {
    if (verbose) Serial.println("[NETWORK V2 SAFE] Wi-Fi association failed.");
    return false;
  }

  if (verbose) {
    Serial.print("[NETWORK V2 SAFE] Wi-Fi link OK | RSSI ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    Serial.print("[NETWORK V2 SAFE] IP: ");
    Serial.println(WiFi.localIP());
  }

  return true;
}

bool tryUsableInternet(const String& ssid, const String& pass, bool verbose) {
  if (!configured(ssid)) return false;
  if (!connectRadio(ssid, pass, 7000, verbose)) return false;

  uint32_t latency = 0;
  bool ok = quickInternetCheck(latency);

  if (verbose) {
    Serial.print("[NETWORK V2 SAFE] Internet on ");
    Serial.print(ssid);
    Serial.print(": ");
    Serial.println(ok ? "YES" : "NO");
    if (ok) {
      Serial.print("[NETWORK V2 SAFE] Lightweight latency: ");
      Serial.print(latency);
      Serial.println(" ms");
    }
  }

  if (!ok) return false;

  internetAvailable = true;
  currentLatencyMs = latency;
  consecutiveHealthFails = 0;
  lastConnectedSsid = WiFi.SSID();
  if (prefsReady) netPrefs.putString("last_ssid", lastConnectedSsid);
  return true;
}

bool connectFirstUsable(bool verbose) {
  if (forcedOffline) return false;
  if (!configured(ssid1) && !configured(ssid2)) return false;

  WiFi.mode(WIFI_STA);

  // Conservative association profile.
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(true);

  internetAvailable = false;
  currentLatencyMs = 0;
  consecutiveHealthFails = 0;

  if (verbose) {
    Serial.println("[NETWORK V2 SAFE] Bulk internet-speed test DISABLED.");
    Serial.println("[NETWORK V2 SAFE] Looking for a working configured internet connection...");
  }

  // Prefer the last connection that actually worked.
  if (lastConnectedSsid == ssid1 && configured(ssid1)) {
    if (tryUsableInternet(ssid1, pass1, verbose)) return true;
    if (configured(ssid2) && ssid2 != ssid1 && tryUsableInternet(ssid2, pass2, verbose)) return true;
  }
  else if (lastConnectedSsid == ssid2 && configured(ssid2)) {
    if (tryUsableInternet(ssid2, pass2, verbose)) return true;
    if (configured(ssid1) && ssid1 != ssid2 && tryUsableInternet(ssid1, pass1, verbose)) return true;
  }
  else {
    if (configured(ssid1) && tryUsableInternet(ssid1, pass1, verbose)) return true;
    if (configured(ssid2) && ssid2 != ssid1 && tryUsableInternet(ssid2, pass2, verbose)) return true;
  }

  // Neither configured network had usable internet. Keep a Wi-Fi link if possible
  // so local-network features still have a chance to work.
  if (configured(ssid1) && connectRadio(ssid1, pass1, 5000, false)) {
    lastConnectedSsid = WiFi.SSID();
  }
  else if (configured(ssid2) && ssid2 != ssid1 && connectRadio(ssid2, pass2, 5000, false)) {
    lastConnectedSsid = WiFi.SSID();
  }

  if (prefsReady && WiFi.status() == WL_CONNECTED) {
    netPrefs.putString("last_ssid", WiFi.SSID());
  }

  if (verbose) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[NETWORK V2 SAFE] Wi-Fi linked, but usable internet was not confirmed.");
    }
    else {
      Serial.println("[NETWORK V2 SAFE] No configured Wi-Fi could be joined.");
    }
  }

  return WiFi.status() == WL_CONNECTED;
}


// ============================================================
// WI-FI MAX POST-ASSOCIATION ARM
// ============================================================
//
// Critical rule:
//   NOTHING from Wi-Fi MAX is allowed to run before WL_CONNECTED.
//
// Association remains the exact known-working KIRA path:
//   WiFi.mode(WIFI_STA)
//   WiFi.setSleep(true)
//   WiFi.disconnect(false,false)
//   delay(80)
//   WiFi.begin(ssid,pass)
// ============================================================

void armWifiMaxAfterAssociation() {
  if(
    WiFi.status() !=
      WL_CONNECTED
  ) {
    return;
  }


  if(
    !wifiMaxArmed
  ) {
    if(
      kiraWifiMaxBegin()
    ) {
      wifiMaxArmed =
        true;

      Serial.println(
        "[NETWORK/WIFI MAX V2] Engine armed AFTER successful association."
      );
    }
  }


  if(
    wifiMaxArmed
  ) {
    kiraWifiMaxApplyRadioProfile();

    kiraWifiMaxObserveConnection(
      true,
      WiFi.RSSI()
    );
  }
}


void clearReconnectState() {
  linkLossStartedMs = 0;
  lastReconnectAttempt = 0;
  reconnectNudges = 0;
}


// ============================================================
// KIRA NETWORK STABILITY V4 - NON-DESTRUCTIVE RUNTIME RECOVERY
// ============================================================
// Runtime recovery never calls ESP.restart(), never switches WIFI_OFF, and
// never waits several seconds inside loop(). Association is requested and
// completed asynchronously by the Wi-Fi driver.
// ============================================================

void requestRuntimeAssociation(const String& ssid, const String& pass) {
  if (!configured(ssid)) return;

  Serial.print("[NETWORK STABILITY V4] async association request -> ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), pass.c_str());
}

void serviceReconnectRecovery() {
  const uint32_t now = millis();

  if (linkLossStartedMs == 0) {
    linkLossStartedMs = now;
    lastReconnectAttempt = now;
    reconnectNudges = 0;
    Serial.println(
      "[NETWORK STABILITY V4] link lost | background recovery armed | ESP reset forbidden"
    );
    return;
  }

  // Do not churn the radio while a voice turn is active.
  if (wifiMaxArmed && kiraWifiMaxVoiceCritical()) {
    return;
  }

  if (now - lastReconnectAttempt < RECONNECT_NUDGE_INTERVAL_MS) {
    return;
  }

  lastReconnectAttempt = now;
  reconnectNudges++;
  const uint32_t lostFor = now - linkLossStartedMs;

  if (lostFor < FALLBACK_ASSOC_AFTER_MS) {
    Serial.print("[NETWORK STABILITY V4] reconnect nudge #");
    Serial.println(reconnectNudges);
    WiFi.reconnect();
    return;
  }

  const bool have1 = configured(ssid1);
  const bool have2 = configured(ssid2) && ssid2 != ssid1;

  if (have1 && have2) {
    if ((reconnectNudges & 1U) == 0U) requestRuntimeAssociation(ssid1, pass1);
    else requestRuntimeAssociation(ssid2, pass2);
  }
  else if (have1) {
    requestRuntimeAssociation(ssid1, pass1);
  }
  else if (have2) {
    requestRuntimeAssociation(ssid2, pass2);
  }
}

void maintainInternetHealth() {
  if (forcedOffline || WiFi.status() != WL_CONNECTED) {
    internetAvailable = false;
    currentLatencyMs = 0;
    consecutiveHealthFails = 0;
    return;
  }

#if KIRA_NETWORK_ACTIVE_HEALTH_PROBE_ENABLED
  if (wifiMaxArmed && kiraWifiMaxVoiceCritical()) return;
  if (millis() - lastHealthCheck < HEALTH_INTERVAL_MS) return;
  lastHealthCheck = millis();

  uint32_t latency = 0;
  const bool ok = quickInternetCheck(latency);
  internetAvailable = ok;
  currentLatencyMs = ok ? latency : 0;
  consecutiveHealthFails = ok ? 0 : 1;

  if (wifiMaxArmed) kiraWifiMaxObserveInternet(ok, currentLatencyMs);
#else
  // No synthetic DNS/HTTP probes in the background. Actual provider calls
  // remain the real test of internet/provider availability.
  internetAvailable = true;
  currentLatencyMs = 0;
  consecutiveHealthFails = 0;
  if (wifiMaxArmed) kiraWifiMaxObserveInternet(true, 0);
#endif
}

} // namespace

void kiraNetworkBegin(
  const char* primarySsid,
  const char* primaryPassword,
  const char* secondarySsid,
  const char* secondaryPassword
) {
  ssid1 = primarySsid ? String(primarySsid) : String();
  pass1 = primaryPassword ? String(primaryPassword) : String();
  ssid2 = secondarySsid ? String(secondarySsid) : String();
  pass2 = secondaryPassword ? String(secondaryPassword) : String();

  prefsReady = netPrefs.begin("kira_net_v2", false);
  if (prefsReady) {
    forcedOffline = netPrefs.getBool("forced", false);
    exhibitionMode = netPrefs.getBool("exhibit", false);
    lastConnectedSsid = netPrefs.getString("last_ssid", "");
  }

  started = true;

  Serial.println("[NETWORK V2 SAFE] Low-power internet-aware dual-Wi-Fi manager ready.");
  Serial.println("[NETWORK V2 SAFE] 64 KB throughput/speed test is OFF.");
  Serial.print("[NETWORK V2 SAFE] Wi-Fi #2 configured: ");
  Serial.println(configured(ssid2) ? "YES" : "NO");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(true);

  if (forcedOffline) {
    WiFi.disconnect(false, false);
    internetAvailable = false;
    Serial.println("[NETWORK V2 SAFE] Forced offline mode is ON; startup connection skipped.");
  }
  else {
    connectFirstUsable(true);

    if(
      WiFi.status() ==
        WL_CONNECTED
    ) {
      clearReconnectState();
      armWifiMaxAfterAssociation();
    }

    lastHealthCheck = millis();
  }
}

void kiraNetworkMaintain() {
  if (!started) return;

  if(
    wifiMaxArmed
  ) {
    kiraWifiMaxService();
  }

  if (forcedOffline) {
    if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(false, false);
    internetAvailable = false;
    currentLatencyMs = 0;
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    internetAvailable = false;
    currentLatencyMs = 0;

    if(
      wifiMaxArmed
    ) {
      kiraWifiMaxObserveInternet(false, 0);
    }

    serviceReconnectRecovery();
    return;
  }

  if(
    linkLossStartedMs != 0
  ) {
    Serial.println(
      "[NETWORK STABILITY V4] Wi-Fi reconnected in background | KIRA stayed alive"
    );

    if (wifiMaxArmed) kiraWifiMaxNoteReconnect(true);

    clearReconnectState();
    lastHealthCheck = millis();
    internetAvailable = true;
    currentLatencyMs = 0;
  }

  if(
    !wifiMaxArmed
  ) {
    armWifiMaxAfterAssociation();
  }

  if (WiFi.SSID() != lastConnectedSsid) {
    lastConnectedSsid = WiFi.SSID();
    if (prefsReady) netPrefs.putString("last_ssid", lastConnectedSsid);
  }

  maintainInternetHealth();
}

KiraConnectivityMode kiraNetworkMode() {
  if (forcedOffline || WiFi.status() != WL_CONNECTED || !internetAvailable) {
    return KIRA_NET_OFFLINE;
  }
  return KiraV1::aiProvidersUsable() ? KIRA_NET_ONLINE_FULL : KIRA_NET_ONLINE_DEGRADED;
}

const char* kiraNetworkModeName() {
  switch (kiraNetworkMode()) {
    case KIRA_NET_ONLINE_FULL: return "ONLINE_FULL";
    case KIRA_NET_ONLINE_DEGRADED: return "ONLINE_DEGRADED";
    default: return "OFFLINE";
  }
}

bool kiraNetworkForcedOffline() { return forcedOffline; }
bool kiraNetworkExhibitionMode() { return exhibitionMode; }
bool kiraNetworkConnected() { return WiFi.status() == WL_CONNECTED && !forcedOffline; }
bool kiraNetworkInternetAvailable() { return kiraNetworkConnected() && internetAvailable; }
String kiraNetworkCurrentSsid() { return WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String(); }
int kiraNetworkCurrentRssi() { return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -1000; }

int kiraNetworkSmoothedRssi() {
  if(
    wifiMaxArmed
  ) {
    return
      kiraWifiMaxSmoothedRssi();
  }

  return
    kiraNetworkCurrentRssi();
}

const char* kiraNetworkAdaptiveModeName() {
  if(
    wifiMaxArmed
  ) {
    return
      kiraWifiMaxModeName();
  }

  return
    WiFi.status() ==
      WL_CONNECTED
        ? "SAFE_ASSOCIATION"
        : "NOT_ARMED";
}

int kiraNetworkHealthScore() {
  if(
    wifiMaxArmed
  ) {
    return
      kiraWifiMaxHealthScore();
  }

  if(
    WiFi.status() !=
      WL_CONNECTED
  ) {
    return 0;
  }

  // Before MAX is armed, report a conservative link-only score.
  int rssi =
    WiFi.RSSI();

  if(rssi >= -55) return 90;
  if(rssi >= -65) return 80;
  if(rssi >= -72) return 68;
  if(rssi >= -80) return 52;

  return 35;
}

// Real bulk throughput measurement is intentionally disabled in SAFE mode.
uint32_t kiraNetworkCurrentSpeedKbps() { return 0; }
uint32_t kiraNetworkCurrentLatencyMs() { return kiraNetworkInternetAvailable() ? currentLatencyMs : 0; }

// Compatibility function used by the existing Elli status strip.
// In SAFE mode these are INTERNET-RESPONSIVENESS bars, not throughput bars.
int kiraNetworkSpeedBars() {
  if (!kiraNetworkInternetAvailable()) return 0;
  if (currentLatencyMs == 0) return 1;
  if (currentLatencyMs <= 150UL) return 4;
  if (currentLatencyMs <= 300UL) return 3;
  if (currentLatencyMs <= 600UL) return 2;
  return 1;
}

bool kiraNetworkHasPrimaryConfigured() { return configured(ssid1); }
bool kiraNetworkDualConfigValid() {
  return configured(ssid1) && (!configured(ssid2) || !sameConfiguredNetwork(ssid1, ssid2));
}

void kiraNetworkSetForcedOffline(bool enabled) {
  forcedOffline = enabled;
  if (prefsReady) netPrefs.putBool("forced", enabled);

  if (enabled) {
    WiFi.disconnect(false, false);
    internetAvailable = false;
    currentLatencyMs = 0;
    Serial.println("[NETWORK V2 SAFE] Forced offline mode enabled.");
  }
  else {
    Serial.println("[NETWORK V2 SAFE] Forced offline mode disabled; reconnecting safely.");
    kiraNetworkReconnectNow();
  }
}

void kiraNetworkSetExhibitionMode(bool enabled) {
  exhibitionMode = enabled;
  if (prefsReady) netPrefs.putBool("exhibit", enabled);
  Serial.print("[NETWORK V2 SAFE] Exhibition mode: ");
  Serial.println(enabled ? "ON" : "OFF");
}

void kiraNetworkReconnectNow() {
  if (forcedOffline) return;

  clearReconnectState();
  internetAvailable = false;
  currentLatencyMs = 0;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(true);

  if (WiFi.status() == WL_CONNECTED) {
    internetAvailable = true;
    armWifiMaxAfterAssociation();
    return;
  }

  String targetSsid = lastConnectedSsid;
  String targetPass;

  if (targetSsid == ssid1 && configured(ssid1)) targetPass = pass1;
  else if (targetSsid == ssid2 && configured(ssid2)) targetPass = pass2;
  else if (configured(ssid1)) { targetSsid = ssid1; targetPass = pass1; }
  else if (configured(ssid2)) { targetSsid = ssid2; targetPass = pass2; }

  if (configured(targetSsid)) {
    Serial.println(
      "[NETWORK STABILITY V4] manual reconnect queued; KIRA remains running"
    );
    requestRuntimeAssociation(targetSsid, targetPass);
    linkLossStartedMs = millis();
    lastReconnectAttempt = millis();
  }
}

void kiraNetworkPrintStatus() {
  Serial.println();
  Serial.println("========== KIRA NETWORK V2 SAFE ==========");
  Serial.print("Mode              : "); Serial.println(kiraNetworkModeName());
  Serial.print("Forced offline    : "); Serial.println(forcedOffline ? "YES" : "NO");
  Serial.print("Exhibition mode   : "); Serial.println(exhibitionMode ? "YES" : "NO");
  Serial.print("Wi-Fi #1 config   : "); Serial.println(configured(ssid1) ? "YES" : "NO");
  Serial.print("Wi-Fi #2 config   : "); Serial.println(configured(ssid2) ? "YES" : "NO");
  Serial.print("Wi-Fi linked      : "); Serial.println(WiFi.status() == WL_CONNECTED ? "YES" : "NO");
  Serial.print("Internet          : "); Serial.println(kiraNetworkInternetAvailable() ? "YES" : "NO");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("SSID              : "); Serial.println(WiFi.SSID());
    Serial.print("RSSI              : "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
    Serial.print("IP                : "); Serial.println(WiFi.localIP());
  }

  Serial.println("Background probes : DISABLED (voice-stability mode)");
  if (kiraNetworkInternetAvailable()) {
    Serial.print("Last known latency: "); Serial.print(currentLatencyMs); Serial.println(" ms");
    Serial.print("Quality bars      : "); Serial.print(kiraNetworkSpeedBars()); Serial.println(" / 4");
  }

  Serial.print("Providers         : "); Serial.println(KiraV1::aiProviderStatusCompact());

  Serial.print("Wi-Fi MAX armed   : ");
  Serial.println(
    wifiMaxArmed
      ? "YES"
      : "NO"
  );

  if(
    wifiMaxArmed
  ) {
    kiraWifiMaxPrintDiagnostics();
  }

  Serial.println("==========================================");
}

bool kiraNetworkHandleCommand(String q) {
  q = normalizeInput(q);

  if (
    q == "network mode" ||
    q == "offline status" ||
    q == "network status" ||
    q == "wifi status" ||
    q == "show wifi signal" ||
    q == "show wifi networks"
  ) {
    kiraNetworkPrintStatus();

    if (kiraNetworkInternetAvailable()) {
      elliSay("Internet is online. Bulk speed testing is temporarily disabled for power stability.");
    }
    else if (WiFi.status() == WL_CONNECTED) {
      elliSay("I am connected to Wi-Fi, but usable internet is not confirmed.");
    }
    else {
      elliSay("I am not connected to a configured Wi-Fi network right now.");
    }
    return true;
  }

  if (q == "provider health" || q == "show provider health") {
    KiraV1::printAiProviderStatus();
    elliSay(KiraV1::aiProviderStatusCompact());
    return true;
  }

  if (q == "wifi rescan" || q == "wifi scan" || q == "retry internet" || q == "reconnect wifi") {
    if (forcedOffline) {
      elliSay("Forced offline mode is on. Turn it off before reconnecting.");
      return true;
    }

    kiraNetworkReconnectNow();

    if (kiraNetworkInternetAvailable()) {
      elliSay("I found a configured Wi-Fi connection with working internet.");
    }
    else if (WiFi.status() == WL_CONNECTED) {
      elliSay("I can join Wi-Fi, but usable internet was not confirmed.");
    }
    else {
      elliSay("Neither configured Wi-Fi network is reachable right now.");
    }
    return true;
  }

  if (q == "offline mode on" || q == "force offline on") {
    kiraNetworkSetForcedOffline(true);
    elliSay("Offline mode is on. Local KIRA features remain available.");
    return true;
  }

  if (q == "offline mode off" || q == "force offline off") {
    kiraNetworkSetForcedOffline(false);
    elliSay(
      kiraNetworkInternetAvailable()
      ? "Offline mode is off and I found working internet."
      : "Offline mode is off. I am still looking for usable internet."
    );
    return true;
  }

  if (q == "exhibition mode on" || q == "start exhibition mode") {
    kiraNetworkSetExhibitionMode(true);
    elliSay("Exhibition mode is on. I will prefer fast local knowledge and fall back online when needed.");
    return true;
  }

  if (q == "exhibition mode off" || q == "stop exhibition mode") {
    kiraNetworkSetExhibitionMode(false);
    elliSay("Exhibition mode is off.");
    return true;
  }

  if (q == "exhibition mode" || q == "exhibition status") {
    elliSay(String("Exhibition mode is ") + (exhibitionMode ? "on" : "off") + ". Network mode is " + kiraNetworkModeName() + ".");
    return true;
  }

  return false;
}
