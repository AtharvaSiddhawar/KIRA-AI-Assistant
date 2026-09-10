#include <Arduino.h>
#include "elli_intent.h"
#include "elli_language.h"
#include "elli_nlu.h"

// Still in kira_brain.ino during migration.
String normalizeInput(String s);

const char* utteranceIntentName(int intent){
  switch(intent){
    case UTT_CHAT:      return "CONVERSATION";
    case UTT_PROFILE:   return "PROFILE / MEMORY";
    case UTT_TASK:      return "TASK / REMINDER";
    case UTT_ROUTINE:   return "DAILY ROUTINE";
    case UTT_DEVICE:    return "DEVICE COMMAND";
    case UTT_QUESTION:  return "QUESTION / KNOWLEDGE";
    case UTT_SUPPORT:   return "SUPPORT / MOTIVATION";
    case UTT_CLOCK:     return "TIME / DATE / CLOCK / TIMER / ALARM";
    default:            return "PERSONAL STATEMENT";
  }
}


static bool elliHasNaturalTaskScheduleCue(const String& original){
  String q=normalizeInput(original);
  const char* dayMarkers[]={
    " tomorrow"," today"," on monday"," on tuesday"," on wednesday"," on thursday",
    " on friday"," on saturday"," on sunday"," by monday"," by tuesday"," by wednesday",
    " by thursday"," by friday"," by saturday"," by sunday"
  };
  for(size_t i=0;i<sizeof(dayMarkers)/sizeof(dayMarkers[0]);i++) if(q.indexOf(dayMarkers[i])>=0) return true;
  int at=q.indexOf(" at ");
  if(at>=0){
    for(int i=at+4;i<q.length();i++) if(isdigit((unsigned char)q[i])) return true;
  }
  return false;
}


bool startsLikeKnowledgeQuestion(const String& q){

  // Universal grammar first: WH questions, auxiliary inversion, modal
  // questions, polite directives, and embedded interrogatives.
  if(elliLooksLikeQuestionSyntax(q)){
    return true;
  }

  // Legacy opener list remains as a compatibility safety net.
  const char* questionOpeners[]={

    // WH questions
    "what ",
    "why ",
    "where ",
    "when ",
    "who ",
    "which ",
    "how ",

    // Yes / no questions
    "is ",
    "are ",
    "was ",
    "were ",

    "do ",
    "does ",
    "did ",

    "has ",
    "have ",
    "had ",

    "will ",
    "would ",

    "can ",
    "could ",
    "should ",

    "may ",
    "might ",

    // Explicit knowledge / request forms
    "can you tell ",
    "could you tell ",
    "would you tell ",
    "do you know ",

    "tell me ",
    "show me ",
    "give me ",
    "find ",
    "look up ",
    "search for ",

    "i want to know ",

    "explain ",
    "define "
  };


  for(
    size_t i=0;
    i<
    sizeof(questionOpeners)/
    sizeof(questionOpeners[0]);
    i++
  ){

    if(
      q.startsWith(
        questionOpeners[i]
      )
    ){

      return true;
    }
  }


  return false;
}


// =====================================================
//       STRUCTURAL LOCAL DATE/TIME DETECTOR
// =====================================================
//
// This DOES NOT depend on exact sentences.
//
// Local examples:
//
//   what is date
//   what is the date
//   what day is it
//   current date
//   what time is it
//   current time
//   what is the date and time
//
// Knowledge examples:
//
//   define time
//   what is time dilation
//   what is date palm
//   what time is sunrise
//
// =====================================================

static bool temporalAllowedToken(const String& word){
  const char* const allowed[]={
    "what",
    "whats",
    "is",
    "are",
    "the",
    "tell",
    "me",
    "please",
    "give",
    "show",
    "current",
    "now",
    "right",
    "today",
    "todays",
    "date",
    "day",
    "time",
    "clock",
    "it",
    "and"
  };

  for(size_t i=0;i<sizeof(allowed)/sizeof(allowed[0]);i++){
    if(word==allowed[i]){
      return true;
    }
  }

  return false;
}


