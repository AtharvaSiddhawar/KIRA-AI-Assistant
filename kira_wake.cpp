#include "kira_wake.h"

#include "kira_metrics.h"
#include "kira_next_config.h"
#include "kira_runtime.h"

namespace {

constexpr uint32_t WAKE_DEBOUNCE_MS =
  850;

constexpr uint32_t POST_SPEECH_COOLDOWN_MS =
  1000;

volatile bool ready =
  false;

volatile bool speakerActive =
  false;

volatile uint32_t lastAcceptedMs =
  0;

volatile uint32_t cooldownUntilMs =
  0;

volatile uint32_t acceptedCount =
  0;

volatile uint32_t rejectedCount =
  0;

portMUX_TYPE wakeMux =
  portMUX_INITIALIZER_UNLOCKED;


bool beforeDeadline(
  uint32_t now,
  uint32_t deadline
) {
  return
    (int32_t)(
      deadline -
      now
    ) >
    0;
}

} // namespace


bool kiraWakeBegin() {
#if !KIRA_WAKE_SERVICE_ENABLED
  return true;
#else
  if(
    ready
  ) {
    return true;
  }

  speakerActive =
    false;

  lastAcceptedMs =
    0;

  cooldownUntilMs =
    0;

  acceptedCount =
    0;

  rejectedCount =
    0;

  ready =
    true;

  Serial.println(
    "[WAKE] READY | model=Hi ESP | debounce=850ms | post-speech cooldown=1000ms"
  );

  Serial.println(
    "[WAKE] Custom model slot reserved: Hey Cutie / Hey Elli / Hi Elli / Yoo Elli / Hi Babes"
  );

  return true;
#endif
}


bool kiraWakeAcceptDetection(
  sr_event_t event
) {
#if !KIRA_WAKE_SERVICE_ENABLED
  (void)event;
  return true;
#else
  if(
    !ready
  ) {
    kiraWakeBegin();
  }

  if(
    event !=
      SR_EVENT_WAKEWORD &&
    event !=
      SR_EVENT_WAKEWORD_CHANNEL
  ) {
    return false;
  }

  uint32_t now =
    millis();

  bool rejectSpeaker =
    speakerActive;

  bool rejectCooldown =
    beforeDeadline(
      now,
      cooldownUntilMs
    );

  bool rejectDebounce =
    lastAcceptedMs != 0 &&
    now -
      lastAcceptedMs <
      WAKE_DEBOUNCE_MS;

  if(
    rejectSpeaker ||
    rejectCooldown ||
    rejectDebounce
  ) {
    portENTER_CRITICAL(
      &wakeMux
    );

    rejectedCount++;

    portEXIT_CRITICAL(
      &wakeMux
    );

    kiraMetricsIncrement(
      KIRA_METRIC_WAKE_REJECTS
    );

    Serial.print(
      "[WAKE] REJECT reason="
    );

    if(
      rejectSpeaker
    ) {
      Serial.println(
        "speaker_active"
      );
    }
    else if(
      rejectCooldown
    ) {
      Serial.println(
        "post_speech_cooldown"
      );
    }
    else {
      Serial.println(
        "debounce"
      );
    }

    kiraRuntimePost(
      KIRA_EVENT_WAKE_REJECTED,
      KIRA_EVENT_SOURCE_WAKE
    );

    return false;
  }

  portENTER_CRITICAL(
    &wakeMux
  );

  lastAcceptedMs =
    now;

  acceptedCount++;

  portEXIT_CRITICAL(
    &wakeMux
  );

  Serial.println(
    "[WAKE] ACCEPT phrase=Hi ESP"
  );

  kiraRuntimePost(
    KIRA_EVENT_WAKE_DETECTED,
    KIRA_EVENT_SOURCE_WAKE
  );

  kiraRuntimePost(
    KIRA_EVENT_LISTENING_STARTED,
    KIRA_EVENT_SOURCE_WAKE
  );

  return true;
#endif
}


void kiraWakeSetSpeakerActive(
  bool active
) {
#if KIRA_WAKE_SERVICE_ENABLED
  speakerActive =
    active;

  if(
    !active
  ) {
    cooldownUntilMs =
      millis() +
      POST_SPEECH_COOLDOWN_MS;
  }
#else
  (void)active;
#endif
}


void kiraWakeArmIdle() {
#if KIRA_WAKE_SERVICE_ENABLED
  speakerActive =
    false;

  cooldownUntilMs =
    millis() +
    POST_SPEECH_COOLDOWN_MS;
#endif
}


uint32_t kiraWakeAcceptedCount() {
  return
    acceptedCount;
}


uint32_t kiraWakeRejectedCount() {
  return
    rejectedCount;
}
