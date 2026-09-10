#include "kira_protocol.h"

#include "kira_next_config.h"

#include <string.h>

namespace {

String jsonEscape(const String& in) {
  String out;
  out.reserve(in.length() + 12);

  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];

    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:   out += c; break;
    }
  }

  return out;
}

int findValueStart(const String& json, const char* key) {
  if (!key || !key[0]) return -1;

  String needle = "\"";
  needle += key;
  needle += "\"";

  int pos = json.indexOf(needle);
  if (pos < 0) return -1;

  pos = json.indexOf(':', pos + needle.length());
  if (pos < 0) return -1;

  ++pos;
  while (pos < (int)json.length()) {
    char c = json[pos];
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
    ++pos;
  }

  return pos < (int)json.length() ? pos : -1;
}

bool extractString(const String& json, const char* key, String& out) {
  out = "";
  int pos = findValueStart(json, key);
  if (pos < 0 || json[pos] != '"') return false;

  ++pos;
  bool escaped = false;

  for (; pos < (int)json.length(); ++pos) {
    char c = json[pos];

    if (escaped) {
      switch (c) {
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case '\\': out += '\\'; break;
        case '"': out += '"'; break;
        default: out += c; break;
      }
      escaped = false;
      continue;
    }

    if (c == '\\') {
      escaped = true;
      continue;
    }

    if (c == '"') return true;
    out += c;
  }

  return false;
}

bool extractUInt(const String& json, const char* key, uint32_t& out) {
  int pos = findValueStart(json, key);
  if (pos < 0) return false;

  uint32_t value = 0;
  bool found = false;

  while (pos < (int)json.length()) {
    const char c = json[pos];
    if (c < '0' || c > '9') break;
    found = true;
    value = value * 10UL + (uint32_t)(c - '0');
    ++pos;
  }

  if (!found) return false;
  out = value;
  return true;
}

KiraProtocolType mapType(const String& type) {
  if (type == "hello") return KIRA_PROTOCOL_HELLO;
  if (type == "listen") return KIRA_PROTOCOL_LISTEN;
  if (type == "abort") return KIRA_PROTOCOL_ABORT;
  if (type == "stt") return KIRA_PROTOCOL_STT;
  if (type == "llm") return KIRA_PROTOCOL_LLM;
  if (type == "tts") return KIRA_PROTOCOL_TTS;
  if (type == "error") return KIRA_PROTOCOL_ERROR;
  if (type == "mcp") return KIRA_PROTOCOL_MCP;
  return KIRA_PROTOCOL_UNKNOWN;
}

bool protocolSelfTest() {
  KiraProtocolMessage m;

  const char* hello =
    "{\"type\":\"hello\",\"session_id\":\"kira-test\","
    "\"audio_params\":{\"format\":\"opus\",\"sample_rate\":16000,"
    "\"channels\":1,\"frame_duration\":60}}";

  if (!kiraProtocolParse((const uint8_t*)hello, strlen(hello), m)) return false;
  if (m.type != KIRA_PROTOCOL_HELLO) return false;
  if (m.sessionId != "kira-test") return false;
  if (m.audioFormat != "opus") return false;
  if (m.sampleRate != 16000) return false;
  if (m.channels != 1) return false;
  if (m.frameDurationMs != 60) return false;

  const char* stt =
    "{\"type\":\"stt\",\"session_id\":\"kira-test\","
    "\"text\":\"hello elli\"}";

  if (!kiraProtocolParse((const uint8_t*)stt, strlen(stt), m)) return false;
  if (m.type != KIRA_PROTOCOL_STT) return false;
  if (m.text != "hello elli") return false;

  return true;
}

} // namespace

bool kiraProtocolBegin() {
#if !KIRA_PROTOCOL_CORE_ENABLED
  return true;
#else
  Serial.println("[PROTOCOL] READY | hello/listen/abort/stt/llm/tts parser");

#if KIRA_PROTOCOL_SELFTEST_ENABLED
  const bool ok = protocolSelfTest();
  Serial.print("[PROTOCOL] SELFTEST ");
  Serial.println(ok ? "PASS" : "FAIL");
  return ok;
#else
  return true;
#endif
#endif
}

