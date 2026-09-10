#include <Arduino.h>

#include "elli_dialogue_v2.h"
#include "elli_intent.h"
#include "elli_nlu.h"
#include "elli_semantic_v2.h"


String normalizeInput(String s);


// =====================================================
// INTERNAL STATE
// =====================================================

struct ElliDialogueState {

  bool active=false;

  ElliDialogueKind kind=
    ELLI_DIALOGUE_NONE;

  String originQuery;
  String assistantQuestion;
  String payload;

  uint32_t createdMs=0;
};


static ElliDialogueState dialogueState;


static const uint32_t DIALOGUE_TTL_MS=
  120000UL;


// =====================================================
// HELPERS
// =====================================================

static String dlgNormalize(String s){

  s=
    normalizeInput(
      s
    );

  s.trim();

  return s;
}


static int dlgWordCount(String q){

  q=
    dlgNormalize(
      q
    );


  if(!q.length()){
    return 0;
  }


  int count=1;


  for(
    size_t i=0;
    i<q.length();
    i++
  ){

    if(q[i]==' '){
      count++;
    }
  }


  return count;
}


static bool dlgExpired(){

  if(!dialogueState.active){
    return false;
  }


  return
    millis()-
    dialogueState.createdMs
    >
    DIALOGUE_TTL_MS;
}


static void dlgExpireIfNeeded(){

  if(dlgExpired()){

    dialogueState=
      ElliDialogueState();
  }
}


static bool dlgCancelPhrase(String q){

  q=
    dlgNormalize(
      q
    );


  return
    q=="never mind" ||
    q=="nevermind" ||
    q=="forget it" ||
    q=="cancel that" ||
    q=="cancel context" ||
    q=="skip it" ||
    q=="leave it";
}


static bool dlgLooksLikeTimeAnswer(String q){

  q=
    dlgNormalize(
      q
    );


  if(
    q=="noon" ||
    q=="midnight"
  ){

    return true;
  }


  if(
    q.indexOf(" am")>=0 ||
    q.endsWith("am") ||
    q.indexOf(" pm")>=0 ||
    q.endsWith("pm") ||
    q.indexOf(":")>=0
  ){

    return true;
  }


  // A short numeric reply such as:
  //   7
  //   730
  // is allowed to reach the clock parser which can then
  // ask AM/PM if it is ambiguous.
  if(
    dlgWordCount(q)<=2
  ){

    bool hasDigit=false;


    for(
      size_t i=0;
      i<q.length();
      i++
    ){

      if(
        isdigit(
          (unsigned char)q[i]
        )
      ){

        hasDigit=true;
      }
    }


    if(hasDigit){
      return true;
    }
  }


  return false;
}


static bool dlgStartsExplicitNewQuestion(String q){

  q=
    dlgNormalize(
      q
    );


  const char* const starts[]={

    "what ",
    "who ",
    "where ",
    "when ",
    "why ",
    "how ",
    "which ",

    "is ",
    "are ",
    "was ",
    "were ",

    "do ",
    "does ",
    "did ",

    "can ",
    "could ",
    "should ",
    "would ",
    "will ",
    "may ",
    "might ",

    "tell me ",
    "show me ",
    "explain ",
    "define ",
    "suggest ",
    "recommend "
  };


  for(
    size_t i=0;
    i<
    sizeof(starts)/
    sizeof(starts[0]);
    i++
  ){

    if(
      q.startsWith(
        starts[i]
      )
    ){

      return true;
    }
  }


  return false;
}


static bool dlgStrongStandaloneRequest(String q){

  q=
    dlgNormalize(
      q
    );


  int intent=
    detectUtteranceIntent(
      q
    );


  if(
    intent==UTT_DEVICE ||
    intent==UTT_CLOCK ||
    intent==UTT_TASK ||
    intent==UTT_ROUTINE
  ){

    return true;
  }


  // A normal first-person profile/support statement may itself be the
  // ANSWER to Elli's question:
  //
  //   ELLI: What made today good?
  //   YOU : I finished my project.
  //
  // So PROFILE/SUPPORT labels are not automatically treated as a new
  // request. Explicit memory/support commands still interrupt.
  if(
    q.startsWith("remember ") ||
    q.startsWith("memory ") ||
    q.startsWith("motivate me") ||
    q.startsWith("inspire me") ||
    q.startsWith("help me ")
  ){

    return true;
  }


  // Explicit new questions interrupt a pending conversational answer.
  if(
    dlgStartsExplicitNewQuestion(
      q
    )
  ){

    return true;
  }


  // Clear social closers/openers are new turns.
  if(
    intent==UTT_CHAT &&
    (
      q=="hi" ||
      q=="hello" ||
      q=="hey" ||
      q=="bye" ||
      q=="thanks" ||
      q=="thank you"
    )
  ){

    return true;
  }


  return false;
}


