#pragma once
#include <Arduino.h>

// Non-destructive diagnostics. Never toggles devices, creates timers/alarms,
// writes profile facts, or calls the AI provider during "test all".
void kiraSelfTestBegin();
bool kiraHandleSelfTestCommand(String q);
