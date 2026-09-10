#include "kira_runtime.h"

#include "kira_metrics.h"
#include "kira_next_config.h"
#include "kira_input_router.h"


namespace {

KiraRuntimeState currentState =
  KIRA_STATE_BOOT;

uint32_t stateEnteredMs = 0;
uint32_t illegalTransitionCount = 0;

portMUX_TYPE runtimeMux =
  portMUX_INITIALIZER_UNLOCKED;


void setRuntimeStateRaw(
  KiraRuntimeState state
) {
  portENTER_CRITICAL(
    &runtimeMux
  );

  currentState = state;
  stateEnteredMs = millis();

  portEXIT_CRITICAL(
    &runtimeMux
  );
}


bool staleTurnEventWhileIdle(
  const KiraEvent& event
){
  if(
    kiraRuntimeState()!=
    KIRA_STATE_IDLE
  ){
    return false;
  }

  switch(event.type){
    // These events make sense only while a turn is already active.
    // If the voice layer has completed the command and returned runtime to
    // IDLE before the Arduino loop drains the queue, they belong to the turn
    // that just finished and must NOT start a phantom second pipeline.
    case KIRA_EVENT_SPEECH_STARTED:
    case KIRA_EVENT_SPEECH_FINISHED:
    case KIRA_EVENT_ROUTING_STARTED:
    case KIRA_EVENT_OFFLINE_COMMAND_MATCHED:
    case KIRA_EVENT_STT_REQUIRED:
    case KIRA_EVENT_STT_READY:
    case KIRA_EVENT_AI_STARTED:
    case KIRA_EVENT_AI_RESPONSE_READY:
    case KIRA_EVENT_ACTION_STARTED:
    case KIRA_EVENT_ACTION_FINISHED:
    case KIRA_EVENT_TTS_STARTED:
    case KIRA_EVENT_TTS_FINISHED:
    case KIRA_EVENT_TTS_FAILED:
      return true;

    default:
      return false;
  }
}


bool handleEvent(
  const KiraEvent& event
) {
  if(
    staleTurnEventWhileIdle(
      event
    )
  ){
    kiraMetricsIncrement(
      KIRA_METRIC_STALE_EVENTS_IGNORED
    );

    Serial.print(
      "[KIRA RUNTIME] stale completed-turn event ignored: "
    );

    Serial.println(
      kiraEventName(
        event.type
      )
    );

    return true;
  }

#if KIRA_ROUTER_V2_ENABLED
  // Router observes only events accepted for the current live turn.
  kiraInputRouterObserveEvent(
    event
  );
#endif

  switch(event.type) {
    case KIRA_EVENT_NONE:
      return true;

    case KIRA_EVENT_BOOT_STARTED:
      return kiraRuntimeRequestState(
        KIRA_STATE_BOOT,
        "BOOT_STARTED"
      );

    case KIRA_EVENT_BOOT_COMPLETE:
      return kiraRuntimeRequestState(
        KIRA_STATE_IDLE,
        "BOOT_COMPLETE"
      );

    case KIRA_EVENT_WIFI_CONNECTING:
      return kiraRuntimeRequestState(
        KIRA_STATE_WIFI_CONNECTING,
        "WIFI_CONNECTING"
      );

    case KIRA_EVENT_NETWORK_LOST:
    case KIRA_EVENT_SESSION_RECOVERING:
    {
      KiraRuntimeState s=
        kiraRuntimeState();

      // Do not smash LISTENING/TRANSCRIBING/SPEAKING in the middle of a turn.
      // The active subsystem can fail gracefully; reconnect state becomes
      // authoritative when KIRA is otherwise idle.
      if(
        s==
          KIRA_STATE_IDLE ||
        s==
          KIRA_STATE_WIFI_CONNECTING ||
        s==
          KIRA_STATE_RECONNECTING
      ){
        return kiraRuntimeRequestState(
          KIRA_STATE_RECONNECTING,
          kiraEventName(event.type)
        );
      }

      Serial.print(
        "[SESSION] network loss deferred while state="
      );

      Serial.println(
        kiraRuntimeStateName(s)
      );

      return true;
    }

    case KIRA_EVENT_NETWORK_RECOVERED:
    case KIRA_EVENT_SESSION_RECOVERED:
    {
      KiraRuntimeState s=
        kiraRuntimeState();

      if(
        s==
          KIRA_STATE_RECONNECTING ||
        s==
          KIRA_STATE_WIFI_CONNECTING
      ){
        return kiraRuntimeRequestState(
          KIRA_STATE_IDLE,
          kiraEventName(event.type)
        );
      }

      return true;
    }

    case KIRA_EVENT_WAKE_DETECTED:
      kiraMetricsIncrement(
        KIRA_METRIC_WAKE_ACCEPTS
      );

      return kiraRuntimeRequestState(
        KIRA_STATE_WAKE_DETECTED,
        "WAKE_DETECTED"
      );

    case KIRA_EVENT_WAKE_REJECTED:
      kiraMetricsIncrement(
        KIRA_METRIC_WAKE_REJECTS
      );
      return true;

    case KIRA_EVENT_LISTENING_STARTED:
    case KIRA_EVENT_SPEECH_STARTED:
      return kiraRuntimeRequestState(
        KIRA_STATE_LISTENING,
        kiraEventName(event.type)
      );

    case KIRA_EVENT_SPEECH_FINISHED:
    case KIRA_EVENT_ROUTING_STARTED:
      return kiraRuntimeRequestState(
        KIRA_STATE_ROUTING,
        kiraEventName(event.type)
      );

    case KIRA_EVENT_SPEECH_REJECTED:
      kiraMetricsIncrement(
        KIRA_METRIC_VAD_FALSE_TRIGGERS
      );

      return kiraRuntimeRequestState(
        KIRA_STATE_IDLE,
        "SPEECH_REJECTED"
      );

    case KIRA_EVENT_OFFLINE_COMMAND_MATCHED:
    case KIRA_EVENT_ACTION_STARTED:
      return kiraRuntimeRequestState(
        KIRA_STATE_ACTION,
        kiraEventName(event.type)
      );

    case KIRA_EVENT_ACTION_FINISHED:
      return kiraRuntimeRequestState(
        KIRA_STATE_IDLE,
        "ACTION_FINISHED"
      );

    case KIRA_EVENT_STT_REQUIRED:
      return kiraRuntimeRequestState(
        KIRA_STATE_TRANSCRIBING,
        "STT_REQUIRED"
      );

    case KIRA_EVENT_STT_READY:
      return kiraRuntimeRequestState(
        KIRA_STATE_ROUTING,
        "STT_READY"
      );

    case KIRA_EVENT_STT_FAILED:
      kiraMetricsIncrement(
        KIRA_METRIC_STT_FAILURES
      );

      return kiraRuntimeRequestState(
        KIRA_STATE_IDLE,
        "STT_FAILED"
      );

    case KIRA_EVENT_AI_STARTED:
      return kiraRuntimeRequestState(
        KIRA_STATE_THINKING,
        "AI_STARTED"
      );

    case KIRA_EVENT_AI_RESPONSE_READY:
      // A response being ready does not itself claim speaker ownership.
      // TTS_STARTED is the authoritative transition to SPEAKING.
      return true;

    case KIRA_EVENT_AI_FAILED:
      kiraMetricsIncrement(
        KIRA_METRIC_AI_FAILURES
      );

      return kiraRuntimeRequestState(
        KIRA_STATE_IDLE,
        "AI_FAILED"
      );

    case KIRA_EVENT_TTS_STARTED:
      return kiraRuntimeRequestState(
        KIRA_STATE_SPEAKING,
        "TTS_STARTED"
      );

    case KIRA_EVENT_TTS_FINISHED:
      return kiraRuntimeRequestState(
        KIRA_STATE_IDLE,
        "TTS_FINISHED"
      );

    case KIRA_EVENT_TTS_FAILED:
      kiraMetricsIncrement(
        KIRA_METRIC_TTS_FAILURES
      );

      return kiraRuntimeRequestState(
        KIRA_STATE_IDLE,
        "TTS_FAILED"
      );

    case KIRA_EVENT_INTERRUPT_REQUESTED:
      if(
        kiraRuntimeState() ==
        KIRA_STATE_SPEAKING
      ) {
        return kiraRuntimeRequestState(
          KIRA_STATE_LISTENING,
          "INTERRUPT_REQUESTED"
        );
      }

      Serial.print(
        "[INTERRUPT] ignored in state="
      );
      Serial.println(
        kiraRuntimeStateName()
      );
      return true;

    case KIRA_EVENT_OTA_STARTED:
      return kiraRuntimeRequestState(
        KIRA_STATE_OTA,
        "OTA_STARTED"
      );

    case KIRA_EVENT_OTA_FINISHED:
      return kiraRuntimeRequestState(
        KIRA_STATE_BOOT,
        "OTA_FINISHED"
      );

    case KIRA_EVENT_ERROR_OCCURRED:
      return kiraRuntimeRequestState(
        KIRA_STATE_ERROR,
        "ERROR_OCCURRED"
      );
  }

  return false;
}

} // namespace