static String dlgExtractLastQuestion(String response){

  response.trim();


  if(
    !response.length()
  ){

    return "";
  }


  int qmark=
    response.lastIndexOf(
      '?'
    );


  if(qmark<0){
    return "";
  }


  // Only treat it as an active follow-up if the final question is
  // effectively at the end of the assistant response.
  String after=
    response.substring(
      qmark+1
    );

  after.trim();


  if(after.length()){
    return "";
  }


  int start=0;


  for(
    int i=qmark-1;
    i>=0;
    i--
  ){

    char c=
      response[i];


    if(
      c=='.' ||
      c=='!' ||
      c=='?'
    ){

      start=i+1;
      break;
    }
  }


  String question=
    response.substring(
      start,
      qmark+1
    );

  question.trim();

  return question;
}


static bool dlgGenericNewRequestPrompt(String question){

  String q=
    dlgNormalize(
      question
    );


  const char* const generic[]={

    "anything else",
    "what else",
    "what is next",
    "whats next",
    "what do you need",
    "what can i help with",
    "what are we doing",
    "what are we doing today",
    "got anything for me"
  };


  for(
    size_t i=0;
    i<
    sizeof(generic)/
    sizeof(generic[0]);
    i++
  ){

    if(
      q.indexOf(
        generic[i]
      )>=0
    ){

      return true;
    }
  }


  return false;
}


static bool dlgKnowledgeRoute(String route){

  return
    route=="V1 UNIVERSAL QUERY" ||
    route=="SEMANTIC CORE" ||
    route=="WEB";
}


static bool dlgConversationRoute(String route){

  return
    route=="PERSONAL CONVERSATION" ||
    route=="LOCAL CHAT" ||
    route=="SUPPORT / MOTIVATION" ||
    route=="DIALOGUE FOLLOWUP";
}


static String dlgSentence(String s){

  s.trim();


  while(
    s.endsWith(".") ||
    s.endsWith("!") ||
    s.endsWith("?")
  ){

    s.remove(
      s.length()-1
    );

    s.trim();
  }


  if(!s.length()){
    return "";
  }


  s.setCharAt(
    0,
    toupper(
      (unsigned char)s[0]
    )
  );


  return
    s+
    ".";
}


static String dlgLowerInitial(String s){

  s.trim();


  if(s.length()){

    s.setCharAt(
      0,
      tolower(
        (unsigned char)s[0]
      )
    );
  }


  return s;
}


static bool dlgContainsAny(
  String q,
  const char* const words[],
  size_t count
){

  q=
    dlgNormalize(
      q
    );


  for(
    size_t i=0;
    i<count;
    i++
  ){

    if(
      q.indexOf(
        words[i]
      )>=0
    ){

      return true;
    }
  }


  return false;
}


static uint8_t dlgPick(
  uint8_t count,
  uint8_t& last
){

  if(count<=1){

    last=0;

    return 0;
  }


  uint8_t p=
    (uint8_t)random(
      count
    );


  if(p==last){

    p=
      (uint8_t)(
        (p+1)%
        count
      );
  }


  last=p;

  return p;
}


// =====================================================
// PUBLIC STATE
// =====================================================

bool elliDialogueHasPending(){

  dlgExpireIfNeeded();

  return
    dialogueState.active;
}


void elliDialogueClear(){

  dialogueState=
    ElliDialogueState();
}


void elliDialogueBeginRoutineTime(
  const String& title
){

  dialogueState=
    ElliDialogueState();


  dialogueState.active=true;

  dialogueState.kind=
    ELLI_DIALOGUE_ROUTINE_TIME;

  dialogueState.payload=
    title;

  dialogueState.assistantQuestion=
    "What time should I remind you every day?";

  dialogueState.createdMs=
    millis();
}


