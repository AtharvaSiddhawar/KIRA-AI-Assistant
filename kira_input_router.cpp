#include "kira_input_router.h"

#include "kira_next_config.h"

namespace {

bool ready=false;

KiraInputRoute route=
  KIRA_INPUT_ROUTE_NONE;

uint32_t turnId=0;


const char* routeName(
  KiraInputRoute r
){
  switch(r){
    case KIRA_INPUT_ROUTE_NONE:
      return "NONE";

    case KIRA_INPUT_ROUTE_OFFLINE:
      return "OFFLINE";

    case KIRA_INPUT_ROUTE_STT:
      return "STT";

    case KIRA_INPUT_ROUTE_UNIVERSAL:
      return "UNIVERSAL";
  }

  return "UNKNOWN";
}


void setRoute(
  KiraInputRoute next,
  const char* cause
){
  if(route==next) return;

  route=next;

  Serial.print(
    "[ROUTER V2] route="
  );

  Serial.print(
    routeName(route)
  );

  if(cause && cause[0]){
    Serial.print(
      " cause="
    );

    Serial.print(
      cause
    );
  }

  Serial.println();
}

}


bool kiraInputRouterBegin(){
#if !KIRA_ROUTER_V2_ENABLED
  return true;
#else
  ready=true;
  route=KIRA_INPUT_ROUTE_NONE;
  turnId=0;

  Serial.println(
    "[ROUTER V2] READY | existing Phase 5J-2 offline-first routing preserved"
  );

  return true;
#endif
}


void kiraInputRouterObserveEvent(
  const KiraEvent& event
){
#if KIRA_ROUTER_V2_ENABLED
  if(!ready) return;

  switch(event.type){
    case KIRA_EVENT_WAKE_DETECTED:
      turnId++;
      setRoute(
        KIRA_INPUT_ROUTE_NONE,
        "WAKE"
      );
      break;

    case KIRA_EVENT_OFFLINE_COMMAND_MATCHED:
      setRoute(
        KIRA_INPUT_ROUTE_OFFLINE,
        "MULTINET_MATCH"
      );

      Serial.print(
        "[ROUTER V2] offline command_id="
      );

      Serial.println(
        event.value
      );
      break;

    case KIRA_EVENT_STT_REQUIRED:
      setRoute(
        KIRA_INPUT_ROUTE_STT,
        "UNKNOWN_OFFLINE"
      );
      break;

    case KIRA_EVENT_STT_READY:
      setRoute(
        KIRA_INPUT_ROUTE_UNIVERSAL,
        "TRANSCRIPT_READY"
      );
      break;

    case KIRA_EVENT_STT_FAILED:
    case KIRA_EVENT_SPEECH_REJECTED:
      setRoute(
        KIRA_INPUT_ROUTE_NONE,
        kiraEventName(event.type)
      );
      break;

    default:
      break;
  }
#else
  (void)event;
#endif
}


KiraInputRoute kiraInputRouterRoute(){
  return route;
}


const char* kiraInputRouterRouteName(){
  return routeName(route);
}


uint32_t kiraInputRouterTurnId(){
  return turnId;
}
