#include "kira_recovery.h"

#include "kira_audio_output.h"
#include "kira_events.h"
#include "kira_metrics.h"
#include "kira_network_v2.h"
#include "kira_next_config.h"
#include "kira_runtime.h"

namespace {

bool ready=false;
bool active=false;

uint32_t attempts=0;
uint32_t successes=0;

uint32_t lastObservedDrops=0;
uint32_t lastObservedIllegal=0;
uint32_t lastHealthWarningMs=0;
uint32_t lastRecoveryMs=0;

KiraRecoveryReason lastReason=
  KIRA_RECOVERY_NONE;

String lastDetail;


bool recoveryCooldownPassed(){
  return
    millis()-lastRecoveryMs>
    3000UL;
}


bool returnRuntimeToIdle(
  const char* reason
){
  if(
    kiraRuntimeState()==
    KIRA_STATE_IDLE
  ){
    return true;
  }

  return
    kiraRuntimeRequestState(
      KIRA_STATE_IDLE,
      reason
    );
}

}


const char* kiraRecoveryReasonName(
  KiraRecoveryReason reason
){
  switch(reason){
    case KIRA_RECOVERY_NONE: return "NONE";
    case KIRA_RECOVERY_RUNTIME_ERROR: return "RUNTIME_ERROR";
    case KIRA_RECOVERY_STUCK_LISTENING: return "STUCK_LISTENING";
    case KIRA_RECOVERY_STUCK_ROUTING: return "STUCK_ROUTING";
    case KIRA_RECOVERY_STUCK_STT: return "STUCK_STT";
    case KIRA_RECOVERY_STUCK_AI: return "STUCK_AI";
    case KIRA_RECOVERY_STUCK_ACTION: return "STUCK_ACTION";
    case KIRA_RECOVERY_STUCK_TTS: return "STUCK_TTS";
    case KIRA_RECOVERY_STUCK_RECONNECT: return "STUCK_RECONNECT";
    case KIRA_RECOVERY_EVENT_OVERFLOW: return "EVENT_OVERFLOW";
    case KIRA_RECOVERY_LOW_MEMORY: return "LOW_MEMORY";
    case KIRA_RECOVERY_MANUAL: return "MANUAL";
  }

  return "UNKNOWN";
}


bool kiraRecoveryBegin(){
#if !KIRA_RECOVERY_ENABLED
  return true;
#else
  ready=true;
  active=false;

  attempts=0;
  successes=0;

  lastObservedDrops=
    kiraEventDroppedCount();

  lastObservedIllegal=
    kiraRuntimeIllegalTransitionCount();

  lastHealthWarningMs=0;
  lastRecoveryMs=0;

  lastReason=
    KIRA_RECOVERY_NONE;

  lastDetail="";

  Serial.println(
    "[RECOVERY] READY | soft recovery only | automatic reboot=OFF"
  );

  return true;
#endif
}


bool kiraRecoveryRequest(
  KiraRecoveryReason reason,
  const char* detail
){
#if !KIRA_RECOVERY_ENABLED
  (void)reason;
  (void)detail;
  return false;
#else
  if(
    !ready ||
    active ||
    !recoveryCooldownPassed()
  ){
    return false;
  }


  active=true;
  attempts++;

  kiraMetricsIncrement(
    KIRA_METRIC_RECOVERY_ATTEMPTS
  );

  lastReason=
    reason;

  lastDetail=
    detail
      ? detail
      : "";

  lastRecoveryMs=
    millis();


  Serial.print(
    "[RECOVERY] START reason="
  );

  Serial.print(
    kiraRecoveryReasonName(
      reason
    )
  );

  if(lastDetail.length()){
    Serial.print(
      " detail="
    );

    Serial.print(
      lastDetail
    );
  }

  Serial.println();


  bool success=false;


  switch(reason){
    case KIRA_RECOVERY_STUCK_TTS:
      kiraAudioOutputAbort();
      kiraEventsFlush();

      success=
        returnRuntimeToIdle(
          "RECOVERY_TTS"
        );
      break;


    case KIRA_RECOVERY_RUNTIME_ERROR:
    case KIRA_RECOVERY_STUCK_LISTENING:
    case KIRA_RECOVERY_STUCK_ROUTING:
    case KIRA_RECOVERY_STUCK_STT:
    case KIRA_RECOVERY_STUCK_AI:
    case KIRA_RECOVERY_STUCK_ACTION:
    case KIRA_RECOVERY_EVENT_OVERFLOW:
    case KIRA_RECOVERY_MANUAL:
      // ESP-SR remains the only owner of real-time microphone I2S.
      // Recovery never starts a second mic reader.
      kiraEventsFlush();

      success=
        returnRuntimeToIdle(
          "RECOVERY_SOFT"
        );
      break;


    case KIRA_RECOVERY_STUCK_RECONNECT:
      if(
        kiraNetworkConnected()
      ){
        kiraEventsFlush();

        success=
          returnRuntimeToIdle(
            "RECOVERY_NETWORK_ALREADY_UP"
          );
      }
      else{
        // Network/Session remain authoritative for reconnect.
        success=true;
      }
      break;


    case KIRA_RECOVERY_LOW_MEMORY:
      kiraAudioOutputAbort();

      if(
        kiraRuntimeState()==
        KIRA_STATE_SPEAKING
      ){
        success=
          returnRuntimeToIdle(
            "RECOVERY_LOW_MEMORY"
          );
      }
      else{
        success=true;
      }
      break;


    case KIRA_RECOVERY_NONE:
      success=false;
      break;
  }


  if(success){
    successes++;

    kiraMetricsIncrement(
      KIRA_METRIC_RECOVERY_SUCCESSES
    );
  }


  Serial.print(
    "[RECOVERY] END result="
  );

  Serial.print(
    success
      ? "OK"
      : "FAILED"
  );

  Serial.print(
    " state="
  );

  Serial.println(
    kiraRuntimeStateName()
  );


#if KIRA_RECOVERY_AUTO_REBOOT
  // Intentionally OFF in this release.
#endif


  active=false;

  return success;
#endif
}