// =====================================================
// CONSUME NEXT USER TURN
// =====================================================

bool elliDialogueConsumeInput(
  const String& input,
  ElliDialogueResolution& resolution
){

  resolution=
    ElliDialogueResolution();


  dlgExpireIfNeeded();


  if(
    !dialogueState.active
  ){

    return false;
  }


  String q=
    dlgNormalize(
      input
    );


  if(
    dlgCancelPhrase(
      q
    )
  ){

    resolution.kind=
      ELLI_DIALOGUE_RESOLVE_CANCELLED;

    resolution.originQuery=
      dialogueState.originQuery;

    resolution.assistantQuestion=
      dialogueState.assistantQuestion;

    resolution.userAnswer=
      q;

    resolution.payload=
      dialogueState.payload;


    elliDialogueClear();

    return true;
  }


  // ---------------------------------------------------
  // ROUTINE-TIME SETUP
  // ---------------------------------------------------

  if(
    dialogueState.kind==
    ELLI_DIALOGUE_ROUTINE_TIME
  ){

    if(
      dlgLooksLikeTimeAnswer(
        q
      )
    ){

      resolution.kind=
        ELLI_DIALOGUE_RESOLVE_ROUTINE_TIME;

      resolution.assistantQuestion=
        dialogueState.assistantQuestion;

      resolution.userAnswer=
        q;

      resolution.payload=
        dialogueState.payload;


      elliDialogueClear();

      return true;
    }


    // A genuinely new command interrupts the setup.
    if(
      dlgStrongStandaloneRequest(
        q
      )
    ){

      elliDialogueClear();

      return false;
    }


    // Otherwise let the caller attempt to parse the short answer
    // and re-prompt if needed.
    if(
      dlgWordCount(q)<=5
    ){

      resolution.kind=
        ELLI_DIALOGUE_RESOLVE_ROUTINE_TIME;

      resolution.userAnswer=
        q;

      resolution.payload=
        dialogueState.payload;


      elliDialogueClear();

      return true;
    }


    elliDialogueClear();

    return false;
  }


  // ---------------------------------------------------
  // GENERAL ASSISTANT FOLLOW-UP
  // ---------------------------------------------------

  if(
    dlgStrongStandaloneRequest(
      q
    )
  ){

    // The user deliberately started a different request.
    elliDialogueClear();

    return false;
  }


  resolution.originQuery=
    dialogueState.originQuery;

  resolution.assistantQuestion=
    dialogueState.assistantQuestion;

  resolution.userAnswer=
    q;

  resolution.payload=
    dialogueState.payload;


  if(
    dialogueState.kind==
    ELLI_DIALOGUE_KNOWLEDGE_FOLLOWUP
  ){

    resolution.kind=
      ELLI_DIALOGUE_RESOLVE_KNOWLEDGE;
  }

  else{

    resolution.kind=
      ELLI_DIALOGUE_RESOLVE_LOCAL;
  }


  elliDialogueClear();

  return true;
}


// =====================================================
// OBSERVE ASSISTANT OUTPUT
// =====================================================

void elliDialogueObserveTurn(
  const String& userQuery,
  const String& route,
  const String& assistantResponse
){

  dlgExpireIfNeeded();


  // Explicit setup states must not be overwritten by a generic
  // "response ends with ?" detector.
  if(
    dialogueState.active
  ){

    return;
  }


  String question=
    dlgExtractLastQuestion(
      assistantResponse
    );


  if(
    !question.length() ||
    dlgGenericNewRequestPrompt(
      question
    )
  ){

    return;
  }


  ElliDialogueKind kind=
    ELLI_DIALOGUE_NONE;


  if(
    dlgKnowledgeRoute(
      route
    )
  ){

    kind=
      ELLI_DIALOGUE_KNOWLEDGE_FOLLOWUP;
  }

  else if(
    dlgConversationRoute(
      route
    )
  ){

    kind=
      ELLI_DIALOGUE_CONVERSATION_FOLLOWUP;
  }

  else{

    return;
  }


  dialogueState=
    ElliDialogueState();


  dialogueState.active=true;

  dialogueState.kind=
    kind;

  dialogueState.originQuery=
    userQuery;

  dialogueState.assistantQuestion=
    question;

  dialogueState.createdMs=
    millis();


  Serial.print(
    "[DIALOGUE V2] Waiting for answer -> "
  );

  Serial.println(
    question
  );
}


