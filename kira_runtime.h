#pragma once

#include <Arduino.h>

#include "kira_events.h"


// ============================================================
// KIRA NEXT - CENTRAL RUNTIME STATE MACHINE
// ============================================================


enum KiraRuntimeState : uint8_t {
  KIRA_STATE_BOOT = 0,
  KIRA_STATE_WIFI_CONNECTING,
  KIRA_STATE_IDLE,
  KIRA_STATE_WAKE_DETECTED,
  KIRA_STATE_LISTENING,
  KIRA_STATE_ROUTING,
  KIRA_STATE_TRANSCRIBING,
  KIRA_STATE_THINKING,
  KIRA_STATE_ACTION,
  KIRA_STATE_SPEAKING,
  KIRA_STATE_RECONNECTING,
  KIRA_STATE_ERROR,
  KIRA_STATE_OTA
};


bool kiraRuntimeBegin();
void kiraRuntimeService();

KiraRuntimeState kiraRuntimeState();
const char* kiraRuntimeStateName();

bool kiraRuntimeRequestState(
  KiraRuntimeState next,
  const char* reason = nullptr
);

bool kiraRuntimePost(
  KiraEventType type,
  KiraEventSource source = KIRA_EVENT_SOURCE_UNKNOWN,
  int32_t value = 0,
  uint32_t token = 0,
  uint16_t flags = 0
);

bool kiraRuntimeCanTransition(
  KiraRuntimeState from,
  KiraRuntimeState to
);

uint32_t kiraRuntimeStateAgeMs();
uint32_t kiraRuntimeIllegalTransitionCount();

const char* kiraRuntimeStateName(
  KiraRuntimeState state
);
