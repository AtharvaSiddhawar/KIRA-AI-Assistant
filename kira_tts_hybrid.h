#pragma once

#include <Arduino.h>

// True when Edge backup or a compiled PicoTTS backend is available.
bool kiraTtsHybridConfigured();

// Called only after the normal Groq/Diana path fails or is unavailable.
bool kiraTtsHybridFallback(
  const String& text
);
