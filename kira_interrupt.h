#pragma once

#include <Arduino.h>

bool kiraInterruptBegin();
void kiraInterruptService();

bool kiraInterruptRequest(
  const char* reason="USER_INTERRUPT"
);

bool kiraInterruptPending();
void kiraInterruptClear();

uint32_t kiraInterruptCount();