void kiraRecoveryService(){
#if KIRA_RECOVERY_ENABLED
  if(
    !ready ||
    active
  ){
    return;
  }


  uint32_t drops=
    kiraEventDroppedCount();

  if(
    drops>
    lastObservedDrops
  ){
    lastObservedDrops=
      drops;

    kiraRecoveryRequest(
      KIRA_RECOVERY_EVENT_OVERFLOW,
      "central event queue dropped events"
    );

    return;
  }


  uint32_t illegal=
    kiraRuntimeIllegalTransitionCount();

  if(
    illegal>
    lastObservedIllegal
  ){
    lastObservedIllegal=
      illegal;

    Serial.print(
      "[RECOVERY] observed illegal transition count="
    );

    Serial.println(
      illegal
    );
  }


  KiraHealthSnapshot health=
    kiraMetricsSnapshot();

  if(
    health.level!=
      KIRA_HEALTH_GOOD &&
    millis()-lastHealthWarningMs>
      10000UL
  ){
    lastHealthWarningMs=
      millis();

    kiraMetricsIncrement(
      KIRA_METRIC_HEALTH_WARNINGS
    );

    Serial.print(
      "[RECOVERY] health warning level="
    );

    Serial.println(
      kiraHealthLevelName(
        health.level
      )
    );
  }


  if(
    health.heapFree<
      (KIRA_HEALTH_WARN_HEAP_BYTES/2)
  ){
    kiraRecoveryRequest(
      KIRA_RECOVERY_LOW_MEMORY,
      "internal heap below critical threshold"
    );

    return;
  }


  KiraRuntimeState state=
    kiraRuntimeState();

  uint32_t age=
    kiraRuntimeStateAgeMs();


  switch(state){
    case KIRA_STATE_ERROR:
      if(
        age>
        KIRA_RECOVERY_ERROR_GRACE_MS
      ){
        kiraRecoveryRequest(
          KIRA_RECOVERY_RUNTIME_ERROR,
          "runtime remained in ERROR"
        );
      }
      break;


    case KIRA_STATE_WAKE_DETECTED:
      if(age>8000UL){
        kiraRecoveryRequest(
          KIRA_RECOVERY_STUCK_LISTENING,
          "wake did not progress to listening"
        );
      }
      break;


    case KIRA_STATE_LISTENING:
      if(
        age>
        KIRA_RECOVERY_LISTENING_MAX_MS
      ){
        kiraRecoveryRequest(
          KIRA_RECOVERY_STUCK_LISTENING,
          "listening exceeded safety window"
        );
      }
      break;


    case KIRA_STATE_ROUTING:
      if(
        age>
        KIRA_RECOVERY_ROUTING_MAX_MS
      ){
        kiraRecoveryRequest(
          KIRA_RECOVERY_STUCK_ROUTING,
          "routing exceeded safety window"
        );
      }
      break;


    case KIRA_STATE_TRANSCRIBING:
      if(
        age>
        KIRA_RECOVERY_STT_MAX_MS
      ){
        kiraRecoveryRequest(
          KIRA_RECOVERY_STUCK_STT,
          "STT exceeded safety window"
        );
      }
      break;


    case KIRA_STATE_THINKING:
      if(
        age>
        KIRA_RECOVERY_AI_MAX_MS
      ){
        kiraRecoveryRequest(
          KIRA_RECOVERY_STUCK_AI,
          "AI exceeded safety window"
        );
      }
      break;


    case KIRA_STATE_ACTION:
      if(
        age>
        KIRA_RECOVERY_ACTION_MAX_MS
      ){
        kiraRecoveryRequest(
          KIRA_RECOVERY_STUCK_ACTION,
          "tool/action exceeded safety window"
        );
      }
      break;


    case KIRA_STATE_SPEAKING:
      if(
        age>
        KIRA_RECOVERY_TTS_MAX_MS
      ){
        kiraRecoveryRequest(
          KIRA_RECOVERY_STUCK_TTS,
          "speaker remained active too long"
        );
      }
      break;


    case KIRA_STATE_RECONNECTING:
      if(
        age>
        KIRA_RECOVERY_RECONNECT_MAX_MS
      ){
        kiraRecoveryRequest(
          KIRA_RECOVERY_STUCK_RECONNECT,
          "reconnect state exceeded safety window"
        );
      }
      break;


    case KIRA_STATE_BOOT:
    case KIRA_STATE_WIFI_CONNECTING:
    case KIRA_STATE_IDLE:
    case KIRA_STATE_OTA:
      break;
  }
#endif
}


uint32_t kiraRecoveryAttemptCount(){
  return attempts;
}


uint32_t kiraRecoverySuccessCount(){
  return successes;
}


KiraRecoveryReason kiraRecoveryLastReason(){
  return lastReason;
}


String kiraRecoveryLastDetail(){
  return lastDetail;
}


bool kiraRecoveryActive(){
  return active;
}
