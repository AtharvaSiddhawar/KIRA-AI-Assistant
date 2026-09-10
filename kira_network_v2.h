#pragma once

#include <Arduino.h>


enum KiraConnectivityMode : uint8_t {
  KIRA_NET_OFFLINE = 0,
  KIRA_NET_ONLINE_DEGRADED,
  KIRA_NET_ONLINE_FULL
};


void kiraNetworkBegin(
  const char* primarySsid,
  const char* primaryPassword,
  const char* secondarySsid,
  const char* secondaryPassword
);

void kiraNetworkMaintain();

bool kiraNetworkHandleCommand(
  String q
);


KiraConnectivityMode kiraNetworkMode();
const char* kiraNetworkModeName();

bool kiraNetworkConnected();
String kiraNetworkCurrentSsid();
int kiraNetworkCurrentRssi();
int kiraNetworkSmoothedRssi();

bool kiraNetworkInternetAvailable();

uint32_t kiraNetworkCurrentSpeedKbps();
uint32_t kiraNetworkCurrentLatencyMs();

int kiraNetworkSpeedBars();

const char* kiraNetworkAdaptiveModeName();
int kiraNetworkHealthScore();

bool kiraNetworkForcedOffline();
bool kiraNetworkExhibitionMode();

void kiraNetworkSetForcedOffline(
  bool enabled
);

void kiraNetworkSetExhibitionMode(
  bool enabled
);

void kiraNetworkReconnectNow();

void kiraNetworkPrintStatus();

bool kiraNetworkHasPrimaryConfigured();
bool kiraNetworkDualConfigValid();
