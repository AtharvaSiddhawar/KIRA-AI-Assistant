#pragma once

#include <Arduino.h>

#include "kira_runtime.h"

bool kiraStateBusBegin();
void kiraStateBusService();

void kiraStateBusApply(
  KiraRuntimeState state
);

KiraRuntimeState kiraStateBusLastState();
