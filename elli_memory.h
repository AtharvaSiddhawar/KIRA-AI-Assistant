#pragma once
#include <Arduino.h>

void elliMemoryBegin();

bool looksSensitiveForAutomaticMemory(String q);
int memoryCount();
bool addPersonalMemory(String text,uint8_t category);
void showPersonalMemory();

// Handles name/profile settings and explicit/automatic remember commands.
bool handleProfileMemoryIntent(String q);

// Handles personal questions BEFORE the web router:
// "what do i like", "what is my project called",
// "when do i go to tuition", "memory search electronics", etc.
bool handlePersonalRecallIntent(String q);

// Handles arbitrary transient/open-vocabulary personal statements locally.
bool handleNaturalPersonalStatement(String q);
// =====================================================
// PERSONAL MEMORY V2
// =====================================================
// Session-only RAM memory + explicit forgetting of saved profile facts.
void elliMemoryV2Begin();
bool handleMemoryV2Intent(String q);
void clearSessionMemory();
int sessionMemoryCount();
String memoryV2Status();

// =====================================================
// PERSONAL MEMORY V3
// =====================================================
// Adds semantic categories, importance and today-only persistence while
// preserving the existing BrainStore binary/NVS layout.
enum ElliMemoryV3Category : uint8_t {
  ELLI_MEM_GENERAL    = 10,
  ELLI_MEM_PERSON     = 11,
  ELLI_MEM_PREFERENCE = 12,
  ELLI_MEM_PROJECT    = 13,
  ELLI_MEM_FACT       = 14,
  ELLI_MEM_NOTE       = 15,
  ELLI_MEM_DEVICE     = 16,
  ELLI_MEM_STUDY      = 17
};

void elliMemoryV3Begin();
bool handleMemoryV3Intent(String q);
String memoryV3Status();
const char* elliMemoryCategoryName(uint8_t encodedCategory);
uint8_t elliInferMemoryCategory(const String& fact);
