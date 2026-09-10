#include "kira_transport.h"

#include "kira_network_v2.h"
#include "kira_next_config.h"

#if KIRA_WEBSOCKET_ENABLED
  #if __has_include(<WebSocketsClient.h>)
    #include <WebSocketsClient.h>
    #define KIRA_WS_LIBRARY_AVAILABLE 1
  #else
    #define KIRA_WS_LIBRARY_AVAILABLE 0
  #endif
#else
  #define KIRA_WS_LIBRARY_AVAILABLE 0
#endif

namespace {

KiraTransportMode mode = KIRA_TRANSPORT_DIRECT;
KiraTransportEventHandler eventHandler = nullptr;

String wsHost;
String wsPath = "/kira";
uint16_t wsPort = 443;
bool wsTls = true;
bool wsConnected = false;
bool wsConfigured = false;

uint32_t reconnectCount = 0;
uint32_t textRxCount = 0;
uint32_t binaryRxCount = 0;
uint32_t binaryRxBytes = 0;

void emit(
  KiraTransportEvent event,
  const uint8_t* payload = nullptr,
  size_t bytes = 0
) {
  if (eventHandler) eventHandler(event, payload, bytes);
}

#if KIRA_WS_LIBRARY_AVAILABLE
WebSocketsClient ws;

void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      wsConnected = true;
      Serial.println("[WS] CONNECTED");
      emit(KIRA_TRANSPORT_EVENT_CONNECTED, payload, length);
      break;

    case WStype_DISCONNECTED:
      if (wsConnected) reconnectCount++;
      wsConnected = false;
      Serial.println("[WS] CONNECTION_LOST");
      emit(KIRA_TRANSPORT_EVENT_DISCONNECTED);
      break;

    case WStype_TEXT:
      textRxCount++;
      emit(KIRA_TRANSPORT_EVENT_TEXT, payload, length);
      break;

    case WStype_BIN:
      binaryRxCount++;
      binaryRxBytes += (uint32_t)length;
      emit(KIRA_TRANSPORT_EVENT_BINARY, payload, length);
      break;

    case WStype_ERROR:
      emit(KIRA_TRANSPORT_EVENT_ERROR, payload, length);
      break;

    default:
      break;
  }
}
#endif

} // namespace

bool kiraTransportBegin() {
  // DIRECT remains the safe boot mode. Merely compiling WebSocket support
  // must never steal connectivity ownership from the existing KIRA brain.
  mode = KIRA_TRANSPORT_DIRECT;
  wsConnected = false;
  wsConfigured = false;

#if KIRA_WEBSOCKET_ENABLED
  Serial.print("[TRANSPORT] WebSocket support compiled | library=");
  Serial.println(KIRA_WS_LIBRARY_AVAILABLE ? "AVAILABLE" : "MISSING");
#else
  Serial.println("[TRANSPORT] DIRECT MODE | WebSocket activation OFF");
#endif

  return true;
}

void kiraTransportSetEventHandler(KiraTransportEventHandler handler) {
  eventHandler = handler;
}

void kiraTransportConfigureWebSocket(
  const char* host,
  uint16_t port,
  const char* path,
  bool useTls
) {
  wsHost = host ? host : "";
  wsPort = port;
  wsPath = (path && path[0]) ? String(path) : String("/kira");
  wsTls = useTls;

#if KIRA_WEBSOCKET_ENABLED && KIRA_WS_LIBRARY_AVAILABLE
  wsConfigured = wsHost.length() > 0;
  if (wsConfigured) mode = KIRA_TRANSPORT_WEBSOCKET;
#else
  wsConfigured = false;
  mode = KIRA_TRANSPORT_DIRECT;
#endif
}

bool kiraTransportConnect() {
  if (mode == KIRA_TRANSPORT_DIRECT) {
    return kiraNetworkConnected();
  }

#if KIRA_WS_LIBRARY_AVAILABLE
  if (!kiraNetworkConnected() || !wsConfigured || !wsHost.length()) return false;

  ws.onEvent(onWsEvent);
  ws.setReconnectInterval(5000);

  if (wsTls) {
    ws.beginSSL(wsHost.c_str(), wsPort, wsPath.c_str());
  } else {
    ws.begin(wsHost.c_str(), wsPort, wsPath.c_str());
  }

  Serial.print("[WS] CONNECTING host=");
  Serial.print(wsHost);
  Serial.print(" path=");
  Serial.println(wsPath);
  return true;
#else
  Serial.println("[WS] unavailable: WebSocketsClient library not installed");
  mode = KIRA_TRANSPORT_DIRECT;
  return false;
#endif
}

void kiraTransportDisconnect() {
#if KIRA_WS_LIBRARY_AVAILABLE
  if (mode == KIRA_TRANSPORT_WEBSOCKET) ws.disconnect();
#endif
  wsConnected = false;
}

void kiraTransportService() {
#if KIRA_WS_LIBRARY_AVAILABLE
  if (mode == KIRA_TRANSPORT_WEBSOCKET) ws.loop();
#endif
}

bool kiraTransportConnected() {
  if (mode == KIRA_TRANSPORT_DIRECT) return kiraNetworkConnected();
  return wsConnected;
}

KiraTransportMode kiraTransportMode() {
  return mode;
}

const char* kiraTransportModeName() {
  return mode == KIRA_TRANSPORT_WEBSOCKET ? "WEBSOCKET" : "DIRECT";
}

bool kiraTransportSendText(const String& text) {
  if (mode == KIRA_TRANSPORT_DIRECT) return false;

#if KIRA_WS_LIBRARY_AVAILABLE
  if (!wsConnected) return false;
  return ws.sendTXT(text);
#else
  (void)text;
  return false;
#endif
}

bool kiraTransportSendBinary(const uint8_t* data, size_t bytes) {
  if (mode == KIRA_TRANSPORT_DIRECT || !data || bytes == 0) return false;

#if KIRA_WS_LIBRARY_AVAILABLE
  if (!wsConnected) return false;
  return ws.sendBIN(const_cast<uint8_t*>(data), bytes);
#else
  (void)data;
  (void)bytes;
  return false;
#endif
}

uint32_t kiraTransportReconnectCount() { return reconnectCount; }
uint32_t kiraTransportTextRxCount() { return textRxCount; }
uint32_t kiraTransportBinaryRxCount() { return binaryRxCount; }
uint32_t kiraTransportBinaryRxBytes() { return binaryRxBytes; }
