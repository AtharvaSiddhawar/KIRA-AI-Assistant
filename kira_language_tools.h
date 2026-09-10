#pragma once
#include <Arduino.h>

bool kiraLooksLikeTranslationTool(const String& q);
bool kiraLooksLikeDictionaryTool(const String& q);

// Executes explicit translation/dictionary utility requests through KIRA's
// existing multi-provider reasoning path. Returns true if the request belonged
// to this module (even if the network/provider was unavailable).
bool kiraHandleTranslationTool(String q);
bool kiraHandleDictionaryTool(String q);