TemporalRequestKind detectLocalTemporalRequest(String q){
  q=normalizeInput(q);
  q.trim();

  if(!q.length()){
    return TEMPORAL_NONE;
  }

  // ---------------------------------------------------
  // DEFINITION / EXPLANATION GUARD
  // ---------------------------------------------------
  //
  // These mean:
  //
  //   "Explain the concept"
  //
  // rather than:
  //
  //   "Tell me your current clock/date"
  //
  // ---------------------------------------------------

  if(
    q.startsWith("define ") ||
    q.startsWith("explain ") ||
    q.indexOf("meant by ")>=0 ||
    q.indexOf("meaning of ")>=0 ||
    q.indexOf("what does ")>=0
  ){
    return TEMPORAL_NONE;
  }

  bool wantsDate=false;
  bool wantsTime=false;
  bool hasUnknownContent=false;

  int pos=0;

  while(pos<q.length()){

    while(
      pos<q.length() &&
      q[pos]==' '
    ){
      pos++;
    }

    if(pos>=q.length()){
      break;
    }

    int end=q.indexOf(' ',pos);

    if(end<0){
      end=q.length();
    }

    String word=q.substring(pos,end);
    word.trim();

    // -------------------------------------------------
    // Detect DATE concept
    // -------------------------------------------------

    if(
      word=="date" ||
      word=="day" ||
      word=="today" ||
      word=="todays"
    ){
      wantsDate=true;
    }

    // -------------------------------------------------
    // Detect TIME concept
    // -------------------------------------------------

    if(
      word=="time" ||
      word=="clock"
    ){
      wantsTime=true;
    }

    // -------------------------------------------------
    // Extra semantic words mean this is probably
    // NOT asking for KIRA's current date/time.
    //
    // Examples:
    //
    // time dilation
    // date palm
    // time sunrise
    //
    // dilation/palm/sunrise are semantic content.
    // -------------------------------------------------

    if(!temporalAllowedToken(word)){
      hasUnknownContent=true;
    }

    pos=end+1;
  }

  if(
    !wantsDate &&
    !wantsTime
  ){
    return TEMPORAL_NONE;
  }

  if(hasUnknownContent){
    return TEMPORAL_NONE;
  }

  if(
    wantsDate &&
    wantsTime
  ){
    return TEMPORAL_DATE_TIME;
  }

  if(wantsDate){
    return TEMPORAL_DATE;
  }

  return TEMPORAL_TIME;
}


// =====================================================
//                 MAIN INTENT DETECTOR
// =====================================================
static bool elliHasWordToken(
  const String& q,
  const String& word
){

  if(q==word){
    return true;
  }


  if(
    q.startsWith(
      word+" "
    )
  ){
    return true;
  }


  if(
    q.endsWith(
      " "+word
    )
  ){
    return true;
  }


  if(
    q.indexOf(
      " "+word+" "
    )>=0
  ){
    return true;
  }


  return false;
}


// =====================================================
//        LOCAL CLOCK / ALARM / TIMER MANAGEMENT
// =====================================================
//
// This detects the CLASS of local clock controls.
//
// Examples:
//
// set an alarm for 6 am
// what alarms do i have
// cancel alarm 2
// pause timer
// is the timer running
// start stopwatch
//
// But:
//
// what is an alarm
// explain how a stopwatch works
//
// remain knowledge questions.
// =====================================================

