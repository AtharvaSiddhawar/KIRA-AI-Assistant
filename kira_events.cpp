#include "kira_events.h"

#include "kira_next_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>


namespace {

QueueHandle_t eventQueue = nullptr;

volatile uint32_t postedCount = 0;
volatile uint32_t handledCount = 0;
volatile uint32_t droppedCount = 0;

portMUX_TYPE counterMux =
  portMUX_INITIALIZER_UNLOCKED;


void incrementCounter(
  volatile uint32_t& counter
) {
  portENTER_CRITICAL(
    &counterMux
  );

  counter++;

  portEXIT_CRITICAL(
    &counterMux
  );
}


uint32_t readCounter(
  volatile uint32_t& counter
) {
  uint32_t value = 0;

  portENTER_CRITICAL(
    &counterMux
  );

  value = counter;

  portEXIT_CRITICAL(
    &counterMux
  );

  return value;
}

} // namespace


bool kiraEventsBegin() {
#if !KIRA_EVENT_BUS_ENABLED
  return true;
#else

  if(
    eventQueue != nullptr
  ) {
    return true;
  }


  eventQueue =
    xQueueCreate(
      KIRA_EVENT_QUEUE_DEPTH,
      sizeof(KiraEvent)
    );


  if(
    eventQueue == nullptr
  ) {
    Serial.println(
      "[KIRA EVENTS] ERROR - queue allocation failed"
    );

    return false;
  }


  Serial.print(
    "[KIRA EVENTS] READY depth="
  );

  Serial.println(
    KIRA_EVENT_QUEUE_DEPTH
  );


  return true;
#endif
}


bool kiraEventsReady() {
#if !KIRA_EVENT_BUS_ENABLED
  return true;
#else
  return
    eventQueue != nullptr;
#endif
}


bool kiraEventPost(
  KiraEventType type,
  KiraEventSource source,
  int32_t value,
  uint32_t token,
  uint16_t flags
) {
#if !KIRA_EVENT_BUS_ENABLED
  (void)type;
  (void)source;
  (void)value;
  (void)token;
  (void)flags;
  return true;
#else

  if(
    eventQueue == nullptr &&
    !kiraEventsBegin()
  ) {
    incrementCounter(
      droppedCount
    );

    return false;
  }


  KiraEvent event = {};

  event.type = type;
  event.source = source;
  event.flags = flags;
  event.value = value;
  event.token = token;
  event.timestampMs = millis();


  BaseType_t result =
    xQueueSend(
      eventQueue,
      &event,
      0
    );


  if(
    result != pdTRUE
  ) {
    incrementCounter(
      droppedCount
    );

    Serial.print(
      "[KIRA EVENTS] QUEUE_OVERFLOW event="
    );

    Serial.println(
      kiraEventName(type)
    );

    return false;
  }


  incrementCounter(
    postedCount
  );

  return true;
#endif
}


bool kiraEventTake(
  KiraEvent& event,
  uint32_t waitMs
) {
#if !KIRA_EVENT_BUS_ENABLED
  (void)event;
  (void)waitMs;
  return false;
#else

  if(
    eventQueue == nullptr
  ) {
    return false;
  }


  TickType_t waitTicks =
    waitMs == 0
      ? 0
      : pdMS_TO_TICKS(waitMs);


  return
    xQueueReceive(
      eventQueue,
      &event,
      waitTicks
    ) == pdTRUE;
#endif
}


uint32_t kiraEventDroppedCount() {
  return readCounter(
    droppedCount
  );
}


uint32_t kiraEventPostedCount() {
  return readCounter(
    postedCount
  );
}


uint32_t kiraEventHandledCount() {
  return readCounter(
    handledCount
  );
}


void kiraEventMarkHandled() {
  incrementCounter(
    handledCount
  );
}


void kiraEventsFlush() {
#if KIRA_EVENT_BUS_ENABLED
  if(
    eventQueue != nullptr
  ) {
    xQueueReset(
      eventQueue
    );
  }
#endif
}


