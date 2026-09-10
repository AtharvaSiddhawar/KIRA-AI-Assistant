#pragma once

#include <Arduino.h>


// ============================================================
// KIRA Wi-Fi MAX — unified software network engine
// ============================================================
//
// Goals:
//   - maximum practical ESP32-S3 software-only Wi-Fi robustness
//   - minimum avoidable voice/network latency
//   - no unnecessary Wi-Fi disconnect/reconnect cycles
//   - no background network work during critical voice traffic
//   - detailed diagnostics without bulk speed tests
//
// NOTE:
// Espressif Long Range (LR) PHY is intentionally NOT enabled because
// ordinary Wi-Fi routers do not use the proprietary LR PHY.
// ============================================================


enum KiraWifiMaxMode : uint8_t {
  KIRA_WIFI_PERFORMANCE = 0,
  KIRA_WIFI_NORMAL,
  KIRA_WIFI_RANGE,
  KIRA_WIFI_WEAK,
  KIRA_WIFI_RECOVERY
};


enum KiraNetPriority : uint8_t {
  KIRA_NET_PRIORITY_BACKGROUND = 0,
  KIRA_NET_PRIORITY_WEB,
  KIRA_NET_PRIORITY_TTS,
  KIRA_NET_PRIORITY_AI,
  KIRA_NET_PRIORITY_STT,
  KIRA_NET_PRIORITY_RECOVERY
};


struct KiraWifiMaxSnapshot {
  bool connected;
  bool internetAvailable;

  int rawRssi;
  int smoothedRssi;

  KiraWifiMaxMode mode;

  uint8_t channel;
  int txPowerQuarterDbm;

  uint32_t linkAgeMs;
  uint32_t lastInternetOkAgeMs;

  uint32_t reconnectAttempts;
  uint32_t reconnectSuccesses;
  uint32_t scanCount;

  uint32_t dnsWarmHits;
  uint32_t dnsWarmMisses;
  uint32_t dnsWarmFailures;

  uint32_t requestCount;
  uint32_t requestContentions;
  uint32_t requestFailures;

  uint32_t lastInternetLatencyMs;

  int healthScore;
};


bool kiraWifiMaxBegin();
void kiraWifiMaxService();

void kiraWifiMaxApplyRadioProfile();

void kiraWifiMaxObserveConnection(
  bool connected,
  int rssi
);

void kiraWifiMaxObserveInternet(
  bool available,
  uint32_t latencyMs
);

void kiraWifiMaxNoteReconnect(
  bool success
);

void kiraWifiMaxNoteScan();

void kiraWifiMaxTriggerPrewarm();

bool kiraWifiMaxVoiceCritical();
bool kiraWifiMaxBackgroundAllowed();

KiraWifiMaxMode kiraWifiMaxMode();
const char* kiraWifiMaxModeName();

int kiraWifiMaxSmoothedRssi();

uint32_t kiraWifiMaxAdaptiveTimeout(
  uint32_t baseMs
);

uint32_t kiraWifiMaxAssociationTimeout();
uint32_t kiraWifiMaxReconnectInterval();
uint32_t kiraWifiMaxHealthInterval();

int kiraWifiMaxHealthScore();

KiraWifiMaxSnapshot kiraWifiMaxSnapshot();

void kiraWifiMaxPrintDiagnostics();


// ============================================================
// REQUEST QOS / PROVIDER HEALTH
// ============================================================
//
// Critical STT/AI/TTS traffic obtains a lightweight network lease.
// Background requests are suppressed while voice traffic is active.
//
// This does not change provider payloads or API keys.
// ============================================================

bool kiraWifiMaxRequestBegin(
  KiraNetPriority priority,
  const char* provider,
  uint32_t waitMs = 0
);

void kiraWifiMaxRequestEnd(
  KiraNetPriority priority,
  const char* provider,
  int httpCode,
  bool success,
  uint32_t elapsedMs
);

const char* kiraWifiMaxProviderHealth(
  const char* provider
);


class KiraWifiRequestScope {
public:
  KiraWifiRequestScope(
    KiraNetPriority priority,
    const char* provider,
    uint32_t waitMs = 0
  );

  ~KiraWifiRequestScope();

  void setHttpCode(
    int code
  );

  void setSuccess(
    bool success
  );

private:
  KiraNetPriority priority_;
  const char* provider_;
  uint32_t startedMs_;
  int httpCode_;
  bool success_;
  bool resultExplicit_;
  bool leaseHeld_;
};
