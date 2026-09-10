#include "kira_metrics.h"

#include "kira_events.h"
#include "kira_next_config.h"
#include "kira_runtime.h"

#include <esp_heap_caps.h>


namespace {

volatile uint32_t counters[
  KIRA_METRIC_COUNT
] = {};

portMUX_TYPE metricsMux =
  portMUX_INITIALIZER_UNLOCKED;

uint32_t lastHealthPrintMs=0;
uint32_t lastObservedEventDrops=0;


bool validCounter(
  KiraMetricCounter counter
){
  return
    counter>=0 &&
    counter<KIRA_METRIC_COUNT;
}


const char* metricName(
  KiraMetricCounter counter
){
  switch(counter){
    case KIRA_METRIC_STATE_TRANSITIONS: return "state_transitions";
    case KIRA_METRIC_ILLEGAL_TRANSITIONS: return "illegal_transitions";
    case KIRA_METRIC_EVENT_DROPS: return "event_drops";
    case KIRA_METRIC_WAKE_ACCEPTS: return "wake_accepts";
    case KIRA_METRIC_WAKE_REJECTS: return "wake_rejects";
    case KIRA_METRIC_VAD_FALSE_TRIGGERS: return "vad_false_triggers";
    case KIRA_METRIC_AUDIO_INPUT_OVERRUNS: return "audio_input_overruns";
    case KIRA_METRIC_AUDIO_OUTPUT_UNDERRUNS: return "audio_output_underruns";
    case KIRA_METRIC_STT_FAILURES: return "stt_failures";
    case KIRA_METRIC_AI_FAILURES: return "ai_failures";
    case KIRA_METRIC_TTS_FAILURES: return "tts_failures";
    case KIRA_METRIC_WS_DISCONNECTS: return "ws_disconnects";
    case KIRA_METRIC_SESSION_RECOVERIES: return "session_recoveries";
    case KIRA_METRIC_TOOL_CALLS: return "tool_calls";
    case KIRA_METRIC_TOOL_FAILURES: return "tool_failures";
    case KIRA_METRIC_TOOL_REJECTIONS: return "tool_rejections";
    case KIRA_METRIC_STALE_EVENTS_IGNORED: return "stale_events_ignored";
    case KIRA_METRIC_RECOVERY_ATTEMPTS: return "recovery_attempts";
    case KIRA_METRIC_RECOVERY_SUCCESSES: return "recovery_successes";
    case KIRA_METRIC_HEALTH_WARNINGS: return "health_warnings";
    case KIRA_METRIC_COUNT: break;
  }

  return "unknown";
}

}


void kiraMetricsBegin(){
#if KIRA_METRICS_ENABLED
  for(
    size_t i=0;
    i<KIRA_METRIC_COUNT;
    i++
  ){
    counters[i]=0;
  }

  lastHealthPrintMs=
    millis();

  lastObservedEventDrops=0;

  Serial.println(
    "[KIRA METRICS V2] READY | tools + recovery + health snapshot"
  );

  kiraMetricsPrintHealth();
#endif
}


void kiraMetricsService(){
#if KIRA_METRICS_ENABLED
  uint32_t drops=
    kiraEventDroppedCount();

  if(
    drops>
    lastObservedEventDrops
  ){
    kiraMetricsIncrement(
      KIRA_METRIC_EVENT_DROPS,
      drops-lastObservedEventDrops
    );

    lastObservedEventDrops=
      drops;
  }


#if KIRA_METRICS_PERIODIC_PRINT
  uint32_t now=
    millis();

  if(
    now-lastHealthPrintMs>=
    KIRA_METRICS_PERIOD_MS
  ){
    lastHealthPrintMs=
      now;

    kiraMetricsPrintHealth();
  }
#endif
#endif
}


void kiraMetricsIncrement(
  KiraMetricCounter counter,
  uint32_t amount
){
#if KIRA_METRICS_ENABLED
  if(!validCounter(counter)){
    return;
  }

  portENTER_CRITICAL(
    &metricsMux
  );

  counters[counter]+=
    amount;

  portEXIT_CRITICAL(
    &metricsMux
  );
#else
  (void)counter;
  (void)amount;
#endif
}


uint32_t kiraMetricsGet(
  KiraMetricCounter counter
){
#if !KIRA_METRICS_ENABLED
  (void)counter;
  return 0;
#else
  if(!validCounter(counter)){
    return 0;
  }

  uint32_t value=0;

  portENTER_CRITICAL(
    &metricsMux
  );

  value=
    counters[counter];

  portEXIT_CRITICAL(
    &metricsMux
  );

  return value;
#endif
}


