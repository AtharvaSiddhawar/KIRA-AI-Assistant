#pragma once
#include <Arduino.h>

// =====================================================
// ELLI STUDY MODE V1
// =====================================================
// Uses KIRA's existing AI/web provider manager. No extra API/service.
//
// Commands:
//   study mode on / off
//   study <topic>
//   study style simple / brief / detailed
//   quiz me on <topic>
//   next question
//   quiz score
//   stop quiz
// =====================================================

void elliStudyBegin();
bool elliStudyHandleCommand(String q);
bool elliStudyModeActive();
String elliStudyDecorateKnowledgeQuery(const String& q);
String elliStudyStatus();