static bool elliLooksLikeClockControl(
  String q
){

  q=
    normalizeInput(q);


  // ===================================================
  // UNIVERSAL BARE STOP
  // ===================================================
  //
  // Wake-word removal turns:
  //
  //   "Elli stop"
  //
  // into:
  //
  //   "stop"
  //
  // A bare stop must NEVER become a web fact lookup.
  // Runtime code decides what is currently active:
  // alarm alert, timer, stopwatch, etc.
  // ===================================================

  if(
    q=="stop" ||
    q=="stop now" ||
    q=="please stop"
  ){

    return true;
  }


  bool hasClockObject=

    q.indexOf("alarm")>=0 ||

    q.indexOf("timer")>=0 ||

    q.indexOf("stopwatch")>=0 ||

    q.indexOf("countdown")>=0;


  // ---------------------------------------------------
  // Commands that imply the clock system directly.
  // ---------------------------------------------------

  if(
    q.startsWith("wake me at ") ||

    q.startsWith("wake me up at ") ||

    q.startsWith("count until ") ||

    q.startsWith("count down ") ||

    q.startsWith("countdown ")
  ){

    return true;
  }


  if(!hasClockObject){

    return false;
  }


  // ---------------------------------------------------
  // ACTION / CONTROL VERBS
  // ---------------------------------------------------

  bool control=

    elliHasWordToken(q,"set") ||

    elliHasWordToken(q,"add") ||

    elliHasWordToken(q,"start") ||

    elliHasWordToken(q,"stop") ||

    elliHasWordToken(q,"reset") ||

    elliHasWordToken(q,"pause") ||

    elliHasWordToken(q,"resume") ||

    elliHasWordToken(q,"cancel") ||

    elliHasWordToken(q,"delete") ||

    elliHasWordToken(q,"remove") ||

    elliHasWordToken(q,"clear") ||

    elliHasWordToken(q,"silence");


  if(control){

    return true;
  }


  // ---------------------------------------------------
  // CLOCK STATE / PERSONAL STATE QUESTIONS
  // ---------------------------------------------------

  bool stateQuery=

    q.indexOf("do i have")>=0 ||

    q.indexOf("have i got")>=0 ||

    q.indexOf("are set")>=0 ||

    q.indexOf("is set")>=0 ||

    q.indexOf("status")>=0 ||

    q.indexOf("running")>=0 ||

    q.indexOf("remaining")>=0 ||

    q.indexOf("time left")>=0 ||

    q.indexOf("elapsed")>=0 ||

    q.indexOf("ringing")>=0 ||

    q.indexOf("active")>=0 ||

    q.startsWith("list alarm") ||

    q.startsWith("show alarm") ||

    q.startsWith("what alarm") ||

    q.startsWith("which alarm") ||

    q.startsWith("how many alarm") ||

    q.startsWith("list timer") ||

    q.startsWith("show timer") ||

    q.startsWith("what timer") ||

    q.startsWith("how long is left") ||

    q.startsWith("how much time is left");


  if(stateQuery){

    return true;
  }


  // ---------------------------------------------------
  // SHORTHAND COMMAND FORMS
  // ---------------------------------------------------

  if(
    q.startsWith("alarm at ") ||

    q.startsWith("alarm for ") ||

    q.startsWith("timer for ")
  ){

    return true;
  }


  return false;
}



// =====================================================
//          PERSONAL LANGUAGE / MEMORY SENSE
// =====================================================
//
// The old detector treated:
//
//   "who are my sisters"
//
// as a generic web question because it only recognized a
// handful of exact memory phrases.
//
// This layer detects AUTOBIOGRAPHICAL grammar as a class.
// It still requires a personal-profile topic so ordinary
// knowledge phrases containing "my" do not all become memory.
// =====================================================

static bool elliPersonalTopicWord(
  const String& word
){

  const char* const topics[]={

    "name",
    "memory",
    "memories",

    "sister",
    "sisters",
    "brother",
    "brothers",
    "sibling",
    "siblings",

    "mother",
    "mom",
    "mum",
    "father",
    "dad",
    "parent",
    "parents",

    "cousin",
    "cousins",
    "uncle",
    "aunt",

    "grandmother",
    "grandfather",
    "grandparent",

    "friend",
    "friends",
    "teacher",
    "teachers",
    "pet",
    "pets",

    "favorite",
    "favourite",
    "hobby",
    "hobbies",
    "project",
    "projects",

    "class",
    "grade",
    "school",
    "college",
    "university",
    "study",

    "birthday",
    "age",

    "goal",
    "goals",
    "dream",
    "dreams",

    "like",
    "love",
    "prefer",
    "preference",
    "preferences",

    "play",
    "use"
  };


  for(
    size_t i=0;
    i<
    sizeof(topics)/
    sizeof(topics[0]);
    i++
  ){

    if(
      word==
      topics[i]
    ){

      return true;
    }
  }


  return false;
}