bool kiraProtocolParse(
  const uint8_t* data,
  size_t bytes,
  KiraProtocolMessage& out
) {
#if !KIRA_PROTOCOL_CORE_ENABLED
  (void)data;
  (void)bytes;
  (void)out;
  return false;
#else
  out = KiraProtocolMessage();

  if (!data || bytes == 0 || bytes > 8192) return false;

  String json;
  json.reserve(bytes + 1);
  for (size_t i = 0; i < bytes; ++i) json += (char)data[i];

  if (!extractString(json, "type", out.typeText)) return false;

  out.type = mapType(out.typeText);
  extractString(json, "session_id", out.sessionId);
  extractString(json, "state", out.state);
  extractString(json, "mode", out.mode);
  extractString(json, "text", out.text);
  extractString(json, "reason", out.reason);
  extractString(json, "format", out.audioFormat);

  uint32_t n = 0;
  if (extractUInt(json, "sample_rate", n)) out.sampleRate = n;
  if (extractUInt(json, "channels", n)) out.channels = (uint8_t)n;
  if (extractUInt(json, "frame_duration", n)) out.frameDurationMs = (uint16_t)n;

  return true;
#endif
}

String kiraProtocolBuildHello(
  const String& sessionId,
  const char* audioFormat,
  uint32_t sampleRate,
  uint8_t channels,
  uint16_t frameDurationMs
) {
  String s = "{\"type\":\"hello\",\"version\":1";

  if (sessionId.length()) {
    s += ",\"session_id\":\"";
    s += jsonEscape(sessionId);
    s += "\"";
  }

  s += ",\"features\":{\"mcp\":false}";
  s += ",\"transport\":\"websocket\"";
  s += ",\"audio_params\":{\"format\":\"";
  s += jsonEscape(audioFormat ? String(audioFormat) : String("opus"));
  s += "\",\"sample_rate\":";
  s += String(sampleRate);
  s += ",\"channels\":";
  s += String(channels);
  s += ",\"frame_duration\":";
  s += String(frameDurationMs);
  s += "}}";

  return s;
}

String kiraProtocolBuildListen(
  const String& sessionId,
  const char* state,
  const char* mode
) {
  String s = "{";

  if (sessionId.length()) {
    s += "\"session_id\":\"";
    s += jsonEscape(sessionId);
    s += "\",";
  }

  s += "\"type\":\"listen\",\"state\":\"";
  s += jsonEscape(state ? String(state) : String("start"));
  s += "\",\"mode\":\"";
  s += jsonEscape(mode ? String(mode) : String("auto"));
  s += "\"}";

  return s;
}

String kiraProtocolBuildAbort(
  const String& sessionId,
  const char* reason
) {
  String s = "{";

  if (sessionId.length()) {
    s += "\"session_id\":\"";
    s += jsonEscape(sessionId);
    s += "\",";
  }

  s += "\"type\":\"abort\",\"reason\":\"";
  s += jsonEscape(reason ? String(reason) : String("user_interrupt"));
  s += "\"}";

  return s;
}

const char* kiraProtocolTypeName(KiraProtocolType type) {
  switch (type) {
    case KIRA_PROTOCOL_HELLO: return "HELLO";
    case KIRA_PROTOCOL_LISTEN: return "LISTEN";
    case KIRA_PROTOCOL_ABORT: return "ABORT";
    case KIRA_PROTOCOL_STT: return "STT";
    case KIRA_PROTOCOL_LLM: return "LLM";
    case KIRA_PROTOCOL_TTS: return "TTS";
    case KIRA_PROTOCOL_ERROR: return "ERROR";
    case KIRA_PROTOCOL_MCP: return "MCP";
    case KIRA_PROTOCOL_UNKNOWN: default: return "UNKNOWN";
  }
}
