#pragma once

#include <Arduino.h>


// ============================================================
// KIRA NEXT - CENTRAL EVENT BUS
// ============================================================
//
// Events are deliberately small POD values. Audio/text payloads remain
// owned by their subsystem and are referenced through value/token fields.
// This keeps the FreeRTOS queue deterministic and avoids copying String
// objects between tasks.
// ============================================================


enum KiraEventType : uint8_t {
  KIRA_EVENT_NONE = 0,

  KIRA_EVENT_BOOT_STARTED,
  KIRA_EVENT_BOOT_COMPLETE,

  KIRA_EVENT_WIFI_CONNECTING,
  KIRA_EVENT_NETWORK_LOST,
  KIRA_EVENT_NETWORK_RECOVERED,

  KIRA_EVENT_WAKE_DETECTED,
  KIRA_EVENT_WAKE_REJECTED,

  KIRA_EVENT_LISTENING_STARTED,
  KIRA_EVENT_SPEECH_STARTED,
  KIRA_EVENT_SPEECH_FINISHED,
  KIRA_EVENT_SPEECH_REJECTED,

  KIRA_EVENT_ROUTING_STARTED,
  KIRA_EVENT_OFFLINE_COMMAND_MATCHED,
  KIRA_EVENT_STT_REQUIRED,
  KIRA_EVENT_STT_READY,
  KIRA_EVENT_STT_FAILED,

  KIRA_EVENT_AI_STARTED,
  KIRA_EVENT_AI_RESPONSE_READY,
  KIRA_EVENT_AI_FAILED,

  KIRA_EVENT_ACTION_STARTED,
  KIRA_EVENT_ACTION_FINISHED,

  KIRA_EVENT_TTS_STARTED,
  KIRA_EVENT_TTS_FINISHED,
  KIRA_EVENT_TTS_FAILED,

  KIRA_EVENT_INTERRUPT_REQUESTED,

  KIRA_EVENT_SESSION_RECOVERING,
  KIRA_EVENT_SESSION_RECOVERED,

  KIRA_EVENT_OTA_STARTED,
  KIRA_EVENT_OTA_FINISHED,

  KIRA_EVENT_ERROR_OCCURRED
};


enum KiraEventSource : uint8_t {
  KIRA_EVENT_SOURCE_UNKNOWN = 0,
  KIRA_EVENT_SOURCE_SYSTEM,
  KIRA_EVENT_SOURCE_NETWORK,
  KIRA_EVENT_SOURCE_AUDIO,
  KIRA_EVENT_SOURCE_VAD,
  KIRA_EVENT_SOURCE_WAKE,
  KIRA_EVENT_SOURCE_ROUTER,
  KIRA_EVENT_SOURCE_STT,
  KIRA_EVENT_SOURCE_BRAIN,
  KIRA_EVENT_SOURCE_TTS,
  KIRA_EVENT_SOURCE_TOOL,
  KIRA_EVENT_SOURCE_SESSION,
  KIRA_EVENT_SOURCE_INTERRUPT
};


struct KiraEvent {
  KiraEventType type;
  KiraEventSource source;
  uint16_t flags;
  int32_t value;
  uint32_t token;
  uint32_t timestampMs;
};


bool kiraEventsBegin();
bool kiraEventsReady();

bool kiraEventPost(
  KiraEventType type,
  KiraEventSource source = KIRA_EVENT_SOURCE_UNKNOWN,
  int32_t value = 0,
  uint32_t token = 0,
  uint16_t flags = 0
);

bool kiraEventTake(
  KiraEvent& event,
  uint32_t waitMs = 0
);

uint32_t kiraEventDroppedCount();
uint32_t kiraEventPostedCount();
uint32_t kiraEventHandledCount();

void kiraEventMarkHandled();
void kiraEventsFlush();

const char* kiraEventName(
  KiraEventType type
);

const char* kiraEventSourceName(
  KiraEventSource source
);
