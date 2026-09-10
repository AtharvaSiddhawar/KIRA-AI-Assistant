#pragma once

#include <Arduino.h>


enum KiraRecoveryReason : uint8_t {
  KIRA_RECOVERY_NONE=0,
  KIRA_RECOVERY_RUNTIME_ERROR,
  KIRA_RECOVERY_STUCK_LISTENING,
  KIRA_RECOVERY_STUCK_ROUTING,
  KIRA_RECOVERY_STUCK_STT,
  KIRA_RECOVERY_STUCK_AI,
  KIRA_RECOVERY_STUCK_ACTION,
  KIRA_RECOVERY_STUCK_TTS,
  KIRA_RECOVERY_STUCK_RECONNECT,
  KIRA_RECOVERY_EVENT_OVERFLOW,
  KIRA_RECOVERY_LOW_MEMORY,
  KIRA_RECOVERY_MANUAL
};


bool kiraRecoveryBegin();
void kiraRecoveryService();

bool kiraRecoveryRequest(
  KiraRecoveryReason reason,
  const char* detail = nullptr
);

const char* kiraRecoveryReasonName(
  KiraRecoveryReason reason
);

uint32_t kiraRecoveryAttemptCount();
uint32_t kiraRecoverySuccessCount();

KiraRecoveryReason kiraRecoveryLastReason();
String kiraRecoveryLastDetail();

bool kiraRecoveryActive();
