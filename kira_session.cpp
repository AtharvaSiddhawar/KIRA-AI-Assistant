#include "kira_session.h"

#include "kira_gateway.h"
#include "kira_network_v2.h"
#include "kira_next_config.h"
#include "kira_runtime.h"

namespace {

bool ready=false;

KiraSessionState state=
  KIRA_SESSION_DISCONNECTED;

bool previousLink=false;

uint32_t recoveryCount=0;


const char* stateName(
  KiraSessionState s
){
  switch(s){
    case KIRA_SESSION_DISCONNECTED:
      return "DISCONNECTED";

    case KIRA_SESSION_CONNECTING:
      return "CONNECTING";

    case KIRA_SESSION_CONNECTED:
      return "CONNECTED";

    case KIRA_SESSION_OPEN:
      return "SESSION_OPEN";

    case KIRA_SESSION_VOICE_ACTIVE:
      return "VOICE_ACTIVE";

    case KIRA_SESSION_RECOVERING:
      return "RECOVERING";
  }

  return "UNKNOWN";
}


void setState(
  KiraSessionState next,
  const char* reason
){
  if(state==next) return;

  Serial.print(
    "[SESSION] "
  );

  Serial.print(
    stateName(state)
  );

  Serial.print(
    " -> "
  );

  Serial.print(
    stateName(next)
  );

  if(reason && reason[0]){
    Serial.print(
      " cause="
    );

    Serial.print(
      reason
    );
  }

  Serial.println();

  state=next;
}

}


bool kiraSessionBegin(){
#if !KIRA_SESSION_MANAGER_ENABLED
  return true;
#else
  previousLink=
    kiraNetworkConnected();

  state=
    previousLink
      ? KIRA_SESSION_CONNECTED
      : KIRA_SESSION_DISCONNECTED;

  ready=true;

  Serial.print(
    "[SESSION] READY | initial="
  );

  Serial.println(
    stateName(state)
  );

  return true;
#endif
}


void kiraSessionService(){
#if KIRA_SESSION_MANAGER_ENABLED
  if(!ready) return;

  bool link=
    kiraNetworkConnected();


  if(
    previousLink &&
    !link
  ){
    recoveryCount++;

    setState(
      KIRA_SESSION_RECOVERING,
      "NETWORK_LOST"
    );

    kiraRuntimePost(
      KIRA_EVENT_NETWORK_LOST,
      KIRA_EVENT_SOURCE_SESSION
    );

    kiraRuntimePost(
      KIRA_EVENT_SESSION_RECOVERING,
      KIRA_EVENT_SOURCE_SESSION
    );
  }


  if(
    !previousLink &&
    link
  ){
    setState(
      KIRA_SESSION_CONNECTED,
      "NETWORK_RECOVERED"
    );

    kiraRuntimePost(
      KIRA_EVENT_NETWORK_RECOVERED,
      KIRA_EVENT_SOURCE_SESSION
    );

    kiraRuntimePost(
      KIRA_EVENT_SESSION_RECOVERED,
      KIRA_EVENT_SOURCE_SESSION
    );
  }


  previousLink=
    link;


  if(
    !link
  ){
    if(
      state!=
      KIRA_SESSION_RECOVERING
    ){
      setState(
        KIRA_SESSION_DISCONNECTED,
        "NO_LINK"
      );
    }

    return;
  }


  if(
    kiraGatewayEnabled()
  ){
    if(
      kiraGatewayReady()
    ){
      setState(
        KIRA_SESSION_OPEN,
        "GATEWAY_READY"
      );
    }
    else{
      setState(
        KIRA_SESSION_CONNECTING,
        "GATEWAY_WAIT"
      );
    }
  }
  else{
    setState(
      KIRA_SESSION_CONNECTED,
      "DIRECT_MODE"
    );
  }
#endif
}


KiraSessionState kiraSessionState(){
  return state;
}


const char* kiraSessionStateName(){
  return
    stateName(state);
}


uint32_t kiraSessionRecoveryCount(){
  return recoveryCount;
}