bool kiraRuntimeBegin() {
#if !KIRA_STATE_MACHINE_ENABLED
  return true;
#else

  if(
    !kiraEventsBegin()
  ) {
    Serial.println(
      "[KIRA RUNTIME] ERROR - event bus unavailable"
    );

    return false;
  }


  setRuntimeStateRaw(
    KIRA_STATE_BOOT
  );

  illegalTransitionCount = 0;

  Serial.println(
    "[KIRA RUNTIME] READY"
  );

  Serial.println(
    "[KIRA STATE] BOOT"
  );

  return true;
#endif
}


void kiraRuntimeService() {
#if KIRA_STATE_MACHINE_ENABLED
  KiraEvent event = {};

  // Drain only a bounded batch per Arduino loop so a noisy producer can
  // never monopolize the main loop indefinitely.
  uint8_t handledThisPass = 0;

  while(
    handledThisPass < 8 &&
    kiraEventTake(
      event,
      0
    )
  ) {
    handleEvent(
      event
    );

    kiraEventMarkHandled();
    handledThisPass++;
  }
#endif
}


KiraRuntimeState kiraRuntimeState() {
  KiraRuntimeState state;

  portENTER_CRITICAL(
    &runtimeMux
  );

  state = currentState;

  portEXIT_CRITICAL(
    &runtimeMux
  );

  return state;
}


