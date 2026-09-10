#pragma once
#include <Arduino.h>

static const uint8_t KIRA_MAX_NOTES = 16;

void kiraNotesBegin();
bool kiraNotesReady();
int kiraNotesCount();
String kiraNotesStatus();

// Direct API used by the Tool Engine for safe chaining.
bool kiraNotesCreate(const String& text,bool important,uint16_t& noteId,String& error);

// Natural-language note commands. Returns true only when the command belongs
// to the notes tool.
bool kiraNotesHandleCommand(String q);
