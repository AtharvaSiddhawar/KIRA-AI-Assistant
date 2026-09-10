#pragma once
#include <Arduino.h>

void kiraV1Begin();
bool kiraV1HasPendingContext();
bool kiraV1ShouldConsumePendingInput(const String& input);
void kiraV1ClearPendingContext();
bool kiraV1HandlePendingInput(const String& input);
bool kiraV1HandleStructuredWeb(const String& input);