static bool elliHasPersonalTopic(
  String q
){

  q=
    normalizeInput(
      q
    );


  int pos=0;


  while(pos<q.length()){

    while(
      pos<q.length() &&
      q[pos]==' '
    ){
      pos++;
    }


    if(pos>=q.length()){
      break;
    }


    int end=
      q.indexOf(
        ' ',
        pos
      );


    if(end<0){
      end=q.length();
    }


    if(
      elliPersonalTopicWord(
        q.substring(
          pos,
          end
        )
      )
    ){

      return true;
    }


    pos=
      end+1;
  }


  return false;
}


static bool elliProspectiveSelfQuestion(
  String q
){

  q=
    normalizeInput(
      q
    );


  // ---------------------------------------------------
  // QUESTIONS ABOUT WHAT THE USER SHOULD / COULD / CAN DO
  // ARE ADVICE / FEASIBILITY QUESTIONS, NOT MEMORY RECALL.
  //
  // Generic class examples:
  //
  //   what should i study after 10th
  //   could i use an esp32 for this
  //   should my project use dma
  //   how can i improve this
  //   which option should i choose
  // ---------------------------------------------------

  const char* const directStarts[]={

    "can i ",
    "could i ",
    "should i ",
    "would i ",
    "may i ",
    "might i ",
    "must i ",
    "will i ",
    "shall i ",

    "can we ",
    "could we ",
    "should we ",
    "would we ",
    "may we ",
    "might we ",
    "must we ",
    "will we ",
    "shall we ",

    "can my ",
    "could my ",
    "should my ",
    "would my ",
    "may my ",
    "might my ",
    "must my ",
    "will my "
  };


  for(
    size_t i=0;
    i<
    sizeof(directStarts)/
    sizeof(directStarts[0]);
    i++
  ){

    if(
      q.startsWith(
        directStarts[i]
      )
    ){

      return true;
    }
  }


  const char* const embedded[]={

    " should i ",
    " can i ",
    " could i ",
    " would i ",
    " may i ",
    " might i ",
    " must i ",
    " will i ",

    " should we ",
    " can we ",
    " could we ",
    " would we ",
    " may we ",
    " might we ",
    " must we ",
    " will we ",

    " should my ",
    " can my ",
    " could my ",
    " would my ",
    " will my "
  };


  String padded=
    " "+
    q+
    " ";


  for(
    size_t i=0;
    i<
    sizeof(embedded)/
    sizeof(embedded[0]);
    i++
  ){

    if(
      padded.indexOf(
        embedded[i]
      )>=0
    ){

      return true;
    }
  }


  // "what do i need to...", "what do i have to..." and
  // similar planning forms are prospective too.
  const char* const planningPhrases[]={

    "do i need to ",
    "do i have to ",
    "do i need ",
    "do i choose ",
    "do i pick ",
    "do i buy ",
    "do i get ",
    "do i make ",
    "do i build ",
    "do i use ",
    "do i change ",
    "do i improve ",
    "do i fix ",
    "do i solve ",
    "do i start ",
    "do i learn "
  };


  for(
    size_t i=0;
    i<
    sizeof(planningPhrases)/
    sizeof(planningPhrases[0]);
    i++
  ){

    if(
      q.indexOf(
        planningPhrases[i]
      )>=0
    ){

      return true;
    }
  }


  return false;
}


