#pragma once
#include <Arduino.h>

// =====================================================
// ELLI CONVERSATION STATE V2
// =====================================================
//
// KIRA already remembers:
//   - web ambiguity choices
//   - "this / it / that" references
//
// Conversation State V2 adds a missing capability:
//
//   Elli asks a question
//          ↓
//   user answers normally
//          ↓
//   KIRA understands that answer as a CONTINUATION
//
// Examples:
//
//   YOU : suggest me something
//   ELLI: What kind of thing are you looking for?
//   YOU : craft
//        -> resumes the original suggestion request
//
//   YOU : i am happy today
//   ELLI: What made it so good?
//   YOU : i finished my project
//        -> understood as the answer to Elli's question
//
// It also supports multi-turn recurring-reminder setup.
// =====================================================

enum ElliDialogueKind {
  ELLI_DIALOGUE_NONE = 0,
  ELLI_DIALOGUE_KNOWLEDGE_FOLLOWUP,
  ELLI_DIALOGUE_CONVERSATION_FOLLOWUP,
  ELLI_DIALOGUE_ROUTINE_TIME
};

enum ElliDialogueResolutionKind {
  ELLI_DIALOGUE_RESOLVE_NONE = 0,
  ELLI_DIALOGUE_RESOLVE_KNOWLEDGE,
  ELLI_DIALOGUE_RESOLVE_LOCAL,
  ELLI_DIALOGUE_RESOLVE_ROUTINE_TIME,
  ELLI_DIALOGUE_RESOLVE_CANCELLED
};

struct ElliDialogueResolution {
  ElliDialogueResolutionKind kind=
    ELLI_DIALOGUE_RESOLVE_NONE;

  String originQuery;
  String assistantQuestion;
  String userAnswer;
  String payload;
};


// Explicit multi-turn setup used by recurring reminders.
void elliDialogueBeginRoutineTime(const String& title);


// Consume the user's next message if it answers a pending Elli question.
// Returns true only when the input belongs to the pending dialogue.
bool elliDialogueConsumeInput(
  const String& input,
  ElliDialogueResolution& resolution
);


// Observe a completed turn. If Elli's final response genuinely asks a
// follow-up question, Conversation State V2 remembers it.
void elliDialogueObserveTurn(
  const String& userQuery,
  const String& route,
  const String& assistantResponse
);


// Build a knowledge request containing the user's clarification.
String elliDialogueBuildKnowledgeResume(
  const ElliDialogueResolution& resolution
);


// Natural local continuation for emotional/chat follow-ups.
String elliDialogueComposeLocalReply(
  const ElliDialogueResolution& resolution
);


// Context lifecycle / debug.
bool elliDialogueHasPending();
void elliDialogueClear();
String elliDialogueStatus();