void kiraMetricsObserveLatency(
  const char* stage,
  uint32_t elapsedMs
){
#if KIRA_METRICS_ENABLED
  Serial.print(
    "[KIRA LATENCY] stage="
  );

  Serial.print(
    stage
      ? stage
      : "unknown"
  );

  Serial.print(
    " elapsed="
  );

  Serial.print(
    elapsedMs
  );

  Serial.println(
    "ms"
  );
#else
  (void)stage;
  (void)elapsedMs;
#endif
}


uint32_t kiraMetricsFreeHeap(){
  return ESP.getFreeHeap();
}


uint32_t kiraMetricsFreePsram(){
  return ESP.getFreePsram();
}


uint32_t kiraMetricsLargestHeapBlock(){
  return
    heap_caps_get_largest_free_block(
      MALLOC_CAP_8BIT |
      MALLOC_CAP_INTERNAL
    );
}


uint32_t kiraMetricsLargestPsramBlock(){
  return
    heap_caps_get_largest_free_block(
      MALLOC_CAP_SPIRAM
    );
}


KiraHealthSnapshot kiraMetricsSnapshot(){
  KiraHealthSnapshot s={};

  s.heapFree=
    kiraMetricsFreeHeap();

  s.heapLargest=
    kiraMetricsLargestHeapBlock();

  s.psramFree=
    kiraMetricsFreePsram();

  s.psramLargest=
    kiraMetricsLargestPsramBlock();

  s.eventPosted=
    kiraEventPostedCount();

  s.eventHandled=
    kiraEventHandledCount();

  s.eventDropped=
    kiraEventDroppedCount();

  s.stateAgeMs=
    kiraRuntimeStateAgeMs();

  s.illegalTransitions=
    kiraRuntimeIllegalTransitionCount();

  s.level=
    KIRA_HEALTH_GOOD;


  if(
    s.heapFree<
      KIRA_HEALTH_WARN_HEAP_BYTES ||
    s.heapLargest<
      KIRA_HEALTH_WARN_LARGEST_HEAP_BYTES ||
    (
      ESP.getPsramSize()>0 &&
      s.psramFree<
        KIRA_HEALTH_WARN_PSRAM_BYTES
    )
  ){
    s.level=
      KIRA_HEALTH_WARNING;
  }


  if(
    s.eventDropped>0 ||
    s.illegalTransitions>5 ||
    s.heapFree<
      (KIRA_HEALTH_WARN_HEAP_BYTES/2)
  ){
    s.level=
      KIRA_HEALTH_DEGRADED;
  }


  return s;
}


const char* kiraHealthLevelName(
  KiraHealthLevel level
){
  switch(level){
    case KIRA_HEALTH_GOOD: return "GOOD";
    case KIRA_HEALTH_WARNING: return "WARNING";
    case KIRA_HEALTH_DEGRADED: return "DEGRADED";
  }

  return "UNKNOWN";
}


void kiraMetricsPrintHealth(){
#if KIRA_METRICS_ENABLED
  KiraHealthSnapshot s=
    kiraMetricsSnapshot();

  Serial.print(
    "[KIRA HEALTH] level="
  );

  Serial.print(
    kiraHealthLevelName(
      s.level
    )
  );

  Serial.print(
    " heap_free="
  );

  Serial.print(
    s.heapFree
  );

  Serial.print(
    " heap_largest="
  );

  Serial.print(
    s.heapLargest
  );

  Serial.print(
    " psram_free="
  );

  Serial.print(
    s.psramFree
  );

  Serial.print(
    " psram_largest="
  );

  Serial.print(
    s.psramLargest
  );

  Serial.print(
    " events="
  );

  Serial.print(
    s.eventHandled
  );

  Serial.print(
    "/"
  );

  Serial.print(
    s.eventPosted
  );

  Serial.print(
    " drops="
  );

  Serial.print(
    s.eventDropped
  );

  Serial.print(
    " state="
  );

  Serial.print(
    kiraRuntimeStateName()
  );

  Serial.print(
    " state_age="
  );

  Serial.print(
    s.stateAgeMs
  );

  Serial.println(
    "ms"
  );
#endif
}


void kiraMetricsPrintCounters(){
#if KIRA_METRICS_ENABLED
  Serial.println(
    "[KIRA METRICS] counters"
  );

  for(
    uint8_t i=0;
    i<KIRA_METRIC_COUNT;
    i++
  ){
    KiraMetricCounter counter=
      static_cast<KiraMetricCounter>(i);

    Serial.print(
      "  "
    );

    Serial.print(
      metricName(
        counter
      )
    );

    Serial.print(
      "="
    );

    Serial.println(
      kiraMetricsGet(
        counter
      )
    );
  }
#endif
}