static bool elliLooksLikePersonalRecall(
  String q
){

  q=
    normalizeInput(
      q
    );


  if(
    !elliLooksLikeQuestionSyntax(
      q
    )
  ){

    return false;
  }


  // ===================================================
  // EXPLICIT / POSSESSIVE PERSONAL-KNOWLEDGE QUESTIONS
  // ===================================================
  //
  // These are about the user's OWN stored profile, not the
  // public internet.
  //
  // The rule is structural:
  //
  //   memory/knowledge verb
  //       +
  //   me / my / mine
  //
  // not a list of specific family words.
  //
  // Covers:
  //   tell me about my sisters
  //   do you know my sisters
  //   what do you know about my project
  //   have i told you about my school
  //   can you tell me about my hobbies
  //
  // while advice forms such as:
  //   is my project good
  //   should my project use dma
  //
  // are still handled later as reasoning/advice.
  // ===================================================

  bool explicitPersonalMemory=

    q.indexOf("do you remember")>=0 ||

    q.indexOf("did i tell you")>=0 ||

    q.indexOf("have i told you")>=0 ||

    q.indexOf("what do you remember about me")>=0 ||

    q.indexOf("what do you know about me")>=0 ||

    q.indexOf("what do you remember about my ")>=0 ||

    q.indexOf("what do you know about my ")>=0 ||

    q.startsWith("tell me about me") ||

    q.startsWith("tell me about my ") ||

    q.startsWith("tell me something about my ") ||

    q.startsWith("can you tell me about my ") ||

    q.startsWith("could you tell me about my ") ||

    q.startsWith("do you know my ") ||

    q.startsWith("do you know who my ") ||

    q.startsWith("do you know what my ") ||

    q.startsWith("do you know where my ") ||

    q.startsWith("do you know when my ") ||

    q.startsWith("describe my ") ||

    q.startsWith("memory search ") ||

    q.startsWith("search memory ");


  if(explicitPersonalMemory){

    return true;
  }


  // Advice / capability / planning beats profile-memory recall.
  if(
    elliProspectiveSelfQuestion(
      q
    )
  ){

    return false;
  }


  // ---------------------------------------------------
  // POSSESSIVE RECALL
  //
  // Open vocabulary, but structure matters.
  //
  //   what is my grade
  //   who are my sisters
  //   where is my school
  //   when is my birthday
  //   which is my project
  //
  // NOT:
  //
  //   is my project good
  //   why is my code crashing
  // ---------------------------------------------------

  const char* const possessiveRecallStarts[]={

    "what is my ",
    "what are my ",

    "who is my ",
    "who are my ",

    "where is my ",
    "where are my ",

    "when is my ",
    "when are my ",

    "which is my ",
    "which are my ",

    "tell me my ",
    "show me my ",
    "give me my ",

    "tell me what my ",
    "tell me who my ",
    "tell me where my ",
    "tell me when my ",
    "tell me which my "
  };


  for(
    size_t i=0;
    i<
    sizeof(possessiveRecallStarts)/
    sizeof(possessiveRecallStarts[0]);
    i++
  ){

    if(
      q.startsWith(
        possessiveRecallStarts[i]
      )
    ){

      return true;
    }
  }


  if(
    q.indexOf("do you remember my ")>=0 ||
    q.indexOf("what do you know about my ")>=0 ||
    q.indexOf("what do you remember about my ")>=0
  ){

    return true;
  }


  // ---------------------------------------------------
  // FIRST-PERSON STORED-FACT RECALL
  //
  // Open vocabulary around stable autobiographical relations:
  //
  //   where do i live
  //   what languages do i speak
  //   what do i like
  //   how many pets do i have
  //   how old am i
  //   what grade am i in
  // ---------------------------------------------------

  if(
    q.startsWith("how old am i") ||
    q.startsWith("how many ") &&
    (
      q.indexOf(" do i have")>=0 ||
      q.indexOf(" have i ")>=0
    )
  ){

    return true;
  }


  bool whRecall=

    q.startsWith("what ") ||
    q.startsWith("who ") ||
    q.startsWith("where ") ||
    q.startsWith("when ") ||
    q.startsWith("which ");


  if(
    whRecall &&
    (
      q.indexOf(" do i ")>=0 ||
      q.indexOf(" am i ")>=0 ||
      q.indexOf(" have i ")>=0 ||
      q.indexOf(" did i ")>=0
    )
  ){

    // Questions explicitly asking "why" / "how to" are reasoning,
    // not memory retrieval. Those never enter this branch anyway.
    return true;
  }


  return false;
}