const char* kiraRuntimeStateName() {
  return kiraRuntimeStateName(
    kiraRuntimeState()
  );
}


bool kiraRuntimeRequestState(
  KiraRuntimeState next,
  const char* reason
) {
#if !KIRA_STATE_MACHINE_ENABLED
  (void)next;
  (void)reason;
  return true;
#else

  KiraRuntimeState from =
    kiraRuntimeState();


  if(
    from == next
  ) {
    return true;
  }


  if(
    !kiraRuntimeCanTransition(
      from,
      next
    )
  ) {
    portENTER_CRITICAL(
      &runtimeMux
    );

    illegalTransitionCount++;

    portEXIT_CRITICAL(
      &runtimeMux
    );

    kiraMetricsIncrement(
      KIRA_METRIC_ILLEGAL_TRANSITIONS
    );

    Serial.print(
      "[STATE] ILLEGAL_TRANSITION "
    );

    Serial.print(
      kiraRuntimeStateName(from)
    );

    Serial.print(
      " -> "
    );

    Serial.print(
      kiraRuntimeStateName(next)
    );

    if(
      reason != nullptr &&
      reason[0] != '\0'
    ) {
      Serial.print(
        " reason="
      );
      Serial.print(reason);
    }

    Serial.println();

    return false;
  }


  setRuntimeStateRaw(
    next
  );

  kiraMetricsIncrement(
    KIRA_METRIC_STATE_TRANSITIONS
  );


  Serial.print(
    "[KIRA STATE] "
  );

  Serial.print(
    kiraRuntimeStateName(from)
  );

  Serial.print(
    " -> "
  );

  Serial.print(
    kiraRuntimeStateName(next)
  );

  if(
    reason != nullptr &&
    reason[0] != '\0'
  ) {
    Serial.print(
      " cause="
    );
    Serial.print(reason);
  }

  Serial.println();

  return true;
#endif
}


bool kiraRuntimePost(
  KiraEventType type,
  KiraEventSource source,
  int32_t value,
  uint32_t token,
  uint16_t flags
) {
  return kiraEventPost(
    type,
    source,
    value,
    token,
    flags
  );
}