const char* kiraEventName(
  KiraEventType type
) {
  switch(type) {
    case KIRA_EVENT_NONE: return "NONE";
    case KIRA_EVENT_BOOT_STARTED: return "BOOT_STARTED";
    case KIRA_EVENT_BOOT_COMPLETE: return "BOOT_COMPLETE";
    case KIRA_EVENT_WIFI_CONNECTING: return "WIFI_CONNECTING";
    case KIRA_EVENT_NETWORK_LOST: return "NETWORK_LOST";
    case KIRA_EVENT_NETWORK_RECOVERED: return "NETWORK_RECOVERED";
    case KIRA_EVENT_WAKE_DETECTED: return "WAKE_DETECTED";
    case KIRA_EVENT_WAKE_REJECTED: return "WAKE_REJECTED";
    case KIRA_EVENT_LISTENING_STARTED: return "LISTENING_STARTED";
    case KIRA_EVENT_SPEECH_STARTED: return "SPEECH_STARTED";
    case KIRA_EVENT_SPEECH_FINISHED: return "SPEECH_FINISHED";
    case KIRA_EVENT_SPEECH_REJECTED: return "SPEECH_REJECTED";
    case KIRA_EVENT_ROUTING_STARTED: return "ROUTING_STARTED";
    case KIRA_EVENT_OFFLINE_COMMAND_MATCHED: return "OFFLINE_COMMAND_MATCHED";
    case KIRA_EVENT_STT_REQUIRED: return "STT_REQUIRED";
    case KIRA_EVENT_STT_READY: return "STT_READY";
    case KIRA_EVENT_STT_FAILED: return "STT_FAILED";
    case KIRA_EVENT_AI_STARTED: return "AI_STARTED";
    case KIRA_EVENT_AI_RESPONSE_READY: return "AI_RESPONSE_READY";
    case KIRA_EVENT_AI_FAILED: return "AI_FAILED";
    case KIRA_EVENT_ACTION_STARTED: return "ACTION_STARTED";
    case KIRA_EVENT_ACTION_FINISHED: return "ACTION_FINISHED";
    case KIRA_EVENT_TTS_STARTED: return "TTS_STARTED";
    case KIRA_EVENT_TTS_FINISHED: return "TTS_FINISHED";
    case KIRA_EVENT_TTS_FAILED: return "TTS_FAILED";
    case KIRA_EVENT_INTERRUPT_REQUESTED: return "INTERRUPT_REQUESTED";
    case KIRA_EVENT_SESSION_RECOVERING: return "SESSION_RECOVERING";
    case KIRA_EVENT_SESSION_RECOVERED: return "SESSION_RECOVERED";
    case KIRA_EVENT_OTA_STARTED: return "OTA_STARTED";
    case KIRA_EVENT_OTA_FINISHED: return "OTA_FINISHED";
    case KIRA_EVENT_ERROR_OCCURRED: return "ERROR_OCCURRED";
  }

  return "UNKNOWN";
}


const char* kiraEventSourceName(
  KiraEventSource source
) {
  switch(source) {
    case KIRA_EVENT_SOURCE_UNKNOWN: return "UNKNOWN";
    case KIRA_EVENT_SOURCE_SYSTEM: return "SYSTEM";
    case KIRA_EVENT_SOURCE_NETWORK: return "NETWORK";
    case KIRA_EVENT_SOURCE_AUDIO: return "AUDIO";
    case KIRA_EVENT_SOURCE_VAD: return "VAD";
    case KIRA_EVENT_SOURCE_WAKE: return "WAKE";
    case KIRA_EVENT_SOURCE_ROUTER: return "ROUTER";
    case KIRA_EVENT_SOURCE_STT: return "STT";
    case KIRA_EVENT_SOURCE_BRAIN: return "BRAIN";
    case KIRA_EVENT_SOURCE_TTS: return "TTS";
    case KIRA_EVENT_SOURCE_TOOL: return "TOOL";
    case KIRA_EVENT_SOURCE_SESSION: return "SESSION";
    case KIRA_EVENT_SOURCE_INTERRUPT: return "INTERRUPT";
  }

  return "UNKNOWN";
}
