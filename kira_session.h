#pragma once

#include <Arduino.h>

enum KiraSessionState : uint8_t {
  KIRA_SESSION_DISCONNECTED=0,
  KIRA_SESSION_CONNECTING,
  KIRA_SESSION_CONNECTED,
  KIRA_SESSION_OPEN,
  KIRA_SESSION_VOICE_ACTIVE,
  KIRA_SESSION_RECOVERING
};

bool kiraSessionBegin();
void kiraSessionService();

KiraSessionState kiraSessionState();
const char* kiraSessionStateName();

uint32_t kiraSessionRecoveryCount();
