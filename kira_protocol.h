#pragma once

#include <Arduino.h>

// ============================================================
// KIRA NEXT GROUP B - TRANSPORT-NEUTRAL VOICE PROTOCOL
// ============================================================
// Small parser/builder for the subset KIRA needs first:
// hello, listen, abort, stt, llm, tts, error and mcp envelope detection.
// No ArduinoJson dependency is required.
// ============================================================

enum KiraProtocolType : uint8_t {
  KIRA_PROTOCOL_UNKNOWN = 0,
  KIRA_PROTOCOL_HELLO,
  KIRA_PROTOCOL_LISTEN,
  KIRA_PROTOCOL_ABORT,
  KIRA_PROTOCOL_STT,
  KIRA_PROTOCOL_LLM,
  KIRA_PROTOCOL_TTS,
  KIRA_PROTOCOL_ERROR,
  KIRA_PROTOCOL_MCP
};

struct KiraProtocolMessage {
  KiraProtocolType type = KIRA_PROTOCOL_UNKNOWN;
  String typeText;
  String sessionId;
  String state;
  String mode;
  String text;
  String reason;

  String audioFormat;
  uint32_t sampleRate = 0;
  uint8_t channels = 0;
  uint16_t frameDurationMs = 0;
};

bool kiraProtocolBegin();

bool kiraProtocolParse(
  const uint8_t* data,
  size_t bytes,
  KiraProtocolMessage& out
);

String kiraProtocolBuildHello(
  const String& sessionId,
  const char* audioFormat,
  uint32_t sampleRate,
  uint8_t channels,
  uint16_t frameDurationMs
);

String kiraProtocolBuildListen(
  const String& sessionId,
  const char* state,
  const char* mode = "auto"
);

String kiraProtocolBuildAbort(
  const String& sessionId,
  const char* reason = "user_interrupt"
);

const char* kiraProtocolTypeName(
  KiraProtocolType type
);
