#pragma once
#include <Arduino.h>

// Speech-to-text/casual typing normalization.
String normalizeSpeechUtterance(String input);

// Conservative typo correction for high-value intent words.
// v0.8.1 intentionally avoids insertion/deletion fuzzy matches
// because "time" -> "timer" caused the spacetime bug.
String fuzzyNormalizeIntentWords(String q);