static bool elliLooksLikePersonalFactStatement(
  String q
){

  q=
    normalizeInput(
      q
    );


  // Open-vocabulary personal facts are recognized from grammar and
  // sentence structure rather than a finite subject list.
  if(elliLooksLikeStablePersonalFactSyntax(q)){
    return true;
  }


  // Legacy topic-aware rules remain as a compatibility safety net.
  if(
    !elliHasPersonalTopic(
      q
    )
  ){

    return false;
  }


  if(
    q.indexOf(" is my ")>=0 ||
    q.indexOf(" are my ")>=0 ||
    q.indexOf(" was my ")>=0 ||
    q.indexOf(" were my ")>=0
  ){

    return true;
  }


  return

    q.startsWith("my favorite ") ||
    q.startsWith("my favourite ") ||
    q.startsWith("my hobby ") ||
    q.startsWith("my project ") ||

    q.startsWith("i like ") ||
    q.startsWith("i love ") ||
    q.startsWith("i prefer ") ||
    q.startsWith("i study ") ||
    q.startsWith("i play ") ||
    q.startsWith("i use ") ||

    q.startsWith("i am in class ") ||
    q.startsWith("i am in grade ") ||
    q.startsWith("i am a student");
}




// =====================================================
//              BARE KNOWLEDGE TOPIC SENSE
// =====================================================
//
// Judges/users do not always speak in full questions:
//
//   "quantum computing"
//   "taj mahal location"
//   "population of india"
//   "weather yavatmal"
//
// A short noun/topic phrase can be treated as a knowledge
// request IF it does not look like a personal statement or
// action command.
// =====================================================

static bool elliLooksLikeBareKnowledgeTopic(
  String q
){

  q=
    normalizeInput(
      q
    );


  if(
    !q.length() ||
    q.length()>90
  ){

    return false;
  }


  // Personal / conversational sentences are not topic lookups.
  if(
    q.startsWith("i ") ||
    q.startsWith("my ") ||
    q.startsWith("we ") ||
    q.startsWith("you ") ||
    q.indexOf(" i ")>=0 ||
    q.indexOf(" my ")>=0
  ){

    return false;
  }


  // Imperative/action families are handled elsewhere.
  const char* const actionStarts[]={

    "turn ",
    "switch ",
    "power ",

    "set ",
    "start ",
    "stop ",
    "pause ",
    "resume ",
    "cancel ",
    "delete ",
    "remove ",
    "add ",
    "create ",

    "open ",
    "close ",
    "play ",
    "send ",
    "call "
  };


  for(
    size_t i=0;
    i<
    sizeof(actionStarts)/
    sizeof(actionStarts[0]);
    i++
  ){

    if(
      q.startsWith(
        actionStarts[i]
      )
    ){

      return false;
    }
  }


  // A finite-verb sentence is probably a statement rather than
  // a bare encyclopedia/search topic.
  const char* const statementTokens[]={

    " am ",
    " is ",
    " are ",
    " was ",
    " were ",

    " feel ",
    " felt ",

    " went ",
    " got ",
    " made ",
    " said ",
    " told ",

    " like ",
    " love ",
    " hate ",
    " prefer "
  };


  String padded=
    " "+
    q+
    " ";


  for(
    size_t i=0;
    i<
    sizeof(statementTokens)/
    sizeof(statementTokens[0]);
    i++
  ){

    if(
      padded.indexOf(
        statementTokens[i]
      )>=0
    ){

      return false;
    }
  }


  // Keep this deliberately short-topic oriented.
  int words=1;


  for(
    size_t i=0;
    i<q.length();
    i++
  ){

    if(q[i]==' '){
      words++;
    }
  }


  return
    words<=8;
}


