#pragma once

#include <Arduino.h>

enum KiraTransportMode : uint8_t {
  KIRA_TRANSPORT_DIRECT = 0,
  KIRA_TRANSPORT_WEBSOCKET
};

enum KiraTransportEvent : uint8_t {
  KIRA_TRANSPORT_EVENT_CONNECTED = 0,
  KIRA_TRANSPORT_EVENT_DISCONNECTED,
  KIRA_TRANSPORT_EVENT_TEXT,
  KIRA_TRANSPORT_EVENT_BINARY,
  KIRA_TRANSPORT_EVENT_ERROR
};

typedef void (*KiraTransportEventHandler)(
  KiraTransportEvent event,
  const uint8_t* payload,
  size_t bytes
);

bool kiraTransportBegin();
void kiraTransportService();

void kiraTransportSetEventHandler(
  KiraTransportEventHandler handler
);

void kiraTransportConfigureWebSocket(
  const char* host,
  uint16_t port,
  const char* path,
  bool useTls
);

bool kiraTransportConnect();
void kiraTransportDisconnect();

bool kiraTransportConnected();

KiraTransportMode kiraTransportMode();
const char* kiraTransportModeName();

bool kiraTransportSendText(
  const String& text
);

bool kiraTransportSendBinary(
  const uint8_t* data,
  size_t bytes
);

uint32_t kiraTransportReconnectCount();
uint32_t kiraTransportTextRxCount();
uint32_t kiraTransportBinaryRxCount();
uint32_t kiraTransportBinaryRxBytes();
