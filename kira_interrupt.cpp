#include "kira_interrupt.h"

#include "kira_audio_output.h"
#include "kira_next_config.h"
#include "kira_runtime.h"

namespace {

bool ready=false;
volatile bool pending=false;
uint32_t count=0;

}


bool kiraInterruptBegin(){
#if !KIRA_INTERRUPT_ENABLED
  return true;
#else
  ready=true;
  pending=false;
  count=0;

  Serial.println(
    "[INTERRUPT] READY | playback flush supported | auto full-duplex=OFF"
  );

  return true;
#endif
}


bool kiraInterruptRequest(
  const char* reason
){
#if !KIRA_INTERRUPT_ENABLED
  (void)reason;
  return false;
#else
  if(!ready) return false;

  if(
    kiraRuntimeState()!=
    KIRA_STATE_SPEAKING
  ){
    Serial.print(
      "[INTERRUPT] ignored state="
    );

    Serial.println(
      kiraRuntimeStateName()
    );

    return false;
  }

  pending=true;
  count++;

  Serial.print(
    "[INTERRUPT] REQUEST reason="
  );

  Serial.println(
    reason ? reason : "UNKNOWN"
  );

  // AudioOutput is queue-owned and safe to flush immediately.
  kiraAudioOutputAbort();

  kiraRuntimePost(
    KIRA_EVENT_INTERRUPT_REQUESTED,
    KIRA_EVENT_SOURCE_INTERRUPT
  );

  return true;
#endif
}


void kiraInterruptService(){
#if KIRA_INTERRUPT_ENABLED
  // Architecture hook only.
  // Automatic mic-triggered full-duplex interruption is deliberately OFF.
#endif
}


bool kiraInterruptPending(){
  return pending;
}


void kiraInterruptClear(){
  pending=false;
}


uint32_t kiraInterruptCount(){
  return count;
}