// =====================================================
// KNOWLEDGE RESUME
// =====================================================

String elliDialogueBuildKnowledgeResume(
  const ElliDialogueResolution& r
){

  String request=
    r.originQuery;


  request+=
    " The assistant asked this follow-up question: "+
    r.assistantQuestion;


  request+=
    " The user answered: "+
    r.userAnswer+
    ".";


  request+=
    " Continue the original request using that answer. "
    "Give the direct useful result now and do not ask the same clarification again.";


  return request;
}


// =====================================================
// LOCAL CONVERSATION CONTINUATION
// =====================================================

String elliDialogueComposeLocalReply(
  const ElliDialogueResolution& r
){

  String origin=
    dlgNormalize(
      r.originQuery
    );


  String answer=
    dlgSentence(
      elliPerspectiveToUser(
        r.userAnswer
      )
    );


  String lowerAnswer=
    dlgLowerInitial(
      answer
    );


  const char* const positiveWords[]={

    "happy",
    "excited",
    "great",
    "awesome",
    "proud",
    "good",
    "amazing",
    "relaxed"
  };


  const char* const difficultWords[]={

    "tired",
    "exhausted",
    "drained",
    "stressed",
    "worried",
    "frustrated",
    "sad",
    "upset",
    "overwhelmed",
    "lonely"
  };


  static uint8_t lastPositive=255;
  static uint8_t lastDifficult=255;
  static uint8_t lastNeutral=255;


  if(
    dlgContainsAny(
      origin,
      positiveWords,
      sizeof(positiveWords)/
      sizeof(positiveWords[0])
    )
  ){

    const char* const a[]={

      "Ahh, that explains it — ",
      "Niceee, that makes sense — ",
      "Yooo, I get it now — ",
      "Okay, that explains the good mood — "
    };


    uint8_t i=
      dlgPick(
        (uint8_t)(
          sizeof(a)/
          sizeof(a[0])
        ),
        lastPositive
      );


    return
      String(a[i])+
      lowerAnswer+
      " Nice!";
  }


  if(
    dlgContainsAny(
      origin,
      difficultWords,
      sizeof(difficultWords)/
      sizeof(difficultWords[0])
    )
  ){

    const char* const a[]={

      "Yeah, that would explain it — ",
      "I get why you'd feel that way — ",
      "That makes more sense now — ",
      "Okay, I see what was behind it — "
    };


    uint8_t i=
      dlgPick(
        (uint8_t)(
          sizeof(a)/
          sizeof(a[0])
        ),
        lastDifficult
      );


    return
      String(a[i])+
      lowerAnswer+
      " Hope things ease up a bit.";
  }


  const char* const a[]={

    "Got you — ",
    "Ahh, okay — ",
    "That gives me the context — ",
    "Yep, I follow now — ",
    "Okay, that answers what I was asking — "
  };


  uint8_t i=
    dlgPick(
      (uint8_t)(
        sizeof(a)/
        sizeof(a[0])
      ),
      lastNeutral
    );


  return
    String(a[i])+
    lowerAnswer;
}


// =====================================================
// STATUS
// =====================================================

String elliDialogueStatus(){

  dlgExpireIfNeeded();


  if(
    !dialogueState.active
  ){

    return
      "No assistant follow-up is waiting for an answer right now.";
  }


  String type="conversation";


  if(
    dialogueState.kind==
    ELLI_DIALOGUE_KNOWLEDGE_FOLLOWUP
  ){

    type="knowledge follow-up";
  }

  else if(
    dialogueState.kind==
    ELLI_DIALOGUE_ROUTINE_TIME
  ){

    type="routine-time setup";
  }


  String r=
    "Pending "+
    type+
    ": "+
    dialogueState.assistantQuestion;


  if(
    dialogueState.payload.length()
  ){

    r+=
      " | pending item: "+
      dialogueState.payload;
  }


  return r;
}