bool kiraRuntimeCanTransition(
  KiraRuntimeState from,
  KiraRuntimeState to
) {
  if(
    from == to
  ) {
    return true;
  }


  switch(from) {
    case KIRA_STATE_BOOT:
      return
        to == KIRA_STATE_WIFI_CONNECTING ||
        to == KIRA_STATE_IDLE ||
        to == KIRA_STATE_ERROR ||
        to == KIRA_STATE_OTA;

    case KIRA_STATE_WIFI_CONNECTING:
      return
        to == KIRA_STATE_IDLE ||
        to == KIRA_STATE_RECONNECTING ||
        to == KIRA_STATE_ERROR ||
        to == KIRA_STATE_OTA;

    case KIRA_STATE_IDLE:
      return
        to == KIRA_STATE_WAKE_DETECTED ||
        to == KIRA_STATE_LISTENING ||
        to == KIRA_STATE_ROUTING ||
        to == KIRA_STATE_ACTION ||
        to == KIRA_STATE_SPEAKING ||
        to == KIRA_STATE_RECONNECTING ||
        to == KIRA_STATE_ERROR ||
        to == KIRA_STATE_OTA;

    case KIRA_STATE_WAKE_DETECTED:
      return
        to == KIRA_STATE_LISTENING ||
        to == KIRA_STATE_IDLE ||
        to == KIRA_STATE_ERROR;

    case KIRA_STATE_LISTENING:
      return
        to == KIRA_STATE_ROUTING ||
        to == KIRA_STATE_IDLE ||
        to == KIRA_STATE_ERROR;

    case KIRA_STATE_ROUTING:
      return
        to == KIRA_STATE_TRANSCRIBING ||
        to == KIRA_STATE_THINKING ||
        to == KIRA_STATE_ACTION ||
        to == KIRA_STATE_SPEAKING ||
        to == KIRA_STATE_IDLE ||
        to == KIRA_STATE_ERROR;

    case KIRA_STATE_TRANSCRIBING:
      return
        to == KIRA_STATE_ROUTING ||
        to == KIRA_STATE_THINKING ||
        to == KIRA_STATE_IDLE ||
        to == KIRA_STATE_ERROR;

    case KIRA_STATE_THINKING:
      return
        to == KIRA_STATE_ACTION ||
        to == KIRA_STATE_SPEAKING ||
        to == KIRA_STATE_IDLE ||
        to == KIRA_STATE_ERROR;

    case KIRA_STATE_ACTION:
      return
        to == KIRA_STATE_SPEAKING ||
        to == KIRA_STATE_IDLE ||
        to == KIRA_STATE_ERROR;

    case KIRA_STATE_SPEAKING:
      return
        to == KIRA_STATE_IDLE ||
        to == KIRA_STATE_LISTENING ||
        to == KIRA_STATE_ERROR;

    case KIRA_STATE_RECONNECTING:
      return
        to == KIRA_STATE_WIFI_CONNECTING ||
        to == KIRA_STATE_IDLE ||
        to == KIRA_STATE_ERROR ||
        to == KIRA_STATE_OTA;

    case KIRA_STATE_ERROR:
      return
        to == KIRA_STATE_BOOT ||
        to == KIRA_STATE_IDLE ||
        to == KIRA_STATE_RECONNECTING ||
        to == KIRA_STATE_OTA;

    case KIRA_STATE_OTA:
      return
        to == KIRA_STATE_BOOT ||
        to == KIRA_STATE_ERROR;
  }


  return false;
}


uint32_t kiraRuntimeStateAgeMs() {
  uint32_t entered = 0;

  portENTER_CRITICAL(
    &runtimeMux
  );

  entered = stateEnteredMs;

  portEXIT_CRITICAL(
    &runtimeMux
  );

  return
    millis() - entered;
}


uint32_t kiraRuntimeIllegalTransitionCount() {
  uint32_t count = 0;

  portENTER_CRITICAL(
    &runtimeMux
  );

  count = illegalTransitionCount;

  portEXIT_CRITICAL(
    &runtimeMux
  );

  return count;
}


const char* kiraRuntimeStateName(
  KiraRuntimeState state
) {
  switch(state) {
    case KIRA_STATE_BOOT: return "BOOT";
    case KIRA_STATE_WIFI_CONNECTING: return "WIFI_CONNECTING";
    case KIRA_STATE_IDLE: return "IDLE";
    case KIRA_STATE_WAKE_DETECTED: return "WAKE_DETECTED";
    case KIRA_STATE_LISTENING: return "LISTENING";
    case KIRA_STATE_ROUTING: return "ROUTING";
    case KIRA_STATE_TRANSCRIBING: return "TRANSCRIBING";
    case KIRA_STATE_THINKING: return "THINKING";
    case KIRA_STATE_ACTION: return "ACTION";
    case KIRA_STATE_SPEAKING: return "SPEAKING";
    case KIRA_STATE_RECONNECTING: return "RECONNECTING";
    case KIRA_STATE_ERROR: return "ERROR";
    case KIRA_STATE_OTA: return "OTA";
  }

  return "UNKNOWN";
}
