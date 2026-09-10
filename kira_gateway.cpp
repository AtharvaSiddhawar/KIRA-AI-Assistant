#include "kira_gateway.h"

#include "kira_audio_codec.h"
#include "kira_audio_stream.h"
#include "kira_events.h"
#include "kira_gateway_config.h"
#include "kira_next_config.h"
#include "kira_protocol.h"
#include "kira_transport.h"

namespace {

bool socketSeen = false;
bool helloAccepted = false;

String sessionId;
String lastTranscript;

uint32_t protocolRxCount = 0;
uint32_t protocolErrorCount = 0;
uint32_t audioRxBytes = 0;

String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 16);

  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (c == '\\') out += "\\\\";
    else if (c == '"') out += "\\\"";
    else if (c == '\n') out += "\\n";
    else if (c != '\r') out += c;
  }

  return out;
}

void handleProtocolMessage(const KiraProtocolMessage& m) {
  protocolRxCount++;

  if (m.sessionId.length()) sessionId = m.sessionId;

  switch (m.type) {
    case KIRA_PROTOCOL_HELLO:
      helloAccepted = true;
      Serial.print("[GATEWAY] HELLO accepted | session=");
      Serial.println(sessionId.length() ? sessionId : String("<none>"));
      break;

    case KIRA_PROTOCOL_STT:
      lastTranscript = m.text;
      Serial.print("[GATEWAY] STT: ");
      Serial.println(lastTranscript);
      kiraEventPost(KIRA_EVENT_STT_READY, KIRA_EVENT_SOURCE_STT);
      break;

    case KIRA_PROTOCOL_LLM:
      if (m.text.length()) {
        Serial.print("[GATEWAY] LLM text bytes=");
        Serial.println(m.text.length());
      }
      break;

    case KIRA_PROTOCOL_TTS:
      if (m.state == "start") {
        kiraEventPost(KIRA_EVENT_TTS_STARTED, KIRA_EVENT_SOURCE_TTS);
      } else if (m.state == "stop") {
        kiraEventPost(KIRA_EVENT_TTS_FINISHED, KIRA_EVENT_SOURCE_TTS);
      }
      break;

    case KIRA_PROTOCOL_ERROR:
      protocolErrorCount++;
      Serial.print("[GATEWAY] REMOTE ERROR: ");
      Serial.println(m.text.length() ? m.text : m.reason);
      break;

    case KIRA_PROTOCOL_LISTEN:
    case KIRA_PROTOCOL_ABORT:
    case KIRA_PROTOCOL_MCP:
    case KIRA_PROTOCOL_UNKNOWN:
    default:
      break;
  }
}

void onTransportEvent(
  KiraTransportEvent event,
  const uint8_t* payload,
  size_t bytes
) {
  switch (event) {
    case KIRA_TRANSPORT_EVENT_CONNECTED: {
      socketSeen = true;
      helloAccepted = false;
      sessionId = "";

      String hello = kiraProtocolBuildHello(
        "",
        "opus",
        16000,
        1,
        KIRA_NETWORK_AUDIO_FRAME_MS
      );

      if (!kiraTransportSendText(hello)) {
        protocolErrorCount++;
        Serial.println("[GATEWAY] HELLO send failed");
      } else {
        Serial.println("[GATEWAY] HELLO sent");
      }
      break;
    }

    case KIRA_TRANSPORT_EVENT_DISCONNECTED:
      socketSeen = false;
      helloAccepted = false;
      sessionId = "";
      break;

    case KIRA_TRANSPORT_EVENT_TEXT: {
      KiraProtocolMessage message;
      if (!kiraProtocolParse(payload, bytes, message)) {
        protocolErrorCount++;
        Serial.print("[GATEWAY] invalid control frame bytes=");
        Serial.println(bytes);
        return;
      }

      handleProtocolMessage(message);
      break;
    }

    case KIRA_TRANSPORT_EVENT_BINARY:
      // Group B receives/counts binary frames but does not play them yet.
      // Group C will decode incoming Opus into the queued TTS output path.
      audioRxBytes += (uint32_t)bytes;
      break;

    case KIRA_TRANSPORT_EVENT_ERROR:
      protocolErrorCount++;
      break;
  }
}

} // namespace

