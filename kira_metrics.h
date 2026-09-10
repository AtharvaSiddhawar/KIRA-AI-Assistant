#pragma once

#include <Arduino.h>


enum KiraMetricCounter : uint8_t {
  KIRA_METRIC_STATE_TRANSITIONS = 0,
  KIRA_METRIC_ILLEGAL_TRANSITIONS,
  KIRA_METRIC_EVENT_DROPS,
  KIRA_METRIC_WAKE_ACCEPTS,
  KIRA_METRIC_WAKE_REJECTS,
  KIRA_METRIC_VAD_FALSE_TRIGGERS,
  KIRA_METRIC_AUDIO_INPUT_OVERRUNS,
  KIRA_METRIC_AUDIO_OUTPUT_UNDERRUNS,
  KIRA_METRIC_STT_FAILURES,
  KIRA_METRIC_AI_FAILURES,
  KIRA_METRIC_TTS_FAILURES,
  KIRA_METRIC_WS_DISCONNECTS,
  KIRA_METRIC_SESSION_RECOVERIES,

  KIRA_METRIC_TOOL_CALLS,
  KIRA_METRIC_TOOL_FAILURES,
  KIRA_METRIC_TOOL_REJECTIONS,
  KIRA_METRIC_STALE_EVENTS_IGNORED,
  KIRA_METRIC_RECOVERY_ATTEMPTS,
  KIRA_METRIC_RECOVERY_SUCCESSES,
  KIRA_METRIC_HEALTH_WARNINGS,

  KIRA_METRIC_COUNT
};


enum KiraHealthLevel : uint8_t {
  KIRA_HEALTH_GOOD=0,
  KIRA_HEALTH_WARNING,
  KIRA_HEALTH_DEGRADED
};


struct KiraHealthSnapshot {
  uint32_t heapFree;
  uint32_t heapLargest;
  uint32_t psramFree;
  uint32_t psramLargest;

  uint32_t eventPosted;
  uint32_t eventHandled;
  uint32_t eventDropped;

  uint32_t stateAgeMs;
  uint32_t illegalTransitions;

  KiraHealthLevel level;
};


void kiraMetricsBegin();
void kiraMetricsService();

void kiraMetricsIncrement(
  KiraMetricCounter counter,
  uint32_t amount = 1
);

uint32_t kiraMetricsGet(
  KiraMetricCounter counter
);

void kiraMetricsObserveLatency(
  const char* stage,
  uint32_t elapsedMs
);

KiraHealthSnapshot kiraMetricsSnapshot();

const char* kiraHealthLevelName(
  KiraHealthLevel level
);

void kiraMetricsPrintHealth();
void kiraMetricsPrintCounters();

uint32_t kiraMetricsFreeHeap();
uint32_t kiraMetricsFreePsram();
uint32_t kiraMetricsLargestHeapBlock();
uint32_t kiraMetricsLargestPsramBlock();
