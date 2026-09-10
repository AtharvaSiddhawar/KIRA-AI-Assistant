#include "kira_state_bus.h"

#include "elli_visual.h"
#include "kira_next_config.h"

namespace {

bool ready=false;

KiraRuntimeState lastState=
  KIRA_STATE_BOOT;

ElliVisualState mapVisual(
  KiraRuntimeState state
){
  switch(state){
    case KIRA_STATE_BOOT:
      return ELLI_VISUAL_IDLE;

    case KIRA_STATE_WIFI_CONNECTING:
      return ELLI_VISUAL_CONFUSED;

    case KIRA_STATE_IDLE:
      return ELLI_VISUAL_IDLE;

    case KIRA_STATE_WAKE_DETECTED:
      return ELLI_VISUAL_EXCITED;

    case KIRA_STATE_LISTENING:
      return ELLI_VISUAL_LISTENING;

    case KIRA_STATE_ROUTING:
    case KIRA_STATE_TRANSCRIBING:
    case KIRA_STATE_THINKING:
      return ELLI_VISUAL_THINKING;

    case KIRA_STATE_ACTION:
      return ELLI_VISUAL_HAPPY;

    case KIRA_STATE_SPEAKING:
      return ELLI_VISUAL_SPEAKING;

    case KIRA_STATE_RECONNECTING:
      return ELLI_VISUAL_CONFUSED;

    case KIRA_STATE_ERROR:
      return ELLI_VISUAL_SAD;

    case KIRA_STATE_OTA:
      return ELLI_VISUAL_EXCITED;
  }

  return ELLI_VISUAL_IDLE;
}

}


bool kiraStateBusBegin(){
#if !KIRA_STATE_BUS_ENABLED
  return true;
#else
  ready=true;
  lastState=kiraRuntimeState();

  Serial.println(
    "[ELLI STATE BUS] READY"
  );

  kiraStateBusApply(
    lastState
  );

  return true;
#endif
}


void kiraStateBusApply(
  KiraRuntimeState state
){
#if KIRA_STATE_BUS_ENABLED
  if(!ready) return;

  ElliVisualState target=
    mapVisual(state);

  // Apply once per actual runtime transition.
  // Do NOT continuously force IDLE, because Elli's existing sleepy/idle
  // animation logic must remain free to operate after the transition.
  elliVisualSetState(
    target
  );

  Serial.print(
    "[ELLI STATE BUS] "
  );

  Serial.print(
    kiraRuntimeStateName(state)
  );

  Serial.print(
    " -> visual="
  );

  Serial.println(
    (int)target
  );
#else
  (void)state;
#endif
}


void kiraStateBusService(){
#if KIRA_STATE_BUS_ENABLED
  if(!ready) return;

  KiraRuntimeState now=
    kiraRuntimeState();

  if(now==lastState) return;

  lastState=now;

  kiraStateBusApply(
    now
  );
#endif
}


KiraRuntimeState kiraStateBusLastState(){
  return lastState;
}