bool kiraGatewayBegin() {
  kiraProtocolBegin();
  kiraAudioStreamBegin();
  kiraTransportBegin();
  kiraTransportSetEventHandler(onTransportEvent);

#if KIRA_GATEWAY_ENABLED
  String host = KIRA_GATEWAY_HOST;

  if (!host.length()) {
    Serial.println("[GATEWAY] enabled but host is empty; DIRECT MODE retained");
    return true;
  }

  // The XiaoZhi-compatible WebSocket path uses binary Opus audio. Never
  // activate the remote path with a fake PCM claim.
  kiraAudioCodecBegin();
  if (!kiraAudioCodecAvailable(KIRA_AUDIO_CODEC_OPUS)) {
    Serial.println("[GATEWAY] Opus backend unavailable; DIRECT MODE retained");
    return true;
  }

  kiraTransportConfigureWebSocket(
    host.c_str(),
    KIRA_GATEWAY_PORT,
    KIRA_GATEWAY_PATH,
    KIRA_GATEWAY_USE_TLS != 0
  );

  if (kiraTransportMode() != KIRA_TRANSPORT_WEBSOCKET) {
    Serial.println("[GATEWAY] WebSocket unavailable; DIRECT MODE retained");
    return true;
  }

  kiraTransportConnect();
  Serial.println("[GATEWAY] remote voice protocol enabled; waiting for HELLO");
#else
  Serial.println("[GATEWAY] GROUP B CORE READY / REMOTE ACTIVATION OFF");
#endif

  return true;
}

void kiraGatewayService() {
  kiraTransportService();

#if KIRA_AUDIO_UPLINK_ENABLED
  // Consumer side only: encoding + network writes stay out of ESP-SR's
  // microphone callback/task.
  kiraAudioStreamService();
#endif
}

bool kiraGatewayEnabled() {
#if KIRA_GATEWAY_ENABLED
  return kiraTransportMode() == KIRA_TRANSPORT_WEBSOCKET;
#else
  return false;
#endif
}

bool kiraGatewayReady() {
  return
    kiraGatewayEnabled() &&
    socketSeen &&
    helloAccepted &&
    kiraTransportConnected();
}

bool kiraGatewaySendControl(
  const String& eventName,
  const String& payloadJson
) {
  if (!kiraGatewayReady()) return false;

  String message =
    "{\"type\":\"event\",\"event\":\"" +
    jsonEscape(eventName) +
    "\",\"payload\":" +
    (payloadJson.length() ? payloadJson : String("{}"));

  if (sessionId.length()) {
    message += ",\"session_id\":\"";
    message += jsonEscape(sessionId);
    message += "\"";
  }

  message += "}";
  return kiraTransportSendText(message);
}

bool kiraGatewaySendAudio(const uint8_t* data, size_t bytes) {
  if (!kiraGatewayReady()) return false;
  return kiraTransportSendBinary(data, bytes);
}

bool kiraGatewayStartListening(const char* mode) {
  if (!kiraGatewayReady()) return false;
  return kiraTransportSendText(
    kiraProtocolBuildListen(sessionId, "start", mode)
  );
}

bool kiraGatewayStopListening() {
  if (!kiraGatewayReady()) return false;
  return kiraTransportSendText(
    kiraProtocolBuildListen(sessionId, "stop", "auto")
  );
}

bool kiraGatewayAbort(const char* reason) {
  if (!kiraGatewayReady()) return false;
  return kiraTransportSendText(
    kiraProtocolBuildAbort(sessionId, reason)
  );
}

const String& kiraGatewaySessionId() { return sessionId; }
const String& kiraGatewayLastTranscript() { return lastTranscript; }

uint32_t kiraGatewayProtocolRxCount() { return protocolRxCount; }
uint32_t kiraGatewayProtocolErrorCount() { return protocolErrorCount; }
uint32_t kiraGatewayAudioRxBytes() { return audioRxBytes; }

const char* kiraGatewayModeName() {
#if KIRA_GATEWAY_ENABLED
  if (kiraTransportMode() != KIRA_TRANSPORT_WEBSOCKET) return "DIRECT_FALLBACK";
  if (kiraGatewayReady()) return "GATEWAY_READY";
  if (socketSeen) return "GATEWAY_HANDSHAKE";
  return "GATEWAY_CONNECTING";
#else
  return "DIRECT_PROVIDER_MODE";
#endif
}