int detectUtteranceIntent(String q){

  q=
    normalizeInput(
      q
    );


  // ===================================================
  // 1) EXPLICIT PROFILE / MEMORY COMMANDS
  // ===================================================

  if(
    q.startsWith("my name is ") ||
    q.startsWith("call me ") ||
    q.startsWith("remember that ") ||
    q.startsWith("save a note ") ||
    q.startsWith("save note ") ||
    q.startsWith("add a note ") ||
    q.startsWith("add note ") ||
    q.startsWith("note that ") ||
    q=="show my notes" ||
    q=="show notes" ||
    q=="list notes" ||
    q.startsWith("search notes for ") ||
    q.startsWith("delete note ") ||
    (
      q.startsWith("remember ") &&
      !q.startsWith("remember to ")
    ) ||
    q.indexOf("remember everything about me")>=0 ||
    q.indexOf("what do you remember about me")>=0 ||
    q=="what is my name" ||
    q=="who am i" ||

    (
      elliLooksLikePersonalRecall(q)

      &&

      // Live clock/alarm/timer state belongs to KIRA's local
      // clock store, not autobiographical memory.
      //
      // Examples:
      //   what alarms do i have
      //   which alarms are set
      //   how many alarms do i have
      //   is my timer running
      !elliLooksLikeClockControl(q)
    )
  ){

    return UTT_PROFILE;
  }


  // ===================================================
  // 2) RECURRING REMINDER / ROUTINE
  // ===================================================
  //
  // Recurrence must beat the one-time TASK parser.
  //
  // Examples:
  //   remind me every day to drink water
  //   remind me to stretch daily
  //   remember to revise every day
  //   remind me every morning to check my bag
  //
  // The routine brain can then ask for an exact time if needed.
  // ===================================================

  bool recurringLanguage=

    q.indexOf(" daily")>=0 ||
    q.indexOf(" every day")>=0 ||
    q.indexOf(" everyday")>=0 ||
    q.indexOf(" every morning")>=0 ||
    q.indexOf(" every afternoon")>=0 ||
    q.indexOf(" every evening")>=0 ||
    q.indexOf(" every night")>=0;


  bool recurringReminder=

    q.startsWith("remind me ") ||
    q.startsWith("remember to ");


  if(
    recurringLanguage &&
    recurringReminder
  ){

    return UTT_ROUTINE;
  }


  // ===================================================
  // 3) TASKS
  // ===================================================

  if(
    q.startsWith("remind me to ") ||
    q.startsWith("remind me about ") ||
    q.startsWith("remember to ") ||
    q.startsWith("add task ") ||
    q.startsWith("add a task ") ||
    q.startsWith("create task ") ||
    q.startsWith("create a task ") ||
    (q.startsWith("add ") && (
      q.endsWith(" to tasks") || q.endsWith(" to my tasks") || elliHasNaturalTaskScheduleCue(q)
    )) ||
    q=="list tasks" ||
    q=="show tasks" ||
    q=="show completed tasks" ||
    q=="completed tasks" ||
    q=="task status" ||
    q.startsWith("complete task ") ||
    q.startsWith("mark task ") ||
    q.startsWith("finish task ") ||
    q.startsWith("reopen task ") ||
    q.startsWith("cancel task ") ||
    q.startsWith("delete task ") ||
    q.startsWith("remove task ")
  ){

    return UTT_TASK;
  }


  // ===================================================
  // 4) ROUTINES
  // ===================================================

  if(
    q.indexOf(" daily")>=0 ||
    q.indexOf(" every day")>=0 ||
    q.indexOf(" routine")>=0
  ){

    return UTT_ROUTINE;
  }


  // ===================================================
  // 4) SUPPORT / MOTIVATION
  // ===================================================

  if(
    q.indexOf("motivate")>=0 ||
    q.indexOf("inspire")>=0 ||
    q.indexOf("i am sad")>=0 ||
    q.indexOf("i am stressed")>=0 ||
    q.indexOf("i am anxious")>=0 ||
    q.indexOf("i am worried")>=0 ||
    q.indexOf("i am lonely")>=0
  ){

    return UTT_SUPPORT;
  }


  // ===================================================
  // 5) DEVICE COMMAND
  // ===================================================

  bool deviceWord=

    q.indexOf("fan")>=0 ||
    q.indexOf("light")>=0 ||
    q.indexOf("charger")>=0 ||
    q.indexOf("all devices")>=0;


  bool deviceCommandPrefix=

    q.startsWith("turn ") ||
    q.startsWith("switch ") ||
    q.startsWith("power ") ||
    q.startsWith("start ") ||
    q.startsWith("stop ") ||
    q.startsWith("enable ") ||
    q.startsWith("disable ") ||
    q.startsWith("activate ") ||
    q.startsWith("deactivate ") ||

    q.startsWith("please turn ") ||
    q.startsWith("please switch ") ||
    q.startsWith("please power ") ||

    q.startsWith("can you turn ") ||
    q.startsWith("can you switch ") ||
    q.startsWith("could you turn ") ||
    q.startsWith("could you switch ") ||
    q.startsWith("would you turn ") ||
    q.startsWith("would you switch ") ||
    q.startsWith("actually turn ") ||
    q.startsWith("actually switch ") ||
    q.startsWith("actually power ");


  bool deviceAction=

    deviceCommandPrefix &&

    (
      elliHasWordToken(q,"on") ||
      elliHasWordToken(q,"off") ||
      q.startsWith("start ") ||
      q.startsWith("stop ") ||
      q.startsWith("enable ") ||
      q.startsWith("disable ") ||
      q.startsWith("activate ") ||
      q.startsWith("deactivate ")
    );


  if(
    deviceWord &&
    deviceAction
  ){

    return UTT_DEVICE;
  }


  // ===================================================
  // 6) CLOCK / TIMER / ALARM
  // ===================================================

  if(
    elliLooksLikeClockControl(q)
  ){

    return UTT_CLOCK;
  }


  if(
    detectLocalTemporalRequest(q)
    != TEMPORAL_NONE
  ){

    return UTT_CLOCK;
  }


  // ===================================================
  // 7) SOCIAL CONVERSATION
  // ===================================================

  const char* socialPatterns[]={

    "how are you",
    "how are u",
    "how r you",
    "how r u",
    "how are you doing",
    "how you doing",
    "how is it going",
    "hows it going",
    "how are things",
    "how have you been",
    "what is up",
    "whats up",
    "wassup",
    "what are you up to",
    "are you there",
    "can you hear me",
    "who are you",
    "what are you",
    "what is your name",
    "whats your name",
    "tell me about yourself",
    "introduce yourself",
    "what can you do",
    "how can you help"
  };


  for(
    size_t i=0;
    i<
    sizeof(socialPatterns)/
    sizeof(socialPatterns[0]);
    i++
  ){

    if(
      q==socialPatterns[i] ||
      q.indexOf(socialPatterns[i])>=0
    ){

      return UTT_CHAT;
    }
  }


  if(
    q=="hi" ||
    q=="hii" ||
    q=="hello" ||
    q=="hey" ||
    q=="heyy" ||
    q=="yo"
  ){

    return UTT_CHAT;
  }


  // ===================================================
  // 8) QUESTION ALWAYS BEATS PERSONAL-STATEMENT GUESSING
  // ===================================================
  //
  // This is the critical arbitration rule.
  //
  // A sentence containing "I", "my", "study", "use", etc.
  // is NOT automatically personal memory when its grammar
  // clearly asks for advice, feasibility or knowledge.
  //
  // Examples:
  //
  //   what should i study after 10th
  //   could i use an esp32 for this
  //   should my project use dma
  // ===================================================

  if(
    startsLikeKnowledgeQuestion(q) ||
    elliLooksLikeBareKnowledgeTopic(q)
  ){

    return UTT_QUESTION;
  }


  // ===================================================
  // 9) STABLE PERSONAL FACT
  // ===================================================

  if(
    elliLooksLikePersonalFactStatement(
      q
    )
  ){

    return UTT_PROFILE;
  }


  // ===================================================
  // 10) ORDINARY STATEMENT
  // ===================================================

  return UTT_STATEMENT;
}


// =====================================================
//                    DEBUG OUTPUT
// =====================================================

void printUtteranceSense(String q){

  q=fuzzyNormalizeIntentWords(
    normalizeSpeechUtterance(q)
  );

  int intent=
    detectUtteranceIntent(q);

  Serial.println();

  Serial.println(
    "========== ELLI UTTERANCE SENSE =========="
  );

  Serial.print("Normalized : ");
  Serial.println(q);

  Serial.print("Intent     : ");

  Serial.println(
    utteranceIntentName(intent)
  );

  Serial.println(
    "==========================================="
  );
}