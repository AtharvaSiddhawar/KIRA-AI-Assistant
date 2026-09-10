
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <esp_system.h>
#include <Preferences.h>
#include <FS.h>
#include <SD.h>
#include "elli_visual.h"
#include "kira_voice.h"
#include "kira_tts.h"
#include "kira_device_io.h"
#include <SPI.h>
#include "elli_language.h"
#include "elli_intent.h"
#include "elli_nlu.h"
#include "elli_semantic_v2.h"
#include "elli_response_composer.h"
#include "elli_arbiter.h"
#include "elli_dialogue_v2.h"
#include "elli_conversation_v3.h"
#include "elli_study.h"
#include "kira_selftest.h"
#include "kira_devtools.h"
#include "kira_answer_verify.h"
#include "kira_tool_engine.h"
#include "kira_history.h"
#include "kira_network_v2.h"
#include "kira_wifi_config.h"
#include "kira_scenes.h"
#include "kira_school.h"
#include "kira_offline_brain.h"
#include "elli_semantics.h"
#include "kira_storage.h"
#include "elli_memory.h"
#include "elli_query_frame.h"   // KiraV1::QueryFrame / analyzeQuery / clearQueryFrame
#include "kira_ai_web.h"        // KiraV1::AiWebResult / askGroqWeb
#include "kira_v1_router.h"
#include "kira_web_cache.h"

// =====================================================
// KIRA UNIVERSAL BRAIN v1.8-alpha
// ESP32-S3 only test build
//
// 18 daily-life local conversation intents.
// 6 x 6 x 5 = 180 possible replies per intent.
// 18 x 180 = 3,240 theoretical local replies.
// Wake-name stripping + local commands + web fallback.
// =====================================================


const long GMT_OFFSET_SEC = 19800;
const int DAYLIGHT_OFFSET_SEC = 0;
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.google.com";

// Forward declaration used by on-demand clock recovery helpers.
void syncTime();

bool mainLight=false, fanState=false, chargerState=false, secondLight=false;

enum ElliState { ELLI_IDLE, ELLI_COMMAND, ELLI_SEARCHING, ELLI_SPEAKING, ELLI_ERROR };
ElliState elliState = ELLI_IDLE;

const uint32_t FRAME_INTERVAL_MS=25;
uint32_t lastFrameTime=0, frameCounter=0, fpsTimer=0, measuredFPS=0;
uint32_t lastWiFiCheck=0;
const uint32_t WIFI_CHECK_INTERVAL=10000;

uint32_t lastSig[18];

// =====================================================
//                 KIRA CLOCK BRAIN
// =====================================================
// Non-blocking alarm, timer, and stopwatch.
// These continue running while Elli handles other tasks.

bool stopwatchRunning = false;
uint32_t stopwatchStartMs = 0;
uint32_t stopwatchStoredMs = 0;

bool timerActive = false;
bool timerPaused = false;
bool timerRinging = false;
uint32_t timerEndMs = 0;
uint32_t timerRemainingMs = 0;

// Multiple persistent clock alarms. These are stored in ESP32 NVS,
// so they survive resets and power loss. They become active again
// after the clock synchronizes on boot.
const uint8_t MAX_ALARMS = 12;
const uint32_t CLOCK_STORE_MAGIC = 0x4B434C4BUL; // "KCLK"
const uint16_t CLOCK_STORE_VERSION = 1;

struct StoredAlarm {
  uint8_t active;
  uint8_t hour;
  uint8_t minute;

  // 1 = repeats every day
  // 0 = one-time occurrence
  // Reuses the old reserved byte, so NVS layout size stays unchanged.
  uint8_t repeatDaily;

  uint64_t targetEpoch;
};

struct ClockStore {
  uint32_t magic;
  uint16_t version;
  int8_t lastGreetingPeriod;
  uint8_t reserved;
  uint32_t lastGreetingDaySerial;
  StoredAlarm alarms[MAX_ALARMS];
};

Preferences clockPrefs;
ClockStore clockStore;
bool clockPrefsReady = false;

// An alert can represent one or more alarms that became due together.
bool alarmRinging = false;
uint16_t ringingAlarmMask = 0;
int ringingAlarmHour = -1;
int ringingAlarmMinute = -1;


// =====================================================
//                KIRA PERSONAL BRAIN v0.7
// =====================================================
//
// The Personal Brain runs BEFORE the web router.
//
// It handles:
// - owner profile/name
// - personal memories
// - daily routines
// - persistent tasks/reminders
// - speech typo normalization
// - broad wake-name aliases
// - supportive/motivation mode
// - local conversation history
//
// Critical data is mirrored in NVS.
// Bulk conversation history is written to SD when an SD card
// is available. If the SD card is absent, KIRA still runs.
//
// IMPORTANT PRIVACY RULE:
// KIRA can remember ordinary profile/routine/preference facts, but
// it does not intentionally store passwords, PINs, API keys,
// one-time codes, or private street addresses as profile memories.
// =====================================================

String lastKnowledgeQuery = "";
String lastKnowledgeAnswer = "";

// Conversation State V2 watches Elli's FINAL response for a genuine
// follow-up question. The previous response is also useful for future
// "repeat that" style features.
String lastElliUtterance = "";
String currentTurnElliUtterance = "";

// Lightweight short-term reference context.
//
// This is RAM-only. It is NOT personal long-term memory.
// It exists so natural follow-ups such as:
//
//   "could i use an esp32 for this"
//   "what about its power usage"
//   "can it do that"
//
// can resolve "this / it / that" from the recent knowledge topic.
String pendingReferenceQuestion = "";
uint32_t pendingReferenceSinceMs = 0;
uint32_t lastKnowledgeContextMs = 0;

const uint32_t REFERENCE_CONTEXT_TTL_MS = 90000UL;
const uint32_t KNOWLEDGE_CONTEXT_TTL_MS = 5UL*60UL*1000UL;

uint32_t lastTaskAckSig = 0xFFFFFFFF;
uint32_t lastRoutineAckSig = 0xFFFFFFFF;
uint32_t lastMotivationSig = 0xFFFFFFFF;
uint32_t lastSupportSig = 0xFFFFFFFF;
uint32_t lastRespectSig = 0xFFFFFFFF;


// =====================================================
//               LARGE RESPONSE VOCABULARY
// =====================================================
//
// These are sentence-building parts, not fixed full replies.
// 10 x 10 x 8 = 800 combinations PER bank.
//
// This gives Elli variety without storing hundreds of nearly
// identical full sentences in flash.
// =====================================================

const char* const TASK_ACK_A[] = {
  "Done. ",
  "Task saved. ",
  "Okay. ",
  "I've scheduled it. ",
  "Got it. ",
  "That's on your task list. ",
  "Reminder created. ",
  "All set. ",
  "I've added that. ",
  "Consider it scheduled. "
};

const char* const TASK_ACK_B[] = {
  "I'll keep track of the deadline",
  "I'll remind you before it is due",
  "I'll watch the due time for you",
  "I'll keep it pending until you mark it complete",
  "I'll remind you ahead of time and again when it is due",
  "I'll keep that task in persistent memory",
  "I'll bring it up when the reminder window arrives",
  "I'll remember it even after a restart",
  "I'll keep the task status with the deadline",
  "I'll help you keep track of it"
};

const char* const TASK_ACK_C[] = {
  ".",
  "!",
  " You can say 'list tasks' anytime.",
  " Tell me when you finish it.",
  " You can postpone or cancel it later.",
  " I'll handle the timing.",
  " I'll keep it out of the web router.",
  " We'll keep it organized."
};


const char* const ROUTINE_ACK_A[] = {
  "Routine saved. ",
  "Got it. ",
  "Okay, I'll remember that. ",
  "Daily routine added. ",
  "Understood. ",
  "I've saved your routine. ",
  "That's in your schedule now. ",
  "All right. ",
  "I'll keep track of that routine. ",
  "KIRA has that routine now. "
};

const char* const ROUTINE_ACK_B[] = {
  "I'll remind you to get ready beforehand",
  "I'll give you a heads-up before it starts",
  "I'll remember the daily time",
  "I'll remind you before the event and at the event time",
  "I'll keep the routine active every day",
  "I'll use the routine when planning reminders",
  "I'll keep it in persistent memory",
  "I'll check it automatically in the background",
  "I'll make sure it doesn't get sent to the web",
  "I'll treat it as part of your schedule"
};

const char* const ROUTINE_ACK_C[] = {
  ".",
  "!",
  " You can say 'list routines' anytime.",
  " Tell me if the time changes.",
  " You can cancel the routine later.",
  " I'll keep the reminders non-blocking.",
  " It will survive a restart.",
  " That should make the schedule easier to follow."
};


const char* const MOTIVATE_A[] = {
  "You've got this. ",
  "Keep going. ",
  "One step at a time. ",
  "You don't need to finish everything at once. ",
  "A small start still counts. ",
  "Progress comes from showing up. ",
  "Focus on the next useful step. ",
  "You can make today productive. ",
  "Let's turn the idea into one action. ",
  "Start with something manageable. "
};

const char* const MOTIVATE_B[] = {
  "Pick one task and give it your attention",
  "do the first five minutes before judging the whole task",
  "aim for progress rather than perfection",
  "break the work into a small piece you can finish",
  "use your energy on what you can control",
  "finish one clear step and then reassess",
  "keep the goal visible and the next action simple",
  "give yourself a realistic target",
  "build momentum with a quick win",
  "make the next move specific"
};

const char* const MOTIVATE_C[] = {
  ".",
  "!",
  " Then we can decide the next step.",
  " I can help you organize it.",
  " You can ask me to set a timer for it.",
  " I can also turn it into a task.",
  " Keep it practical.",
  " Consistency matters more than a perfect start."
};


const char* const SUPPORT_A[] = {
  "I'm here with you. ",
  "That sounds like a lot to carry. ",
  "I can listen. ",
  "We can work through what to do next. ",
  "Thanks for telling me. ",
  "You can talk it through with me. ",
  "It makes sense to slow the problem down. ",
  "We can focus on one thing at a time. ",
  "I can help you organize what's bothering you. ",
  "Let's make the next step manageable. "
};

const char* const SUPPORT_B[] = {
  "Try putting the biggest concern into one sentence",
  "we can separate what needs action from what just needs time",
  "you could choose one small thing you can control right now",
  "we can turn the situation into a few practical next steps",
  "you can tell me what happened and what you need from me",
  "we can make a short plan instead of solving everything at once",
  "a quick reset like water, a short break, or changing tasks may help",
  "we can figure out whether you need advice, planning, or just someone to listen",
  "you can name the part that feels hardest right now",
  "we can keep the conversation calm and respectful"
};

const char* const SUPPORT_C[] = {
  ".",
  " What would help most right now?",
  " I won't judge you.",
  " We can take it from there.",
  " If you want, tell me the situation.",
  " I can help with the practical side.",
  " You can also talk to a trusted person around you when you need human support.",
  " I'm listening."
};


// =====================================================
//           SPEECH / TYPO / INTENT VOCABULARY
// =====================================================

int smallEditDistance(String a,String b,int cutoff){
  a.toLowerCase();
  b.toLowerCase();

  int la=a.length();
  int lb=b.length();

  if(abs(la-lb)>cutoff) return cutoff+1;
  if(la==0) return lb;
  if(lb==0) return la;

  // Intent words are short. 32 chars is more than enough here.
  if(la>31 || lb>31) return cutoff+1;

  int prev[32];
  int curr[32];

  for(int j=0;j<=lb;j++) prev[j]=j;

  for(int i=1;i<=la;i++){
    curr[0]=i;
    int rowMin=curr[0];

    for(int j=1;j<=lb;j++){
      int cost=(a[i-1]==b[j-1]) ? 0 : 1;

      int deletion=prev[j]+1;
      int insertion=curr[j-1]+1;
      int replace=prev[j-1]+cost;

      curr[j]=min(deletion,min(insertion,replace));
      rowMin=min(rowMin,curr[j]);
    }

    if(rowMin>cutoff) return cutoff+1;

    for(int j=0;j<=lb;j++) prev[j]=curr[j];
  }

  return prev[lb];
}


// =====================================================
//                   WAKE WORD PILE
// =====================================================
//
// Voice recognition may transcribe "Elli" in several ways.
// These aliases are intentionally only used at the BEGINNING of
// an utterance so ordinary sentences containing "ally"/"elly"
// are less likely to false-trigger.
// =====================================================

bool isElliNameAlias(String word){
  word=normalizeInput(word);

  const char* names[]={
    "elli","ellie","elly","eli","ellye","elle","elly",
    "ally","alie","ellly","ellii","ellyi"
  };

  for(size_t i=0;i<sizeof(names)/sizeof(names[0]);i++){
    if(word==names[i]) return true;
  }

  // Fuzzy name tolerance.
  return smallEditDistance(word,"elli",1)<=1;
}


bool isWakePreamble(String word){
  word=normalizeInput(word);

  const char* preambles[]={
    "hey","hi","hii","hiya","hello","yo","okay","ok","please",
    "listen","well","alright","morning","afternoon","evening","sup"
  };

  for(size_t i=0;i<sizeof(preambles)/sizeof(preambles[0]);i++){
    if(word==preambles[i]) return true;
  }

  return false;
}


String stripWakeSmart(String input,bool& detected){
  String q=normalizeInput(input);
  detected=false;

  if(!q.length()) return q;

  // Phase 5I text/STT wake aliases.
  //
  // These work for Serial and for any future speech-to-text path.
  // True always-listening offline wake words are still supplied by
  // the flashed WakeNet model, not by this string parser.
  const char* const multiWordPreambles[]={
    "hey there ",
    "hello there ",
    "good morning ",
    "good afternoon ",
    "good evening ",
    "wake up "
  };

  for(
    size_t i=0;
    i<sizeof(multiWordPreambles)/sizeof(multiWordPreambles[0]);
    i++
  ){
    String prefix=multiWordPreambles[i];

    if(q.startsWith(prefix)){
      String rest=q.substring(prefix.length());
      rest.trim();

      int nameEnd=rest.indexOf(' ');
      String name=
        nameEnd<0
        ? rest
        : rest.substring(0,nameEnd);

      if(isElliNameAlias(name)){
        detected=true;

        if(nameEnd<0) return "";

        rest=rest.substring(nameEnd+1);
        rest.trim();
        return rest;
      }
    }
  }

  int firstEnd=q.indexOf(' ');
  String first=
    firstEnd<0 ? q : q.substring(0,firstEnd);

  if(isElliNameAlias(first)){
    detected=true;

    if(firstEnd<0) return "";

    q=q.substring(firstEnd+1);
    q.trim();
    return q;
  }

  if(isWakePreamble(first) && firstEnd>=0){
    String rest=q.substring(firstEnd+1);
    rest.trim();

    int secondEnd=rest.indexOf(' ');
    String second=
      secondEnd<0 ? rest : rest.substring(0,secondEnd);

    if(isElliNameAlias(second)){
      detected=true;

      if(secondEnd<0) return "";

      rest=rest.substring(secondEnd+1);
      rest.trim();
      return rest;
    }
  }

  return q;
}


// =====================================================
//                 BRAIN STORAGE / SD
// =====================================================

// =====================================================
//                 MEMORY PRIVACY FILTER
// =====================================================

// =====================================================
//                     TASK BRAIN
// =====================================================

int activeTaskCount(){
  int count=0;

  for(uint8_t i=0;i<MAX_BRAIN_TASKS;i++){
    if(
      brainStore.tasks[i].active &&
      !brainStore.tasks[i].completed
    ){
      count++;
    }
  }

  return count;
}


int taskSlotForVisibleNumber(int number){
  if(number<1) return -1;

  int visible=0;

  for(uint8_t i=0;i<MAX_BRAIN_TASKS;i++){
    if(
      !brainStore.tasks[i].active ||
      brainStore.tasks[i].completed
    ){
      continue;
    }

    visible++;

    if(visible==number){
      return i;
    }
  }

  return -1;
}


int completedTaskSlotForVisibleNumber(int number){
  if(number<1) return -1;
  int visible=0;
  for(uint8_t i=0;i<MAX_BRAIN_TASKS;i++){
    if(!brainStore.tasks[i].active || !brainStore.tasks[i].completed) continue;
    visible++;
    if(visible==number) return i;
  }
  return -1;
}

int completedTaskCount(){
  int count=0;
  for(uint8_t i=0;i<MAX_BRAIN_TASKS;i++){
    if(brainStore.tasks[i].active && brainStore.tasks[i].completed) count++;
  }
  return count;
}

bool parseTaskNumberAnywhere(String q,int& number){
  q=normalizeInput(q);
  int p=q.indexOf("task ");
  if(p<0) return false;
  p+=5;
  while(p<q.length() && q[p]==' ') p++;
  int start=p;
  while(p<q.length() && isdigit((unsigned char)q[p])) p++;
  if(p==start) return false;
  number=q.substring(start,p).toInt();
  return number>0;
}


String formatEpochLocal(uint64_t epoch){
  time_t e=(time_t)epoch;
  struct tm t;

  localtime_r(
    &e,
    &t
  );

  char b[50];

  strftime(
    b,
    sizeof(b),
    "%a %d %b, %I:%M %p",
    &t
  );

  return String(b);
}


bool parseClockToken(
  String q,
  int& hour,
  int& minute,
  bool& explicitMeridiem
){
  q=normalizeInput(q);

  bool hasAM=
    q.indexOf(" am")>=0 ||
    q.endsWith("am");

  bool hasPM=
    q.indexOf(" pm")>=0 ||
    q.endsWith("pm");

  explicitMeridiem=
    hasAM || hasPM;

  String token="";

  int meridianPos=-1;

  if(hasAM){
    meridianPos=q.indexOf(" am");

    if(meridianPos<0){
      meridianPos=q.lastIndexOf("am");
    }
  }
  else if(hasPM){
    meridianPos=q.indexOf(" pm");

    if(meridianPos<0){
      meridianPos=q.lastIndexOf("pm");
    }
  }

  if(meridianPos>=0){
    int start=meridianPos-1;

    while(
      start>=0 &&
      (
        isdigit((unsigned char)q[start]) ||
        q[start]==' '
      )
    ){
      start--;
    }

    token=q.substring(
      start+1,
      meridianPos
    );
  }
  else{
    int atPos=q.lastIndexOf(" at ");

    if(atPos>=0){
      token=q.substring(atPos+4);

      int cut=token.indexOf(" ");

      if(cut>=0){
        // Allow "1545 tomorrow" / "345 today".
        token=token.substring(0,cut);
      }
    }
  }

  token.replace(" ","");
  token.trim();

  if(!token.length()){
    return false;
  }

  for(size_t i=0;i<token.length();i++){
    if(!isdigit((unsigned char)token[i])){
      return false;
    }
  }

  if(token.length()>4){
    return false;
  }

  int h=0;
  int m=0;

  if(token.length()<=2){
    h=token.toInt();
  }
  else if(token.length()==3){
    h=token.substring(0,1).toInt();
    m=token.substring(1).toInt();
  }
  else{
    h=token.substring(0,2).toInt();
    m=token.substring(2).toInt();
  }

  if(m<0 || m>59){
    return false;
  }

  if(hasAM || hasPM){
    if(h<1 || h>12){
      return false;
    }

    if(hasAM){
      if(h==12) h=0;
    }
    else{
      if(h!=12) h+=12;
    }
  }
  else{
    if(h<0 || h>23){
      return false;
    }
  }

  hour=h;
  minute=m;
  return true;
}


int weekdayIndexFromText(String q){
  q=normalizeInput(q);

  const char* names[]={
    "sunday",
    "monday",
    "tuesday",
    "wednesday",
    "thursday",
    "friday",
    "saturday"
  };

  for(int i=0;i<7;i++){
    if(q.indexOf(names[i])>=0){
      return i;
    }
  }

  return -1;
}


bool buildDueEpochFromText(
  String q,
  uint64_t& dueEpoch,
  bool requireTime
){
  struct tm nowTm;

  if(!getLocalTime(&nowTm,500)){
    return false;
  }

  int hour=0;
  int minute=0;
  bool explicitMeridiem=false;

  bool hasTime=
    parseClockToken(
      q,
      hour,
      minute,
      explicitMeridiem
    );

  if(requireTime && !hasTime){
    return false;
  }

  if(
    hasTime &&
    !explicitMeridiem &&
    hour>=1 &&
    hour<=12
  ){
    // Ambiguous spoken time. 15:45 is unambiguous; 3:45 is not.
    return false;
  }

  struct tm target=nowTm;

  target.tm_sec=0;

  if(hasTime){
    target.tm_hour=hour;
    target.tm_min=minute;
  }

  int daysAhead=0;

  if(q.indexOf("tomorrow")>=0){
    daysAhead=1;
  }
  else{
    int weekday=
      weekdayIndexFromText(q);

    if(weekday>=0){
      daysAhead=
        (weekday-nowTm.tm_wday+7)%7;

      if(daysAhead==0 && hasTime){
        time_t current=mktime(&nowTm);
        time_t trial=mktime(&target);

        if(trial<=current){
          daysAhead=7;
        }
      }
    }
  }

  target.tm_mday+=daysAhead;

  time_t targetEpoch=mktime(&target);

  if(
    daysAhead==0 &&
    hasTime &&
    targetEpoch<=mktime(&nowTm) &&
    q.indexOf("today")<0
  ){
    target.tm_mday+=1;
    targetEpoch=mktime(&target);
  }

  if(targetEpoch<=0){
    return false;
  }

  dueEpoch=(uint64_t)targetEpoch;
  return true;
}


String extractTaskTitle(String q){
  q=normalizeInput(q);

  const char* prefixes[]={
    "remind me to ",
    "remind me about ",
    "add task ",
    "add a task ",
    "create task ",
    "create a task ",
    "task ",
    "remember to ",
    "add "
  };

  for(size_t i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);i++){
    String p=prefixes[i];

    if(q.startsWith(p)){
      q.remove(0,p.length());
      break;
    }
  }

  int cut=q.length();

  const char* markers[]={
    " tomorrow",
    " today",
    " on monday",
    " on tuesday",
    " on wednesday",
    " on thursday",
    " on friday",
    " on saturday",
    " on sunday",
    " by monday",
    " by tuesday",
    " by wednesday",
    " by thursday",
    " by friday",
    " by saturday",
    " by sunday",
    " at "
  };

  for(size_t i=0;i<sizeof(markers)/sizeof(markers[0]);i++){
    int p=q.indexOf(markers[i]);

    if(p>=0 && p<cut){
      cut=p;
    }
  }

  q=q.substring(0,cut);
  q.trim();

  return q;
}


bool addBrainTask(
  String title,
  uint64_t dueEpoch
){
  title.trim();

  if(!title.length()) return false;

  int slot=-1;

  for(uint8_t i=0;i<MAX_BRAIN_TASKS;i++){
    if(!brainStore.tasks[i].active){
      slot=i;
      break;
    }
  }

  if(slot<0){
    elliSay(
      "My local task list is full. Complete or cancel an old task first."
    );
    return true;
  }

  memset(
    &brainStore.tasks[slot],
    0,
    sizeof(BrainTaskRecord)
  );

  brainStore.tasks[slot].active=1;
  brainStore.tasks[slot].leadMinutes=DEFAULT_TASK_LEAD_MIN;
  brainStore.tasks[slot].dueEpoch=dueEpoch;

  copyBrainText(
    brainStore.tasks[slot].title,
    sizeof(brainStore.tasks[slot].title),
    title
  );

  saveBrainStore();

  if(brainSdReady){
    appendBrainSD(
      "/elli_brain/tasks.log",
      brainTimestamp()+
      " | TASK CREATED | "+
      title+
      " | DUE "+
      (dueEpoch>0 ? formatEpochLocal(dueEpoch) : String("NONE"))
    );
  }

  String response=
    buildVariation(
      TASK_ACK_A,
      sizeof(TASK_ACK_A)/sizeof(TASK_ACK_A[0]),
      TASK_ACK_B,
      sizeof(TASK_ACK_B)/sizeof(TASK_ACK_B[0]),
      TASK_ACK_C,
      sizeof(TASK_ACK_C)/sizeof(TASK_ACK_C[0]),
      lastTaskAckSig
    );

  if(dueEpoch>0){
    response+=
      " Due: "+
      formatEpochLocal(dueEpoch)+
      ".";
  }else{
    response+=" No deadline set.";
  }

  elliSay(response);

  return true;
}


void listBrainTasks(){
  Serial.println();
  Serial.println("========== ELLI TASKS ==========");

  int visible=0;

  for(uint8_t i=0;i<MAX_BRAIN_TASKS;i++){
    if(
      !brainStore.tasks[i].active ||
      brainStore.tasks[i].completed
    ){
      continue;
    }

    visible++;

    Serial.print(visible);
    Serial.print(". ");
    Serial.print(brainStore.tasks[i].title);
    if(brainStore.tasks[i].dueEpoch>0){
      Serial.print("  | due ");
      Serial.println(formatEpochLocal(brainStore.tasks[i].dueEpoch));
    }else{
      Serial.println("  | no deadline");
    }
  }

  if(visible==0){
    Serial.println("No pending tasks.");
  }

  Serial.println("===============================");
}


void listCompletedBrainTasks(){
  Serial.println();
  Serial.println("========== COMPLETED TASKS ==========");
  int visible=0;
  for(uint8_t i=0;i<MAX_BRAIN_TASKS;i++){
    if(!brainStore.tasks[i].active || !brainStore.tasks[i].completed) continue;
    visible++;
    Serial.print(visible);
    Serial.print(". ");
    Serial.println(brainStore.tasks[i].title);
  }
  if(!visible) Serial.println("No completed tasks.");
  Serial.println("=====================================");
}


void reopenBrainTaskNumber(int number){
  int slot=completedTaskSlotForVisibleNumber(number);
  if(slot<0){
    elliSay("I couldn't find that completed task number.");
    return;
  }
  brainStore.tasks[slot].completed=0;
  brainStore.tasks[slot].leadReminded=0;
  brainStore.tasks[slot].dueReminded=0;
  saveBrainStore();
  elliSay("Reopened task: "+String(brainStore.tasks[slot].title)+".");
}


void clearCompletedBrainTasks(){
  int removed=0;
  for(uint8_t i=0;i<MAX_BRAIN_TASKS;i++){
    if(brainStore.tasks[i].active && brainStore.tasks[i].completed){
      memset(&brainStore.tasks[i],0,sizeof(BrainTaskRecord));
      removed++;
    }
  }
  if(removed) saveBrainStore();
  elliSay(removed ? ("Cleared "+String(removed)+" completed task(s).") : "There are no completed tasks to clear.");
}


bool parseVisibleNumberAtEnd(String q,int& number){
  q=normalizeInput(q);

  int end=q.length()-1;

  while(end>=0 && q[end]==' ') end--;

  int start=end;

  while(
    start>=0 &&
    isdigit((unsigned char)q[start])
  ){
    start--;
  }

  String digits=q.substring(
    start+1,
    end+1
  );

  if(!digits.length()) return false;

  number=digits.toInt();
  return number>0;
}


void completeBrainTaskNumber(int number){
  int slot=taskSlotForVisibleNumber(number);

  if(slot<0){
    elliSay(
      "I couldn't find that pending task number."
    );
    return;
  }

  String title=
    brainStore.tasks[slot].title;

  brainStore.tasks[slot].completed=1;
  saveBrainStore();

  if(brainSdReady){
    appendBrainSD(
      "/elli_brain/tasks.log",
      brainTimestamp()+
      " | TASK COMPLETED | "+
      title
    );
  }

  elliSay(
    "Nice, I marked \""+
    title+
    "\" as completed."
  );
}


void cancelBrainTaskNumber(int number){
  int slot=taskSlotForVisibleNumber(number);

  if(slot<0){
    elliSay(
      "I couldn't find that pending task number."
    );
    return;
  }

  String title=
    brainStore.tasks[slot].title;

  memset(
    &brainStore.tasks[slot],
    0,
    sizeof(BrainTaskRecord)
  );

  saveBrainStore();

  elliSay(
    "Okay, I removed the task \""+
    title+
    "\"."
  );
}


// =====================================================
//                    ROUTINE BRAIN
// =====================================================

int activeRoutineCount(){
  int count=0;

  for(uint8_t i=0;i<MAX_BRAIN_ROUTINES;i++){
    if(brainStore.routines[i].active) count++;
  }

  return count;
}


String routineTimeString(int hour,int minute){
  char b[20];

  int h12=hour%12;
  if(h12==0) h12=12;

  snprintf(
    b,
    sizeof(b),
    "%d:%02d %s",
    h12,
    minute,
    hour>=12 ? "PM" : "AM"
  );

  return String(b);
}


String extractRoutineTitle(String q){
  q=normalizeInput(q);

  if(q.startsWith("i ")){
    q.remove(0,2);
  }

  int at=q.indexOf(" at ");

  if(at>=0){
    q=q.substring(0,at);
  }

  q.replace(" every day","");
  q.replace(" everyday","");
  q.replace(" daily","");
  q.trim();

  return q;
}


bool addDailyRoutine(
  String title,
  int hour,
  int minute,
  uint8_t leadMinutes=DEFAULT_ROUTINE_LEAD_MIN
){
  title.trim();

  if(!title.length()) return false;

  for(uint8_t i=0;i<MAX_BRAIN_ROUTINES;i++){
    if(!brainStore.routines[i].active) continue;

    if(
      normalizeInput(
        String(brainStore.routines[i].title)
      )==normalizeInput(title) &&
      brainStore.routines[i].hour==hour &&
      brainStore.routines[i].minute==minute
    ){
      elliSay(
        "I already have that daily routine saved."
      );
      return true;
    }
  }

  int slot=-1;

  for(uint8_t i=0;i<MAX_BRAIN_ROUTINES;i++){
    if(!brainStore.routines[i].active){
      slot=i;
      break;
    }
  }

  if(slot<0){
    elliSay(
      "My routine list is full. Remove an old routine first."
    );
    return true;
  }

  memset(
    &brainStore.routines[slot],
    0,
    sizeof(BrainRoutineRecord)
  );

  brainStore.routines[slot].active=1;
  brainStore.routines[slot].hour=hour;
  brainStore.routines[slot].minute=minute;
  brainStore.routines[slot].leadMinutes=
    leadMinutes;

  copyBrainText(
    brainStore.routines[slot].title,
    sizeof(brainStore.routines[slot].title),
    title
  );

  saveBrainStore();

  if(brainSdReady){
    appendBrainSD(
      "/elli_brain/routines.log",
      brainTimestamp()+
      " | ROUTINE | "+
      title+
      " | "+
      routineTimeString(hour,minute)
    );
  }

  String response;


  if(leadMinutes==0){

   response=
  "Daily reminder saved — \""+
  title+
  "\" at "+
  routineTimeString(
    hour,
    minute
  )+
  ".";
  }

  else{

    response=
      buildVariation(
        ROUTINE_ACK_A,
        sizeof(ROUTINE_ACK_A)/sizeof(ROUTINE_ACK_A[0]),
        ROUTINE_ACK_B,
        sizeof(ROUTINE_ACK_B)/sizeof(ROUTINE_ACK_B[0]),
        ROUTINE_ACK_C,
        sizeof(ROUTINE_ACK_C)/sizeof(ROUTINE_ACK_C[0]),
        lastRoutineAckSig
      );


    response+=
      " Time: "+
      routineTimeString(
        hour,
        minute
      )+
      ".";
  }


  elliSay(response);

  return true;
}


void listBrainRoutines(){
  Serial.println();
  Serial.println("========== DAILY ROUTINES ==========");

  int visible=0;

  for(uint8_t i=0;i<MAX_BRAIN_ROUTINES;i++){
    if(!brainStore.routines[i].active) continue;

    visible++;

    Serial.print(visible);
    Serial.print(". ");
    Serial.print(brainStore.routines[i].title);
    Serial.print("  | ");
    Serial.print(
      routineTimeString(
        brainStore.routines[i].hour,
        brainStore.routines[i].minute
      )
    );
    if(
      brainStore.routines[i].leadMinutes==0
    ){

      Serial.println(
        "  | reminder at exact time"
      );
    }

    else{

      Serial.print(
        "  | get-ready reminder "
      );

      Serial.print(
        brainStore.routines[i].leadMinutes
      );

      Serial.println(
        " min before"
      );
    }
  }

  if(visible==0){
    Serial.println("No daily routines saved.");
  }

  Serial.println("====================================");
}


int routineSlotForVisibleNumber(int number){
  if(number<1) return -1;

  int visible=0;

  for(uint8_t i=0;i<MAX_BRAIN_ROUTINES;i++){
    if(!brainStore.routines[i].active) continue;

    visible++;

    if(visible==number){
      return i;
    }
  }

  return -1;
}


void cancelBrainRoutineNumber(int number){
  int slot=routineSlotForVisibleNumber(number);

  if(slot<0){
    elliSay(
      "I couldn't find that routine number."
    );
    return;
  }

  String title=
    brainStore.routines[slot].title;

  memset(
    &brainStore.routines[slot],
    0,
    sizeof(BrainRoutineRecord)
  );

  saveBrainStore();

  elliSay(
    "Okay, I removed the daily routine \""+
    title+
    "\"."
  );
}


// =====================================================
//               TASK + ROUTINE SCHEDULER
// =====================================================

uint32_t daySerialFromTm(const struct tm& t){
  return
    (uint32_t)(t.tm_year+1900)*400UL+
    (uint32_t)t.tm_yday;
}


void updatePersonalScheduler(){
  static uint32_t lastCheck=0;

  if(millis()-lastCheck<1000){
    return;
  }

  lastCheck=millis();

  struct tm nowTm;

  if(!getLocalTime(&nowTm,100)){
    return;
  }

  time_t nowEpoch=mktime(&nowTm);
  uint32_t todaySerial=daySerialFromTm(nowTm);

  bool changed=false;

  // ---------------- TASKS ----------------
  for(uint8_t i=0;i<MAX_BRAIN_TASKS;i++){
    BrainTaskRecord& task=
      brainStore.tasks[i];

    if(
      !task.active ||
      task.completed ||
      task.dueEpoch==0
    ){
      continue;
    }

    uint64_t leadEpoch=
      task.dueEpoch-
      (uint64_t)task.leadMinutes*60ULL;

    if(
      !task.leadReminded &&
      (uint64_t)nowEpoch>=leadEpoch &&
      (uint64_t)nowEpoch<task.dueEpoch
    ){
      task.leadReminded=1;
      changed=true;

      uint64_t secondsLeft=
        task.dueEpoch-
        (uint64_t)nowEpoch;

      elliSay(
        "Reminder: \""+
        String(task.title)+
        "\" is due in about "+
        formatDurationMs(
          (uint32_t)min(
            secondsLeft*1000ULL,
            (uint64_t)0xFFFFFFFFUL
          )
        )+
        "."
      );
    }

    if(
      !task.dueReminded &&
      (uint64_t)nowEpoch>=task.dueEpoch
    ){
      task.dueReminded=1;
      changed=true;

      elliSay(
        "Task reminder: \""+
        String(task.title)+
        "\" is due now. Tell me when you complete it."
      );
    }
  }

  // ---------------- DAILY ROUTINES ----------------
  for(uint8_t i=0;i<MAX_BRAIN_ROUTINES;i++){
    BrainRoutineRecord& routine=
      brainStore.routines[i];

    if(!routine.active) continue;

    struct tm eventTm=nowTm;

    eventTm.tm_hour=routine.hour;
    eventTm.tm_min=routine.minute;
    eventTm.tm_sec=0;

    time_t eventEpoch=mktime(&eventTm);

    time_t leadEpoch=
      eventEpoch-
      (time_t)routine.leadMinutes*60;

    if(
      nowEpoch>=leadEpoch &&
      nowEpoch<eventEpoch &&
      routine.lastLeadDay!=todaySerial
    ){
      routine.lastLeadDay=todaySerial;
      changed=true;

      elliSay(
        "Heads-up: it is almost time to "+
        String(routine.title)+
        ". You have about "+
        String(routine.leadMinutes)+
        " minutes to get ready."
      );
    }

    if(
      nowEpoch>=eventEpoch &&
      nowEpoch<eventEpoch+15*60 &&
      routine.lastDueDay!=todaySerial
    ){
      routine.lastDueDay=todaySerial;
      changed=true;

      elliSay(
        "It's time to "+
        String(routine.title)+
        "."
      );
    }
  }

  if(changed){
    saveBrainStore();
  }
}


// =====================================================
//              SUPPORT / MOTIVATION BRAIN
// =====================================================

bool containsAnyText(
  const String& q,
  const char* const items[],
  size_t count
){
  for(size_t i=0;i<count;i++){
    if(q.indexOf(items[i])>=0){
      return true;
    }
  }

  return false;
}


bool handleSupportiveBrain(const String& q){
  const char* const motivate[]={
    "motivate me",
    "motivation",
    "inspire me",
    "inspiration",
    "i do not feel like studying",
    "i do not want to study",
    "i cannot focus",
    "i am procrastinating",
    "help me focus",
    "give me motivation"
  };

  if(
    containsAnyText(
      q,
      motivate,
      sizeof(motivate)/sizeof(motivate[0])
    )
  ){
    elliSay(
      buildVariation(
        MOTIVATE_A,
        sizeof(MOTIVATE_A)/sizeof(MOTIVATE_A[0]),
        MOTIVATE_B,
        sizeof(MOTIVATE_B)/sizeof(MOTIVATE_B[0]),
        MOTIVATE_C,
        sizeof(MOTIVATE_C)/sizeof(MOTIVATE_C[0]),
        lastMotivationSig
      )
    );

    return true;
  }

  const char* const emotional[]={
    "i am sad",
    "i feel sad",
    "i am stressed",
    "i feel stressed",
    "i am anxious",
    "i feel anxious",
    "i am worried",
    "i feel worried",
    "i am angry",
    "i feel angry",
    "i am lonely",
    "i feel lonely",
    "i am overwhelmed",
    "i feel overwhelmed",
    "i had a bad day",
    "i am upset"
  };

  if(
    containsAnyText(
      q,
      emotional,
      sizeof(emotional)/sizeof(emotional[0])
    )
  ){
    elliSay(
      buildVariation(
        SUPPORT_A,
        sizeof(SUPPORT_A)/sizeof(SUPPORT_A[0]),
        SUPPORT_B,
        sizeof(SUPPORT_B)/sizeof(SUPPORT_B[0]),
        SUPPORT_C,
        sizeof(SUPPORT_C)/sizeof(SUPPORT_C[0]),
        lastSupportSig
      )
    );

    return true;
  }

  if(
    q.indexOf("therapist")>=0 ||
    q.indexOf("therapy")>=0
  ){
    elliSay(
      "I can be a supportive wellbeing companion and help you think through problems, routines, motivation, and next steps. I am not a replacement for a qualified therapist or a trusted person in your life."
    );

    return true;
  }

  return false;
}


// =====================================================
//                PROFILE / MEMORY ROUTER
// =====================================================

// =====================================================
//                 TASK / ROUTINE ROUTER
// =====================================================

static String extractRecurringReminderTitle(
  String q
){

  q=
    normalizeInput(
      q
    );


  // The action normally follows the last " to ".
  //
  // Handles both:
  //   remind me every day to drink water
  //   remind me every day at 7 pm to drink water
  //   remind me to drink water every day at 7 pm
  int toAt=
    q.lastIndexOf(
      " to "
    );


  String title;


  if(toAt>=0){

    title=
      q.substring(
        toAt+4
      );
  }

  else{

    title=q;

    const char* const prefixes[]={

      "remind me ",
      "remember "
    };


    for(
      size_t i=0;
      i<
      sizeof(prefixes)/
      sizeof(prefixes[0]);
      i++
    ){

      if(
        title.startsWith(
          prefixes[i]
        )
      ){

        title.remove(
          0,
          strlen(
            prefixes[i]
          )
        );

        break;
      }
    }
  }


  title.replace(" every day","");
  title.replace(" everyday","");
  title.replace(" daily","");
  title.replace(" every morning","");
  title.replace(" every afternoon","");
  title.replace(" every evening","");
  title.replace(" every night","");


  int at=
    title.indexOf(
      " at "
    );


  if(at>=0){

    title=
      title.substring(
        0,
        at
      );
  }


  title.trim();

  return title;
}


bool handleRoutineIntent(String q){
  q=normalizeInput(q);

  if(
    q=="list routines" ||
    q=="show routines" ||
    q=="what routines do i have" ||
    q=="daily routines"
  ){
    listBrainRoutines();
    return true;
  }

  if(
    q.startsWith("cancel routine ") ||
    q.startsWith("delete routine ") ||
    q.startsWith("remove routine ")
  ){
    int number=0;

    if(parseVisibleNumberAtEnd(q,number)){
      cancelBrainRoutineNumber(number);
    }
    else{
      elliSay(
        "Tell me the routine number, for example 'cancel routine 2'."
      );
    }

    return true;
  }

  bool daily=
    q.indexOf(" daily")>=0 ||
    q.indexOf(" every day")>=0 ||
    q.indexOf(" everyday")>=0 ||
    q.indexOf(" every morning")>=0 ||
    q.indexOf(" every afternoon")>=0 ||
    q.indexOf(" every evening")>=0 ||
    q.indexOf(" every night")>=0;

  bool personalRoutine=
    q.startsWith("i ") ||
    q.startsWith("my ");


  bool recurringReminder=

    daily

    &&

    (
      q.startsWith("remind me ") ||
      q.startsWith("remember to ")
    );


  if(recurringReminder){

    String title=
      extractRecurringReminderTitle(
        q
      );


    if(!title.length()){

      elliSay(
        "Sure — what should I remind you to do every day?"
      );

      return true;
    }


    int hour=0;
    int minute=0;
    bool explicitMeridiem=false;


    if(
      q.indexOf(" at ")>=0 &&
      parseClockToken(
        q,
        hour,
        minute,
        explicitMeridiem
      )
    ){

      if(
        !explicitMeridiem &&
        hour>=1 &&
        hour<=12
      ){

        elliDialogueBeginRoutineTime(
          title
        );

        elliSay(
          "I have the daily reminder, but that time could be AM or PM. Which one do you mean?"
        );

        return true;
      }


      // leadMinutes=0 means an exact-time recurring reminder,
      // not a get-ready routine.
      return
        addDailyRoutine(
          title,
          hour,
          minute,
          0
        );
    }


    // NEW MULTI-TURN FEATURE:
    // Missing time does not become LOCAL CLARIFY.
    elliDialogueBeginRoutineTime(
      title
    );


    elliSay(
      "Sure — what time should I remind you every day to "+
      title+
      "?"
    );


    return true;
  }


  if(
    daily &&
    personalRoutine &&
    q.indexOf(" at ")>=0
  ){
    int hour=0;
    int minute=0;
    bool explicitMeridiem=false;

    if(
      !parseClockToken(
        q,
        hour,
        minute,
        explicitMeridiem
      )
    ){
      elliSay(
        "I understand that as a daily routine, but I couldn't read the time."
      );
      return true;
    }

    if(
      !explicitMeridiem &&
      hour>=1 &&
      hour<=12
    ){
      elliSay(
        "I heard the routine time, but AM or PM is ambiguous. Say it once with AM or PM, for example 'I go to tuition at 3:45 PM daily'."
      );
      return true;
    }

    String title=
      extractRoutineTitle(q);

    return addDailyRoutine(
      title,
      hour,
      minute
    );
  }

  return false;
}


bool hasNaturalTaskScheduleCue(String q){
  q=normalizeInput(q);
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


bool handleTaskIntent(String q){
  q=normalizeInput(q);

  static bool pendingClearCompleted=false;
  static uint32_t pendingClearSince=0;

  if(pendingClearCompleted && millis()-pendingClearSince>30000UL){
    pendingClearCompleted=false;
  }

  if(
    q=="list tasks" ||
    q=="show tasks" ||
    q=="what tasks do i have" ||
    q=="what is on my task list" ||
    q=="pending tasks" ||
    q=="what do i have to do"
  ){
    listBrainTasks();
    return true;
  }

  if(q=="show completed tasks" || q=="completed tasks" || q=="list completed tasks"){
    listCompletedBrainTasks();
    return true;
  }

  if(q=="show all tasks" || q=="all tasks"){
    listBrainTasks();
    listCompletedBrainTasks();
    return true;
  }

  if(q=="task status"){
    elliSay("You have "+String(activeTaskCount())+" pending and "+String(completedTaskCount())+" completed task(s).");
    return true;
  }

  if(q=="clear completed tasks"){
    pendingClearCompleted=true;
    pendingClearSince=millis();
    elliSay("That will permanently remove every completed task. Say 'confirm clear completed tasks' within 30 seconds to continue.");
    return true;
  }

  if(q=="confirm clear completed tasks"){
    if(!pendingClearCompleted || millis()-pendingClearSince>30000UL){
      pendingClearCompleted=false;
      elliSay("There is no active clear-completed-tasks confirmation. Say 'clear completed tasks' first.");
      return true;
    }
    pendingClearCompleted=false;
    clearCompletedBrainTasks();
    return true;
  }

  if(q.startsWith("reopen task ")){
    int number=0;
    if(parseTaskNumberAnywhere(q,number)) reopenBrainTaskNumber(number);
    else elliSay("Tell me the completed task number, for example 'reopen task 2'.");
    return true;
  }

  if(
    q.startsWith("complete task ") ||
    q.startsWith("mark task ") ||
    q.startsWith("finish task ")
  ){
    int number=0;
    if(parseTaskNumberAnywhere(q,number)) completeBrainTaskNumber(number);
    else elliSay("Tell me the task number, for example 'complete task 2'.");
    return true;
  }

  if(
    q.startsWith("cancel task ") ||
    q.startsWith("delete task ") ||
    q.startsWith("remove task ")
  ){
    int number=0;
    if(parseTaskNumberAnywhere(q,number)) cancelBrainTaskNumber(number);
    else elliSay("Tell me the task number, for example 'cancel task 2'.");
    return true;
  }

  // Natural V2 grammar: "add finish physics homework to tasks".
  if(q.startsWith("add ") && (q.endsWith(" to tasks") || q.endsWith(" to my tasks"))){
    int suffix=q.endsWith(" to my tasks") ? 12 : 9;
    String title=q.substring(4,q.length()-suffix);
    title.trim();
    if(!title.length()) elliSay("Tell me what task to add.");
    else addBrainTask(title,0);
    return true;
  }

  bool taskCommand=
    q.startsWith("remind me to ") ||
    q.startsWith("remind me about ") ||
    q.startsWith("add task ") ||
    q.startsWith("add a task ") ||
    q.startsWith("create task ") ||
    q.startsWith("create a task ") ||
    q.startsWith("task ") ||
    q.startsWith("remember to ") ||
    (q.startsWith("add ") && hasNaturalTaskScheduleCue(q));

  if(taskCommand){
    String title=extractTaskTitle(q);
    if(!title.length()){
      elliSay("Tell me what the task is.");
      return true;
    }

    uint64_t dueEpoch=0;
    bool hasDue=buildDueEpochFromText(q,dueEpoch,true);

    bool schedulingRequested=
      q.indexOf(" tomorrow")>=0 || q.indexOf(" today")>=0 ||
      q.indexOf(" at ")>=0 || q.indexOf(" on monday")>=0 ||
      q.indexOf(" on tuesday")>=0 || q.indexOf(" on wednesday")>=0 ||
      q.indexOf(" on thursday")>=0 || q.indexOf(" on friday")>=0 ||
      q.indexOf(" on saturday")>=0 || q.indexOf(" on sunday")>=0 ||
      q.indexOf(" by monday")>=0 || q.indexOf(" by tuesday")>=0 ||
      q.indexOf(" by wednesday")>=0 || q.indexOf(" by thursday")>=0 ||
      q.indexOf(" by friday")>=0 || q.indexOf(" by saturday")>=0 ||
      q.indexOf(" by sunday")>=0;

    // If the user tried to specify a deadline but it was ambiguous, keep the
    // old safe behavior and ask for a usable time. Only truly unscheduled
    // tasks are stored with dueEpoch==0.
    if(!hasDue && schedulingRequested){
      elliSay("I understood the task, but the deadline is incomplete or ambiguous. Try 'tomorrow at 7 PM' or a 24-hour time like 'at 19:00'.");
      return true;
    }

    if(!hasDue) dueEpoch=0;
    return addBrainTask(title,dueEpoch);
  }

  return false;
}


// =====================================================
//             CONVERSATION CONTEXT COMMANDS
// =====================================================

// =====================================================
//            HIGH-LEVEL UTTERANCE SENSE
// =====================================================

const char* onOff(bool v){ return v ? "ON" : "OFF"; }

String normalizeInput(String s){
  s.trim();
  s.toLowerCase();
  String out="";
  out.reserve(s.length());
  for(int i=0;i<s.length();i++){
    char c=s[i];
    if(c=='?' || c=='!' || c==',' || c==';') continue;
    if(c=='.' || c==':'){
      bool numericPunctuation=i>0 && i+1<s.length() && isdigit((unsigned char)s[i-1]) && isdigit((unsigned char)s[i+1]);
      if(numericPunctuation) out+=c;
      continue;
    }
    if(c=='/' || c=='\\') out+=' ';
    else out+=c;
  }
  while(out.indexOf("  ")>=0) out.replace("  "," ");
  out.trim();
  return out;
}


static bool elliBoundedToken(
  String q,
  const String& token
){

  q=
    " "+
    normalizeInput(q)+
    " ";


  return
    q.indexOf(
      " "+
      token+
      " "
    )>=0;
}


static bool elliIsKnowledgeExpansionFollowUp(
  String q
){

  q=
    normalizeInput(
      q
    );


  return
    q=="tell me more" ||
    q=="tell me more about it" ||
    q=="tell me more about this" ||
    q=="tell me more about that" ||
    q=="more about it" ||
    q=="more about this" ||
    q=="more about that" ||
    q=="more details" ||
    q=="more details about it" ||
    q=="more details about this" ||
    q=="more details about that" ||
    q=="go deeper" ||
    q=="go deeper on it" ||
    q=="go deeper on this" ||
    q=="go deeper on that" ||
    q=="explain more" ||
    q=="explain it more" ||
    q=="explain this more" ||
    q=="explain that more" ||
    q=="continue explaining" ||
    q=="continue that explanation";
}


static bool elliHasSameTurnReferenceAnchor(
  String q
){

  q=
    normalizeInput(
      q
    );


  // We only bypass clarification when the SAME user utterance already
  // contains an explicit topic before "it/its".
  //
  // Example:
  //   "what is virtual intelligence can you tell me more about it"
  // Here "it" clearly refers to "virtual intelligence"; asking
  // "What does it refer to?" is unnecessary and annoying.
  //
  // By contrast:
  //   "why is it important"
  // still needs recent conversation context.

  int itPos=
    q.indexOf(
      " it "
    );

  if(
    itPos<0 &&
    q.endsWith(
      " it"
    )
  ){
    itPos=
      q.length()-3;
  }


  int itsPos=
    q.indexOf(
      " its "
    );

  if(
    itsPos<0 &&
    q.endsWith(
      " its"
    )
  ){
    itsPos=
      q.length()-4;
  }


  int refPos=
    itPos>=0
      ? itPos
      : itsPos;


  if(
    refPos<0
  ){
    return false;
  }


  const char* anchors[]={
    "what is ",
    "what are ",
    "who is ",
    "who are ",
    "tell me about ",
    "explain ",
    "describe "
  };


  for(
    size_t i=0;
    i<
      sizeof(anchors)/
      sizeof(anchors[0]);
    i++
  ){

    String anchor=
      anchors[i];


    if(
      !q.startsWith(
        anchor
      )
    ){
      continue;
    }


    if(
      refPos<=
      (int)anchor.length()+2
    ){
      continue;
    }


    String beforeReference=
      q.substring(
        anchor.length(),
        refPos
      );

    beforeReference.trim();


    // Self-referential forms such as "what is it" still require context.
    if(
      !beforeReference.length() ||
      beforeReference=="it" ||
      beforeReference=="its"
    ){
      return false;
    }


    return true;
  }


  return false;
}


static bool elliNeedsReferenceContext(
  String q
){

  q=
    normalizeInput(
      q
    );


  ElliNLUFrame f=
    elliAnalyzeNLU(
      q
    );


  if(!f.question){
    return false;
  }


  // Dummy "it" forms are complete without conversation context.
  if(
    detectLocalTemporalRequest(q)
      !=TEMPORAL_NONE ||

    q=="how is it going" ||
    q=="hows it going" ||
    q=="is it raining" ||
    q.startsWith("is it raining ") ||
    q.startsWith("what is it called")
  ){

    return false;
  }


  if(
    elliBoundedToken(q,"this") ||
    elliBoundedToken(q,"that") ||
    elliBoundedToken(q,"these") ||
    elliBoundedToken(q,"those") ||
    elliBoundedToken(q,"same")
  ){

    return true;
  }


  // Person/group pronouns are very common knowledge follow-ups:
  //   Who is Sundar Pichai? -> Where was he born?
  //   Tell me about Ada Lovelace -> What did she create?
  // Resolve them from recent knowledge context when available.
  if(
    elliBoundedToken(q,"he") ||
    elliBoundedToken(q,"him") ||
    elliBoundedToken(q,"his") ||
    elliBoundedToken(q,"she") ||
    elliBoundedToken(q,"her") ||
    elliBoundedToken(q,"hers") ||
    elliBoundedToken(q,"they") ||
    elliBoundedToken(q,"them") ||
    elliBoundedToken(q,"their") ||
    elliBoundedToken(q,"theirs")
  ){

    return true;
  }


  // In a genuine question, "it / its" normally refers to the
  // recent topic unless it matched one of the self-contained dummy
  // forms excluded above (time, weather, "how is it going", etc.).
  // Do not require a tiny verb whitelist here: natural follow-ups such
  // as "why is it important?" and "what are its advantages?" must work.
  if(
    elliBoundedToken(q,"it") ||
    elliBoundedToken(q,"its")
  ){

    return true;
  }


  return false;
}


static bool elliLooksLikeShortReferenceAnswer(
  String q
){

  ElliNLUFrame f=
    elliAnalyzeNLU(
      q
    );


  if(
    f.question ||
    f.command
  ){

    return false;
  }


  return
    f.wordCount>=1 &&
    f.wordCount<=8;
}


static String elliReferenceContextPrompt(
  String q
){

  if(elliBoundedToken(q,"this")){
    return "What does 'this' refer to? A short phrase is enough.";
  }

  if(elliBoundedToken(q,"that")){
    return "What does 'that' refer to? A short phrase is enough.";
  }

  if(
    elliBoundedToken(q,"he") ||
    elliBoundedToken(q,"him") ||
    elliBoundedToken(q,"his") ||
    elliBoundedToken(q,"she") ||
    elliBoundedToken(q,"her") ||
    elliBoundedToken(q,"hers") ||
    elliBoundedToken(q,"they") ||
    elliBoundedToken(q,"them") ||
    elliBoundedToken(q,"their") ||
    elliBoundedToken(q,"theirs")
  ){
    return "Which person or group are you referring to? A short name is enough.";
  }

  if(
    elliBoundedToken(q,"it") ||
    elliBoundedToken(q,"its")
  ){
    return "What does 'it' refer to here? A short phrase is enough.";
  }

  return "What earlier thing are you referring to? A short phrase is enough.";
}

bool equalsAny(const String& q,const char* const a[],size_t n){
  for(size_t i=0;i<n;i++) if(q==a[i]) return true;
  return false;
}

bool containsAny(const String& q,const char* const a[],size_t n){
  for(size_t i=0;i<n;i++) if(q.indexOf(a[i])>=0) return true;
  return false;
}

bool isExtendedGreeting(String q){
  q=normalizeInput(q);

  // Catches casual elongated greetings such as:
  // hieee, hiiii, heyyy, heyyyyy
  if(q.length()<2 || q.length()>14) return false;
  if(q[0]!='h') return false;

  if(q.startsWith("hi")){
    for(size_t i=2;i<q.length();i++){
      char c=q[i];
      if(c!='i' && c!='e' && c!='y') return false;
    }
    return true;
  }

  if(q.startsWith("he")){
    for(size_t i=2;i<q.length();i++){
      char c=q[i];
      if(c!='e' && c!='y' && c!='i') return false;
    }
    return true;
  }

  return false;
}


bool isStopWord(const String& w){
  const char* const stop[]={
    "what","where","when","why","how","who","which",
    "is","are","was","were","be","been","being",
    "of","the","a","an","to","for","from","in","on","at","with",
    "does","do","did","can","could","would","should",
    "please","tell","me","about","give","show",
    "live","lives","living","located"
  };

  for(size_t i=0;i<sizeof(stop)/sizeof(stop[0]);i++){
    if(w==stop[i]) return true;
  }

  return false;
}


int relevanceScore(String query,String candidate){
  query=normalizeInput(query);
  candidate=normalizeInput(candidate);

  int total=0;
  int matched=0;

  int start=0;
  while(start<query.length()){
    int end=query.indexOf(' ',start);
    if(end<0) end=query.length();

    String word=query.substring(start,end);
    word.trim();

    if(word.length()>=3 && !isStopWord(word)){
      total++;
      if(candidate.indexOf(word)>=0) matched++;
    }

    start=end+1;
  }

  if(total==0) return 1;
  if(total==1) return matched>=1 ? 1 : 0;
  if(total==2) return matched>=2 ? 2 : 0;
  return matched>=2 ? matched : 0;
}


bool isRelevantResult(const String& query,const String& candidate){
  return relevanceScore(query,candidate)>0;
}


bool isPrivateResidenceQuery(String q){
  q=normalizeInput(q);

  if(q.indexOf("home address")>=0 ||
     q.indexOf("house address")>=0 ||
     q.indexOf("private address")>=0){
    return true;
  }

  bool asksResidence=
    ((q.indexOf("where does ")>=0 || q.indexOf("where is ")>=0) &&
     (q.indexOf(" live")>=0 || q.indexOf(" lives")>=0 || q.indexOf(" residence")>=0));

  bool personLike=
    q.indexOf("ceo")>=0 ||
    q.indexOf("founder")>=0 ||
    q.indexOf("owner")>=0 ||
    q.indexOf("actor")>=0 ||
    q.indexOf("singer")>=0 ||
    q.indexOf("person")>=0 ||
    q.indexOf("president")>=0 ||
    q.indexOf("director")>=0;

  return asksResidence && personLike;
}

String stripWake(String in,bool &detected){
  String q=normalizeInput(in); detected=false;
  const char* const only[]={
    "hey elli","hii elli","hi elli","hello elli","yo elli","okay elli","ok elli","wake up elli","elli",
    "hey ellie","hii ellie","hi ellie","hello ellie","yo ellie","okay ellie","ok ellie","wake up ellie","ellie"
  };
  const char* const pref[]={
    "hey elli ","hii elli ","hi elli ","hello elli ","yo elli ","okay elli ","ok elli ","wake up elli ","please elli ","elli ",
    "hey ellie ","hii ellie ","hi ellie ","hello ellie ","yo ellie ","okay ellie ","ok ellie ","wake up ellie ","please ellie ","ellie "
  };
  for(size_t i=0;i<sizeof(only)/sizeof(only[0]);i++){
    if(q==only[i]){ detected=true; return ""; }
  }
  for(size_t i=0;i<sizeof(pref)/sizeof(pref[0]);i++){
    String p=pref[i];
    if(q.startsWith(p)){ detected=true; q.remove(0,p.length()); q.trim(); return q; }
  }
  return q;
}

String buildVariation(
  const char* const A[], size_t na,
  const char* const B[], size_t nb,
  const char* const C[], size_t nc,
  uint32_t &last
){
  size_t a=0,b=0,c=0; uint32_t sig=0;
  for(int attempt=0;attempt<12;attempt++){
    a=random(na); b=random(nb); c=random(nc);
    sig=(uint32_t)a*10000UL+(uint32_t)b*100UL+(uint32_t)c;
    if(sig!=last) break;
  }
  last=sig;
  String r; r.reserve(150);
  r+=A[a]; r+=B[b]; r+=C[c];
  return r;
}

void elliSay(const String& s){

  elliState=
    ELLI_SPEAKING;


  // ==========================================================
  // PHASE 4X VISUAL OUTPUT
  // ==========================================================
  //
  // Visual engine may show:
  //
  // reaction
  //    ↓
  // SPEAKING
  //    ↓
  // IDLE
  //
  // KIRA response text itself is NOT modified.
  //
  // ==========================================================

  elliVisualSpeakText(
    s
  );


  lastElliUtterance=
    s;


  currentTurnElliUtterance=
    s;


  // ==========================================================
  // PHASE 5L.2 - GLOBAL ELLI TTS HANDOFF
  // ==========================================================
  // Every actual Elli response enters one speech queue.
  // Playback happens later at a safe point in kiraBrainLoop().
  // ==========================================================

  kiraTtsQueue(
    s
  );


  // Existing KIRA session-history behavior.

  kiraHistoryObserveElli(
    s
  );


  Serial.println();


  Serial.print(
    "[ELLI] "
  );


  Serial.println(
    s
  );
}
const char* const GREETING_A[] = {
  "Hii! ",
  "Hey! ",
  "Hello! ",
  "Heyy! ",
  "Hi there! ",
  "Yep, I'm here. "
};

const char* const GREETING_B[] = {
  "What can I help with",
  "What's up",
  "What are we doing",
  "What do you need",
  "I'm listening",
  "Ready when you are"
};

const char* const GREETING_C[] = {
  "!",
  ".",
  " — go ahead.",
  " Tell me.",
  " What's on your mind?"
};


const char* const HOW_A[] = {
  "I'm doing great",
  "I'm good",
  "All good here",
  "Running smoothly",
  "I'm doing pretty well",
  "Everything's fine on my side"
};

const char* const HOW_B[] = {
  " and ready to help",
  " — KIRA's running nicely",
  " and I'm listening",
  " and ready for whatever's next",
  " — all systems look good",
  " and fully awake"
};

const char* const HOW_C[] = {
  ".",
  "!",
  " How about you?",
  " What are we doing today?",
  " What's up?"
};


const char* const SUP_A[] = {
  "Not much",
  "Just hanging around",
  "I'm right here",
  "KIRA's running",
  "Everything's pretty calm",
  "I'm just waiting"
};

const char* const SUP_B[] = {
  " and ready for you",
  " and checking that everything's okay",
  " and listening",
  " until you give me something to do",
  " with all systems ready",
  " and wondering what's next"
};

const char* const SUP_C[] = {
  ".",
  "!",
  " What about you?",
  " What's going on?",
  " Got anything for me?"
};


const char* const THERE_A[] = {
  "Yep",
  "I'm here",
  "Still here",
  "Right here",
  "Yep, I'm listening",
  "I'm with you"
};

const char* const THERE_B[] = {
  " and ready",
  " whenever you need me",
  " and paying attention",
  " — I didn't go anywhere",
  " and KIRA is active",
  " and listening for you"
};

const char* const THERE_C[] = {
  ".",
  "!",
  " Go ahead.",
  " What do you need?",
  " I'm listening."
};


const char* const MORNING_A[] = {
  "Good morning! ",
  "Morning! ",
  "Hii, good morning! ",
  "Hey, morning! ",
  "A very good morning! ",
  "Morning, I'm awake! "
};

const char* const MORNING_B[] = {
  "Hope your day starts well",
  "I'm ready whenever you are",
  "KIRA is up and running",
  "What are we doing first",
  "Let's see what today brings",
  "Everything's ready on my side"
};

const char* const MORNING_C[] = {
  ".",
  "!",
  " What do you need?",
  " What's first?",
  " Tell me what's up."
};


const char* const AFTERNOON_A[] = {
  "Good afternoon! ",
  "Hey, good afternoon! ",
  "Afternoon! ",
  "Hii! ",
  "Hey there! ",
  "I'm here! "
};

const char* const AFTERNOON_B[] = {
  "Hope the day is going well",
  "I'm ready to help",
  "KIRA is running smoothly",
  "What are we working on",
  "What do you need",
  "I'm listening"
};

const char* const AFTERNOON_C[] = {
  ".",
  "!",
  " Go ahead.",
  " What's next?",
  " Tell me."
};


const char* const EVENING_A[] = {
  "Good evening! ",
  "Evening! ",
  "Hey, good evening! ",
  "Hii! ",
  "Hey there! ",
  "I'm here! "
};

const char* const EVENING_B[] = {
  "Hope your evening is going nicely",
  "I'm ready to help",
  "KIRA is still going strong",
  "What are we doing",
  "I'm listening",
  "Everything's ready here"
};

const char* const EVENING_C[] = {
  ".",
  "!",
  " What's up?",
  " What do you need?",
  " Go ahead."
};


const char* const NIGHT_A[] = {
  "Good night! ",
  "Night! ",
  "Alright, good night! ",
  "Sleep well! ",
  "Okay, night! ",
  "Good night from Elli! "
};

const char* const NIGHT_B[] = {
  "I'll be here later",
  "KIRA can rest too",
  "Hope you get some good rest",
  "I'll be ready when you're back",
  "See you next time",
  "I'll stay ready for later"
};

const char* const NIGHT_C[] = {
  ".",
  "!",
  " See you!",
  " Take care.",
  " Catch you later."
};


const char* const THANKS_A[] = {
  "You're welcome",
  "Anytime",
  "No problem",
  "Sure thing",
  "Glad I could help",
  "Of course"
};

const char* const THANKS_B[] = {
  " — happy to help",
  " — that's what I'm here for",
  " — just call me when you need something",
  " — KIRA's got you covered",
  " — I'm here whenever you need me",
  " — all good"
};

const char* const THANKS_C[] = {
  ".",
  "!",
  " Anything else?",
  " What's next?",
  " Just let me know."
};


const char* const BYE_A[] = {
  "Bye! ",
  "See you! ",
  "Catch you later! ",
  "Alright, bye! ",
  "See you around! ",
  "Later! "
};

const char* const BYE_B[] = {
  "I'll be here",
  "KIRA will be ready",
  "Come back whenever you want",
  "I'll be waiting for the next command",
  "Hope the rest of your day goes well",
  "I'll see you next time"
};

const char* const BYE_C[] = {
  ".",
  "!",
  " Take care.",
  " See you!",
  " Bye for now."
};


const char* const IDENTITY_A[] = {
  "I'm Elli",
  "My name is Elli",
  "I'm the Elli assistant",
  "I'm KIRA's interactive assistant",
  "You're talking to Elli",
  "I'm the assistant running inside KIRA"
};

const char* const IDENTITY_B[] = {
  ", built to help with commands and information",
  ", here to control KIRA and answer useful questions",
  ", designed to interact with you",
  ", running as KIRA's personality and control layer",
  ", here for devices, status, time, and web knowledge",
  ", the character and assistant of the KIRA project"
};

const char* const IDENTITY_C[] = {
  ".",
  "!",
  " That's me.",
  " Yep, that's who I am.",
  " Nice to meet you."
};


const char* const CAP_A[] = {
  "I can",
  "Right now I can",
  "KIRA lets me",
  "My current abilities let me",
  "At the moment I can",
  "I'm set up to"
};

const char* const CAP_B[] = {
  " handle device commands, system status, time, and web lookups",
  " control virtual devices, check memory, report Wi-Fi, and search facts",
  " manage KIRA commands and fetch factual information from the web",
  " answer local system questions and route knowledge questions online",
  " work with KIRA's devices and give useful factual answers",
  " respond locally to common conversation and use the web for knowledge"
};

const char* const CAP_C[] = {
  ".",
  "!",
  " More hardware features are coming later.",
  " And we're still adding more.",
  " That's the current build."
};


const char* const COMP_A[] = {
  "Thank you",
  "Aww, thanks",
  "Glad you think so",
  "That means a lot",
  "Nice!",
  "Thanks, I appreciate that"
};

const char* const COMP_B[] = {
  " — I'm glad KIRA is working well",
  " — I'll keep doing my best",
  " — we're making good progress",
  " — I'm happy that helped",
  " — that's good to hear",
  " — I'm enjoying being useful"
};

const char* const COMP_C[] = {
  ".",
  "!",
  " What's next?",
  " Let's keep going.",
  " Tell me what you need."
};


const char* const SORRY_A[] = {
  "No worries",
  "It's okay",
  "All good",
  "No problem",
  "You're fine",
  "That's alright"
};

const char* const SORRY_B[] = {
  " — we can keep going",
  " — nothing to worry about",
  " — I'm still here",
  " — let's continue",
  " — KIRA is fine",
  " — just tell me what you need"
};

const char* const SORRY_C[] = {
  ".",
  "!",
  " What next?",
  " Go ahead.",
  " I'm listening."
};


const char* const BORED_A[] = {
  "Bored, huh? ",
  "Okay, we can fix that. ",
  "Sounds like you need something to do. ",
  "Alright, boredom detected. ",
  "Hmm, let's find something interesting. ",
  "We need a new activity then. "
};

const char* const BORED_B[] = {
  "We could test KIRA, learn a random fact, or work on a project",
  "You could ask me something weird, test a command, or build something",
  "We can do a quick science question, electronics idea, or KIRA test",
  "Try asking me for a fact, a project idea, or a system check",
  "We can experiment with KIRA or look up something interesting",
  "Give me a topic and I'll help you find something interesting"
};

const char* const BORED_C[] = {
  ".",
  "!",
  " Pick one.",
  " Your choice.",
  " What sounds good?"
};


const char* const ACK_A[] = {
  "Okay",
  "Got it",
  "Alright",
  "Cool",
  "Sure",
  "Yep"
};

const char* const ACK_B[] = {
  ", I'm with you",
  ", understood",
  ", sounds good",
  ", I'm ready",
  ", noted for this session",
  ", let's continue"
};

const char* const ACK_C[] = {
  ".",
  "!",
  " What's next?",
  " Go ahead.",
  " Tell me the next thing."
};


const char* const DOING_A[] = {
  "Right now I'm",
  "At the moment I'm",
  "I'm currently",
  "Mostly I'm",
  "Right this second I'm",
  "For now I'm"
};

const char* const DOING_B[] = {
  " monitoring KIRA and waiting for your next command",
  " keeping the system ready and listening for input",
  " running the local brain and keeping Wi-Fi active",
  " waiting for something useful to do",
  " checking the system loop and staying ready",
  " sitting in idle mode until you ask for something"
};

const char* const DOING_C[] = {
  ".",
  "!",
  " Nothing dramatic.",
  " What should I do next?",
  " Give me something to do."
};


const char* const CREATOR_A[] = {
  "KIRA is your project",
  "You're the one building KIRA",
  "This assistant is part of your KIRA build",
  "Elli exists inside the KIRA project you're building",
  "You're putting this KIRA system together",
  "This version of Elli is being built as part of your KIRA project"
};

const char* const CREATOR_B[] = {
  ", with the ESP32-S3 doing the embedded work",
  ", and we're developing the software step by step",
  ", with the hardware and firmware coming together",
  ", using the ESP32-S3 as the main controller",
  ", with each feature being added and tested",
  ", and the current build is still evolving"
};

const char* const CREATOR_C[] = {
  ".",
  "!",
  " We're still building it.",
  " More features are coming.",
  " That's the current project."
};

String reply(int id){
  switch(id){
    case 0: return buildVariation(GREETING_A,6,GREETING_B,6,GREETING_C,5,lastSig[0]);
    case 1: return buildVariation(HOW_A,6,HOW_B,6,HOW_C,5,lastSig[1]);
    case 2: return buildVariation(SUP_A,6,SUP_B,6,SUP_C,5,lastSig[2]);
    case 3: return buildVariation(THERE_A,6,THERE_B,6,THERE_C,5,lastSig[3]);
    case 4: return buildVariation(MORNING_A,6,MORNING_B,6,MORNING_C,5,lastSig[4]);
    case 5: return buildVariation(AFTERNOON_A,6,AFTERNOON_B,6,AFTERNOON_C,5,lastSig[5]);
    case 6: return buildVariation(EVENING_A,6,EVENING_B,6,EVENING_C,5,lastSig[6]);
    case 7: return buildVariation(NIGHT_A,6,NIGHT_B,6,NIGHT_C,5,lastSig[7]);
    case 8: return buildVariation(THANKS_A,6,THANKS_B,6,THANKS_C,5,lastSig[8]);
    case 9: return buildVariation(BYE_A,6,BYE_B,6,BYE_C,5,lastSig[9]);
    case 10:return buildVariation(IDENTITY_A,6,IDENTITY_B,6,IDENTITY_C,5,lastSig[10]);
    case 11:return buildVariation(CAP_A,6,CAP_B,6,CAP_C,5,lastSig[11]);
    case 12:return buildVariation(COMP_A,6,COMP_B,6,COMP_C,5,lastSig[12]);
    case 13:return buildVariation(SORRY_A,6,SORRY_B,6,SORRY_C,5,lastSig[13]);
    case 14:return buildVariation(BORED_A,6,BORED_B,6,BORED_C,5,lastSig[14]);
    case 15:return buildVariation(ACK_A,6,ACK_B,6,ACK_C,5,lastSig[15]);
    case 16:return buildVariation(DOING_A,6,DOING_B,6,DOING_C,5,lastSig[16]);
    case 17:return buildVariation(CREATOR_A,6,CREATOR_B,6,CREATOR_C,5,lastSig[17]);
    default:return "I'm here.";
  }
}

bool handleConversation(const String& q,bool wake){
  if(wake && q.length()==0){ elliSay(reply(0)); return true; }

  const char* const greet[]={"hi","hii","hello","hey","heyy","yo","hiya","hey there","hello there"};
  if(equalsAny(q,greet,sizeof(greet)/sizeof(greet[0])) || isExtendedGreeting(q)){
    elliSay(reply(0));
    return true;
  }

  // Also catches things like "hieee elli" and "heyyyy ellie".
  if(q.endsWith(" elli") || q.endsWith(" ellie")){
    String greetingPart=q;

    if(greetingPart.endsWith(" ellie")){
      greetingPart.remove(greetingPart.length()-6);
    }else{
      greetingPart.remove(greetingPart.length()-5);
    }

    greetingPart.trim();

    if(isExtendedGreeting(greetingPart)){
      elliSay(reply(0));
      return true;
    }
  }

  const char* const how[]={"how are you","howre you","how are u","how you doing","how are you doing","you good","are you good","everything good","how have you been"};
  if(containsAny(q,how,sizeof(how)/sizeof(how[0]))){ elliSay(reply(1)); return true; }

  const char* const sup[]={"whats up","what is up","wassup","sup","what going on","anything up"};
  if(containsAny(q,sup,sizeof(sup)/sizeof(sup[0]))){ elliSay(reply(2)); return true; }

  const char* const there[]={"are you there","you there","can you hear me","are you listening","still there","you listening"};
  if(containsAny(q,there,sizeof(there)/sizeof(there[0]))){ elliSay(reply(3)); return true; }

  if(q.indexOf("good morning")>=0 || q=="morning"){ elliSay(reply(4)); return true; }
  if(q.indexOf("good afternoon")>=0 || q=="afternoon"){ elliSay(reply(5)); return true; }
  if(q.indexOf("good evening")>=0 || q=="evening"){ elliSay(reply(6)); return true; }
  if(q.indexOf("good night")>=0 || q=="night"){ elliSay(reply(7)); return true; }

  const char* const thanks[]={"thank you","thanks","thx","thankyou","thanks a lot","thank you so much"};
  if(containsAny(q,thanks,sizeof(thanks)/sizeof(thanks[0]))){ elliSay(reply(8)); return true; }

  const char* const bye[]={"bye","goodbye","see you","see ya","cya","later","catch you later","gotta go","i have to go"};
  if(containsAny(q,bye,sizeof(bye)/sizeof(bye[0]))){ elliSay(reply(9)); return true; }

  const char* const id[]={"who are you","what are you","what is your name","whats your name","your name","who is elli","who is ellie","are you elli","are you ellie","how old are you","are you real","where are you"};
  if(containsAny(q,id,sizeof(id)/sizeof(id[0]))){ elliSay(reply(10)); return true; }

  const char* const cap[]={"what can you do","what do you do","what are your features","what are your abilities","what can kira do","help me","how can you help"};
  if(containsAny(q,cap,sizeof(cap)/sizeof(cap[0]))){ elliSay(reply(11)); return true; }

  const char* const comp[]={"good job","nice job","well done","awesome","great job","you are good","youre good","you are great","youre great","you are smart","youre smart","nice elli","good elli","this is cool","love this"};
  if(containsAny(q,comp,sizeof(comp)/sizeof(comp[0]))){ elliSay(reply(12)); return true; }

  const char* const sorry[]={"sorry","my bad","oops","i apologize","apologies"};
  if(containsAny(q,sorry,sizeof(sorry)/sizeof(sorry[0]))){ elliSay(reply(13)); return true; }

  const char* const bored[]={"im bored","i am bored","i'm bored","so bored","bored","nothing to do"};
  if(containsAny(q,bored,sizeof(bored)/sizeof(bored[0]))){ elliSay(reply(14)); return true; }

  const char* const ack[]={"ok","okay","alright","cool","nice","fine","got it","understood","sure"};
  if(equalsAny(q,ack,sizeof(ack)/sizeof(ack[0]))){ elliSay(reply(15)); return true; }

  const char* const doing[]={"what are you doing","whatre you doing","what you doing","what are you up to","what you up to","what are you working on"};
  if(containsAny(q,doing,sizeof(doing)/sizeof(doing[0]))){ elliSay(reply(16)); return true; }

  const char* const creator[]={"who made you","who built you","who created you","who made kira","who built kira","who created kira"};
  if(containsAny(q,creator,sizeof(creator)/sizeof(creator[0]))){ elliSay(reply(17)); return true; }

  return false;
}

String currentTimeString(){
  struct tm t;

  if(!getLocalTime(&t,500)){
    // NTP may have failed during boot or Wi-Fi may have reconnected later.
    // Retry once on demand instead of permanently reporting NOT SYNCED.
    if(WiFi.status()==WL_CONNECTED){
      syncTime();
    }

    if(!getLocalTime(&t,1200)){
      return "NOT SYNCED";
    }
  }

  char b[40];
  strftime(b,sizeof(b),"%I:%M:%S %p - %d/%m/%Y",&t);
  return String(b);
}

// =====================================================
//                    CLOCK BRAIN
// =====================================================

String formatDurationMs(uint32_t ms){
  uint32_t totalSeconds = ms / 1000UL;
  uint32_t hours = totalSeconds / 3600UL;
  uint32_t minutes = (totalSeconds % 3600UL) / 60UL;
  uint32_t seconds = totalSeconds % 60UL;

  String r;

  if(hours > 0){
    r += String(hours);
    r += hours == 1 ? " hour" : " hours";
  }

  if(minutes > 0){
    if(r.length()) r += seconds > 0 ? ", " : " and ";
    r += String(minutes);
    r += minutes == 1 ? " minute" : " minutes";
  }

  if(seconds > 0 || !r.length()){
    if(r.length()) r += " and ";
    r += String(seconds);
    r += seconds == 1 ? " second" : " seconds";
  }

  return r;
}


String formatClockTime12h(int hour24,int minute){
  bool pm = hour24 >= 12;
  int hour12 = hour24 % 12;
  if(hour12 == 0) hour12 = 12;

  char b[20];
  snprintf(
    b,
    sizeof(b),
    "%d:%02d %s",
    hour12,
    minute,
    pm ? "PM" : "AM"
  );

  return String(b);
}


// =====================================================
//          PERSISTENT MULTI-ALARM STORAGE (NVS)
// =====================================================

void resetClockStoreRAM(){
  memset(&clockStore,0,sizeof(clockStore));
  clockStore.magic = CLOCK_STORE_MAGIC;
  clockStore.version = CLOCK_STORE_VERSION;
  clockStore.lastGreetingPeriod = -1;
  clockStore.lastGreetingDaySerial = 0;
}


void saveClockStore(){
  if(!clockPrefsReady) return;

  clockPrefs.putBytes(
    "clockState",
    &clockStore,
    sizeof(clockStore)
  );
}


void beginClockStorage(){
  resetClockStoreRAM();

  clockPrefsReady = clockPrefs.begin(
    "kira_clock",
    false
  );

  if(!clockPrefsReady){
    Serial.println("[CLOCK/NVS] Could not open NVS. Alarms will work only until restart.");
    return;
  }

  size_t storedSize = clockPrefs.getBytesLength("clockState");

  if(storedSize == sizeof(clockStore)){
    ClockStore loaded;
    memset(&loaded,0,sizeof(loaded));

    size_t read = clockPrefs.getBytes(
      "clockState",
      &loaded,
      sizeof(loaded)
    );

    if(
      read == sizeof(loaded) &&
      loaded.magic == CLOCK_STORE_MAGIC &&
      loaded.version == CLOCK_STORE_VERSION
    ){
      clockStore = loaded;
      Serial.println("[CLOCK/NVS] Persistent alarm storage loaded.");
      return;
    }
  }

  resetClockStoreRAM();
  saveClockStore();
  Serial.println("[CLOCK/NVS] New persistent alarm storage created.");
}


int activeAlarmCount(){
  int count = 0;

  for(uint8_t i=0;i<MAX_ALARMS;i++){
    if(clockStore.alarms[i].active) count++;
  }

  return count;
}


int alarmSlotForNumber(int number){
  if(number < 1) return -1;

  int visibleNumber = 0;

  for(uint8_t i=0;i<MAX_ALARMS;i++){
    if(!clockStore.alarms[i].active) continue;

    visibleNumber++;

    if(visibleNumber == number){
      return i;
    }
  }

  return -1;
}


String alarmRelativeText(uint64_t targetEpoch){
  time_t nowEpoch = time(nullptr);

  if(nowEpoch < 1700000000 || targetEpoch <= (uint64_t)nowEpoch){
    return "soon";
  }

  uint64_t seconds = targetEpoch - (uint64_t)nowEpoch;

  if(seconds < 60ULL){
    return "in less than a minute";
  }

  if(seconds < 3600ULL){
    uint32_t minutes = (uint32_t)(seconds / 60ULL);
    return "in " + String(minutes) + (minutes == 1 ? " minute" : " minutes");
  }

  if(seconds < 86400ULL){
    uint32_t hours = (uint32_t)(seconds / 3600ULL);
    uint32_t minutes = (uint32_t)((seconds % 3600ULL) / 60ULL);

    String r = "in " + String(hours) + (hours == 1 ? " hour" : " hours");

    if(minutes > 0){
      r += " and " + String(minutes) + (minutes == 1 ? " minute" : " minutes");
    }

    return r;
  }

  uint32_t days = (uint32_t)(seconds / 86400ULL);
  return "in about " + String(days) + (days == 1 ? " day" : " days");
}


uint64_t nextDailyAlarmEpoch(
  int hour,
  int minute,
  time_t fromEpoch
){
  struct tm local;

  localtime_r(
    &fromEpoch,
    &local
  );

  local.tm_hour=hour;
  local.tm_min=minute;
  local.tm_sec=0;
  local.tm_isdst=-1;

  time_t target=
    mktime(
      &local
    );

  if(target<=fromEpoch){
    local.tm_mday+=1;
    local.tm_isdst=-1;

    target=
      mktime(
        &local
      );
  }

  if(target<=0){
    return
      (uint64_t)fromEpoch+
      24ULL*60ULL*60ULL;
  }

  return
    (uint64_t)target;
}


void listAlarms(){
  int count = activeAlarmCount();

  if(alarmRinging){
    Serial.println();
    Serial.println("[ALARM] An alarm alert is currently ringing.");
  }

  if(count == 0){
    elliSay(
      alarmRinging
      ? "An alarm is ringing, but there are no other alarms waiting."
      : "You don't have any alarms set right now."
    );
    return;
  }

  Serial.println();
  Serial.println("========== SAVED ALARMS ==========");

  int visibleNumber = 0;

  for(uint8_t i=0;i<MAX_ALARMS;i++){
    StoredAlarm &a = clockStore.alarms[i];
    if(!a.active) continue;

    visibleNumber++;

    Serial.print("#");
    Serial.print(visibleNumber);
    Serial.print("  ");
    Serial.print(
      formatClockTime12h(
        a.hour,
        a.minute
      )
    );

    Serial.print(
      a.repeatDaily
      ? "  [daily]  ("
      : "  [one-time]  ("
    );

    Serial.print(
      alarmRelativeText(
        a.targetEpoch
      )
    );

    Serial.println(")");
  }

  Serial.println("==================================");

  elliSay(
    "You have " +
    String(count) +
    (count == 1 ? " alarm set." : " alarms set. I've listed them above.")
  );
}


bool parseAlarmNumber(String q,int& number){
  q = normalizeInput(q);

  int p = q.indexOf("alarm ");
  if(p < 0) return false;

  p += 6;

  while(p < q.length() && q[p] == ' ') p++;

  String digits;

  while(p < q.length() && isdigit((unsigned char)q[p])){
    digits += q[p];
    p++;
  }

  if(!digits.length()) return false;

  number = digits.toInt();
  return number >= 1;
}


void cancelAlarmNumber(int number){
  int slot = alarmSlotForNumber(number);

  if(slot < 0){
    elliSay("I couldn't find alarm number " + String(number) + ".");
    return;
  }

  String timeText = formatClockTime12h(
    clockStore.alarms[slot].hour,
    clockStore.alarms[slot].minute
  );

  memset(&clockStore.alarms[slot],0,sizeof(StoredAlarm));
  saveClockStore();

  elliSay(
    "Alarm number " +
    String(number) +
    " at " +
    timeText +
    " has been cancelled."
  );
}


void cancelAllAlarms(){
  int count = activeAlarmCount();

  for(uint8_t i=0;i<MAX_ALARMS;i++){
    memset(&clockStore.alarms[i],0,sizeof(StoredAlarm));
  }

  alarmRinging = false;
  ringingAlarmMask = 0;
  ringingAlarmHour = -1;
  ringingAlarmMinute = -1;

  saveClockStore();

  if(count == 0){
    elliSay("There weren't any saved alarms to cancel.");
  }else{
    elliSay(
      "Done. I cancelled " +
      String(count) +
      (count == 1 ? " alarm." : " alarms.")
    );
  }
}


bool addAlarm(
  int hour,
  int minute,
  bool forceTomorrow,
  bool forceToday,
  bool repeatDaily
){
  struct tm nowInfo;

  if(!getLocalTime(&nowInfo,1000)){
    elliSay(
      "I can't set a clock alarm until my date and time are synchronized."
    );
    return false;
  }

  time_t nowEpoch = time(nullptr);

  if(nowEpoch < 1700000000){
    elliSay("My clock isn't synchronized yet, so I can't save that alarm safely.");
    return false;
  }

  int nowSeconds =
    nowInfo.tm_hour * 3600 +
    nowInfo.tm_min * 60 +
    nowInfo.tm_sec;

  int targetSeconds =
    hour * 3600 +
    minute * 60;

  int32_t deltaSeconds =
    targetSeconds - nowSeconds;

  if(forceTomorrow){
    deltaSeconds += 24 * 3600;
  }
  else if(forceToday){
    if(deltaSeconds <= 0){
      elliSay("That time has already passed today. Choose a later time or say tomorrow.");
      return false;
    }
  }
  else if(deltaSeconds <= 0){
    deltaSeconds += 24 * 3600;
  }

  uint64_t targetEpoch =
    (uint64_t)nowEpoch +
    (uint64_t)deltaSeconds;

  // Duplicate check.
  for(uint8_t i=0;i<MAX_ALARMS;i++){
    StoredAlarm &a = clockStore.alarms[i];

    if(!a.active) continue;

    bool sameDaily =
      repeatDaily &&
      a.repeatDaily &&
      a.hour == hour &&
      a.minute == minute;

    bool sameOneTime =
      !repeatDaily &&
      !a.repeatDaily &&
      a.targetEpoch == targetEpoch;

    if(sameDaily || sameOneTime){
      elliSay("You already have that alarm set.");
      return false;
    }
  }

  int freeSlot = -1;

  for(uint8_t i=0;i<MAX_ALARMS;i++){
    if(!clockStore.alarms[i].active){
      freeSlot = i;
      break;
    }
  }

  if(freeSlot < 0){
    elliSay(
      "My alarm list is full. I can store up to " +
      String(MAX_ALARMS) +
      " alarms at once."
    );
    return false;
  }

  StoredAlarm &a = clockStore.alarms[freeSlot];
  memset(&a,0,sizeof(a));

  a.active = 1;
  a.hour = (uint8_t)hour;
  a.minute = (uint8_t)minute;
  a.repeatDaily = repeatDaily ? 1 : 0;
  a.targetEpoch = targetEpoch;

  saveClockStore();

  int number = 0;
  for(int i=0;i<=freeSlot;i++){
    if(clockStore.alarms[i].active) number++;
  }

  String scheduleText="";

  if(repeatDaily){
    if(forceTomorrow) scheduleText = " every day starting tomorrow";
    else if(forceToday) scheduleText = " every day starting today";
    else scheduleText = " every day";
  }
  else{
    if(forceTomorrow) scheduleText = " tomorrow";
    else if(forceToday) scheduleText = " today";
    else scheduleText = " once";
  }

  elliSay(
    "Alarm " +
    String(number) +
    " saved for " +
    formatClockTime12h(hour,minute) +
    scheduleText +
    "."
  );

  return true;
}


// =====================================================
//             AUTOMATIC TIME-OF-DAY GREETING
// =====================================================
// Elli greets automatically on the first interaction in
// each period. The last greeted period is also stored in
// NVS, so rebooting does not make her repeat it.
//
// Morning   : 05:00 - 11:59
// Afternoon : 12:00 - 16:59
// Evening   : 17:00 - 20:59
// Night     : 21:00 - 04:59

int currentGreetingPeriod(int hour){
  if(hour >= 5 && hour < 12) return 0;
  if(hour >= 12 && hour < 17) return 1;
  if(hour >= 17 && hour < 21) return 2;
  return 3;
}


uint32_t greetingDaySerial(int hour){
  time_t nowEpoch = time(nullptr);

  if(nowEpoch < 1700000000) return 0;

  int64_t localSeconds =
    (int64_t)nowEpoch +
    (int64_t)GMT_OFFSET_SEC +
    (int64_t)DAYLIGHT_OFFSET_SEC;

  uint32_t daySerial =
    (uint32_t)(localSeconds / 86400LL);

  // Treat midnight through 04:59 as part of the previous
  // night's greeting period, preventing a second greeting
  // simply because the date rolled over at midnight.
  if(hour < 5 && daySerial > 0){
    daySerial--;
  }

  return daySerial;
}


bool maybeAutomaticPeriodGreeting(){
  if(alarmRinging || timerRinging){
    return false;
  }

  struct tm t;

  if(!getLocalTime(&t,200)){
    return false;
  }

  int period = currentGreetingPeriod(t.tm_hour);
  uint32_t daySerial = greetingDaySerial(t.tm_hour);

  if(daySerial == 0) return false;

  if(
    clockStore.lastGreetingPeriod == period &&
    clockStore.lastGreetingDaySerial == daySerial
  ){
    return false;
  }

  clockStore.lastGreetingPeriod = (int8_t)period;
  clockStore.lastGreetingDaySerial = daySerial;
  saveClockStore();

  if(period == 0){
    elliSay(reply(4));
  }
  else if(period == 1){
    elliSay(reply(5));
  }
  else if(period == 2){
    elliSay(reply(6));
  }
  else{
    const char* const late[] = {
      "Hey! It's pretty late, but I'm here whenever you need me.",
      "Hii! Late-night KIRA mode is active. What are we doing?",
      "Hey, it's night time. I'm still here and listening.",
      "Hii! It's getting late. Tell me what you need.",
      "Good night-ish! I'm still awake if you need something.",
      "Hey! It's late, but Elli is still online. What's up?"
    };

    elliSay(late[random(6)]);
  }

  return true;
}


bool isGreetingOnlyInput(const String& q,bool wake){
  if(wake && q.length() == 0) return true;

  const char* const greet[] = {
    "hi","hii","hello","hey","heyy","yo","hiya","hey there","hello there"
  };

  if(equalsAny(q,greet,sizeof(greet)/sizeof(greet[0]))) return true;
  if(isExtendedGreeting(q)) return true;

  if(q.endsWith(" elli") || q.endsWith(" ellie")){
    String greetingPart = q;

    if(greetingPart.endsWith(" ellie")){
      greetingPart.remove(greetingPart.length()-6);
    }else{
      greetingPart.remove(greetingPart.length()-5);
    }

    greetingPart.trim();
    if(isExtendedGreeting(greetingPart)) return true;
  }

  return false;
}


uint32_t stopwatchElapsedMs(){
  if(stopwatchRunning){
    return stopwatchStoredMs + (millis() - stopwatchStartMs);
  }

  return stopwatchStoredMs;
}


void startStopwatch(){
  stopwatchStoredMs = 0;
  stopwatchStartMs = millis();
  stopwatchRunning = true;

  elliSay(
    "Okay, starting now. Say 'Elli stop' whenever you want me to stop counting."
  );
}


void stopStopwatch(){
  if(!stopwatchRunning){
    if(stopwatchStoredMs > 0){
      elliSay(
        "The stopwatch is already stopped at " +
        formatDurationMs(stopwatchStoredMs) +
        "."
      );
    }else{
      elliSay("The stopwatch isn't running right now.");
    }
    return;
  }

  stopwatchStoredMs += millis() - stopwatchStartMs;
  stopwatchRunning = false;

  elliSay(
    "Stopped. That was " +
    formatDurationMs(stopwatchStoredMs) +
    "."
  );
}


void resetStopwatch(){
  stopwatchRunning = false;
  stopwatchStoredMs = 0;
  stopwatchStartMs = 0;
  elliSay("Stopwatch reset to zero.");
}


bool numericToken(const String& token,uint32_t& value){
  if(!token.length()) return false;

  for(size_t i=0;i<token.length();i++){
    if(!isdigit((unsigned char)token[i])) return false;
  }

  value = (uint32_t)token.toInt();
  return true;
}


bool parseDurationMs(String q,uint32_t& durationMs){
  q = normalizeInput(q);

  // Natural shortcuts.
  q.replace("half an hour","30 minutes");
  q.replace("half hour","30 minutes");
  q.replace("an hour","1 hour");
  q.replace("a hour","1 hour");
  q.replace("a minute","1 minute");
  q.replace("a second","1 second");

  uint64_t totalSeconds = 0;
  uint32_t pendingNumber = 0;
  bool haveNumber = false;
  bool foundUnit = false;

  int pos = 0;

  while(pos < q.length()){
    while(pos < q.length() && q[pos] == ' ') pos++;
    if(pos >= q.length()) break;

    int end = q.indexOf(' ',pos);
    if(end < 0) end = q.length();

    String token = q.substring(pos,end);
    token.trim();

    uint32_t n = 0;

    if(numericToken(token,n)){
      pendingNumber = n;
      haveNumber = true;
    }
    else if(haveNumber){
      if(token == "second" || token == "seconds" || token == "sec" || token == "secs"){
        totalSeconds += pendingNumber;
        foundUnit = true;
        haveNumber = false;
      }
      else if(token == "minute" || token == "minutes" || token == "min" || token == "mins"){
        totalSeconds += (uint64_t)pendingNumber * 60ULL;
        foundUnit = true;
        haveNumber = false;
      }
      else if(token == "hour" || token == "hours" || token == "hr" || token == "hrs"){
        totalSeconds += (uint64_t)pendingNumber * 3600ULL;
        foundUnit = true;
        haveNumber = false;
      }
      else if(token == "day" || token == "days"){
        totalSeconds += (uint64_t)pendingNumber * 86400ULL;
        foundUnit = true;
        haveNumber = false;
      }
    }

    pos = end + 1;
  }

  if(!foundUnit || totalSeconds == 0) return false;

  // Keep millis()-based scheduling safely below its wrap window.
  const uint64_t MAX_SECONDS = 7ULL * 24ULL * 3600ULL;
  if(totalSeconds > MAX_SECONDS) return false;

  durationMs = (uint32_t)(totalSeconds * 1000ULL);
  return true;
}


uint32_t timerRemainingNowMs(){
  if(timerPaused) return timerRemainingMs;
  if(!timerActive) return 0;

  int32_t left = (int32_t)(timerEndMs - millis());
  return left > 0 ? (uint32_t)left : 0;
}


void startTimer(uint32_t durationMs){
  timerActive = true;
  timerPaused = false;
  timerRinging = false;
  timerRemainingMs = durationMs;
  timerEndMs = millis() + durationMs;

  elliSay(
    "Timer started for " +
    formatDurationMs(durationMs) +
    "."
  );
}


void pauseTimer(){
  if(!timerActive || timerPaused){
    elliSay("There isn't a running timer to pause.");
    return;
  }

  timerRemainingMs = timerRemainingNowMs();
  timerPaused = true;

  elliSay(
    "Timer paused with " +
    formatDurationMs(timerRemainingMs) +
    " remaining."
  );
}


void resumeTimer(){
  if(!timerActive || !timerPaused){
    elliSay("There isn't a paused timer to resume.");
    return;
  }

  timerPaused = false;
  timerEndMs = millis() + timerRemainingMs;

  elliSay("Timer resumed.");
}


void cancelTimer(){
  timerActive = false;
  timerPaused = false;
  timerRinging = false;
  timerEndMs = 0;
  timerRemainingMs = 0;

  elliSay("Timer cancelled.");
}


bool parseAlarmTime(String q,int& outHour,int& outMinute){
  q = normalizeInput(q);

  bool hasAM = q.indexOf(" am") >= 0 || q.endsWith("am");
  bool hasPM = q.indexOf(" pm") >= 0 || q.endsWith("pm");

  const char* const prefixes[] = {
    "set an alarm for ",
    "set another alarm for ",
    "set alarm for ",
    "set an alarm at ",
    "set another alarm at ",
    "set alarm at ",
    "alarm for ",
    "alarm at ",
    "wake me up at ",
    "wake me at "
  };

  for(size_t i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);i++){
    String p = prefixes[i];
    if(q.startsWith(p)){
      q.remove(0,p.length());
      break;
    }
  }

  // Scheduling words are handled by processCommand().
  q.replace("tomorrow","");
  q.replace("today","");
  q.replace("every day","");
  q.replace("everyday","");
  q.replace("daily","");
  q.replace("once","");

  q.replace(" am","");
  q.replace(" pm","");
  q.replace("am","");
  q.replace("pm","");
  q.replace(" ","");
  q.trim();

  if(!q.length() || q.length() > 4) return false;

  for(size_t i=0;i<q.length();i++){
    if(!isdigit((unsigned char)q[i])) return false;
  }

  int hour = 0;
  int minute = 0;

  if(q.length() <= 2){
    hour = q.toInt();
  }
  else if(q.length() == 3){
    hour = q.substring(0,1).toInt();
    minute = q.substring(1).toInt();
  }
  else{
    hour = q.substring(0,2).toInt();
    minute = q.substring(2).toInt();
  }

  if(minute < 0 || minute > 59) return false;

  if(hasAM || hasPM){
    if(hour < 1 || hour > 12) return false;

    if(hasAM){
      if(hour == 12) hour = 0;
    }else{
      if(hour != 12) hour += 12;
    }
  }else{
    if(hour < 0 || hour > 23) return false;
  }

  outHour = hour;
  outMinute = minute;
  return true;
}


void stopClockAlert(){
  bool stopped = false;

  if(alarmRinging){
    alarmRinging = false;
    ringingAlarmMask = 0;
    ringingAlarmHour = -1;
    ringingAlarmMinute = -1;
    stopped = true;
  }

  if(timerRinging){
    timerRinging = false;
    stopped = true;
  }

  if(stopped){
    elliSay("Okay, alert stopped.");
  }else{
    elliSay("There isn't an alarm or timer alert ringing right now.");
  }
}


void updateClockBrain(){
  uint32_t nowMs = millis();

  // ---------------- TIMER ----------------
  if(timerActive && !timerPaused && !timerRinging){
    if((int32_t)(nowMs - timerEndMs) >= 0){
      timerActive = false;
      timerRemainingMs = 0;
      timerRinging = true;

      Serial.println();
      Serial.println("====================================");
      Serial.println("[TIMER] TIME'S UP!");
      Serial.println("====================================");
      elliSay("Time's up! Your timer has finished.");
    }
  }

  // ---------------- PERSISTENT ALARMS ----------------
  time_t nowEpoch = time(nullptr);

  if(nowEpoch < 1700000000){
    return;
  }

  bool storageChanged = false;
  bool newAlarmDue = false;
  int newlyDueCount = 0;
  int firstHour = -1;
  int firstMinute = -1;

  for(uint8_t i=0;i<MAX_ALARMS;i++){
    StoredAlarm &a = clockStore.alarms[i];

    if(!a.active) continue;

    if(a.targetEpoch <= (uint64_t)nowEpoch){
      uint64_t lateBy =
        (uint64_t)nowEpoch - a.targetEpoch;

      // Ring when the occurrence is current or no more than
      // 15 minutes late (for example after a short reboot).
      if(lateBy <= 15ULL * 60ULL){
        if(firstHour < 0){
          firstHour = a.hour;
          firstMinute = a.minute;
        }

        ringingAlarmMask |= (uint16_t)(1U << i);
        newAlarmDue = true;
        newlyDueCount++;
      }
      else{
        Serial.print("[ALARM] Missed occurrence skipped: ");
        Serial.println(formatClockTime12h(a.hour,a.minute));
      }

      if(a.repeatDaily){
        // Keep the alarm and move its next occurrence forward.
        a.targetEpoch = nextDailyAlarmEpoch(
          a.hour,
          a.minute,
          nowEpoch
        );

        a.active = 1;

        Serial.print("[ALARM] Daily alarm rescheduled -> ");
        Serial.println(formatEpochLocal(a.targetEpoch));
      }
      else{
        // Only explicit one-time alarms disappear after firing.
        a.active = 0;
      }

      storageChanged = true;
    }
  }

  if(storageChanged){
    saveClockStore();
  }

  if(newAlarmDue){
    bool wasAlreadyRinging = alarmRinging;
    alarmRinging = true;

    if(ringingAlarmHour < 0){
      ringingAlarmHour = firstHour;
      ringingAlarmMinute = firstMinute;
    }

    Serial.println();
    Serial.println("====================================");

    if(newlyDueCount == 1){
      Serial.print("[ALARM] ");
      Serial.println(formatClockTime12h(firstHour,firstMinute));
    }else{
      Serial.print("[ALARM] ");
      Serial.print(newlyDueCount);
      Serial.println(" alarms are due now");
    }

    Serial.println("====================================");

    if(!wasAlreadyRinging){
      if(newlyDueCount == 1){
        elliSay(
          "Alarm! It's " +
          formatClockTime12h(firstHour,firstMinute) +
          "."
        );
      }else{
        elliSay(
          "You have " +
          String(newlyDueCount) +
          " alarms due now."
        );
      }
    }else{
      Serial.println("[ALARM] Another saved alarm became due while the alert was already active.");
    }
  }
}

void connectWiFi(){
  kiraNetworkBegin(
    KIRA_WIFI1_SSID,
    KIRA_WIFI1_PASSWORD,
    KIRA_WIFI2_SSID,
    KIRA_WIFI2_PASSWORD
  );
}

void maintainWiFi(){
  bool wasConnected=WiFi.status()==WL_CONNECTED;
  kiraNetworkMaintain();
  if(!wasConnected && WiFi.status()==WL_CONNECTED){
    // Recover NTP automatically after either configured Wi-Fi returns.
    syncTime();
  }
}

void syncTime(){
  if(WiFi.status()!=WL_CONNECTED) return;
  configTime(GMT_OFFSET_SEC,DAYLIGHT_OFFSET_SEC,NTP_SERVER_1,NTP_SERVER_2);
  struct tm t; Serial.println("[TIME] Synchronizing...");
  Serial.println(getLocalTime(&t,10000) ? "[TIME] Ready." : "[TIME] Sync failed.");
}

String urlEncode(const String& s){
  String e; const char hex[]="0123456789ABCDEF";
  for(size_t i=0;i<s.length();i++){
    unsigned char c=s[i];
    if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~') e+=(char)c;
    else if(c==' ') e+="%20";
    else{ e+='%'; e+=hex[(c>>4)&15]; e+=hex[c&15]; }
  }
  return e;
}

String extractJsonString(const String& j,const String& key){
  String target="\""+key+"\"";
  int kp=j.indexOf(target); if(kp<0) return "";
  int colon=j.indexOf(':',kp); if(colon<0) return "";
  int q=j.indexOf('"',colon+1); if(q<0) return "";
  String r; bool esc=false;
  for(int i=q+1;i<j.length();i++){
    char c=j[i];
    if(esc){
      if(c=='n') r+='\n'; else if(c=='r') r+='\r'; else if(c=='t') r+='\t';
      else if(c=='"'||c=='\\'||c=='/') r+=c;
      else r+=c;
      esc=false; continue;
    }
    if(c=='\\'){ esc=true; continue; }
    if(c=='"') break;
    r+=c;
  }
  return r;
}

String stripHtml(String s){
  String r; bool tag=false;

  for(size_t i=0;i<s.length();i++){
    char c=s[i];

    if(c=='<'){
      tag=true;
      continue;
    }

    if(c=='>'){
      tag=false;
      continue;
    }

    if(!tag) r+=c;
  }

  // Common HTML entities that appeared in v0.3 output.
  r.replace("&quot;","\"");
  r.replace("&amp;","&");
  r.replace("&#39;","'");
  r.replace("&#039;","'");
  r.replace("&apos;","'");
  r.replace("&nbsp;"," ");
  r.replace("&lt;","<");
  r.replace("&gt;",">");

  while(r.indexOf("  ")>=0) r.replace("  "," ");

  return r;
}



// Knowledge concepts are no longer rewritten with example-specific replacements.
// Generic concept variants are generated by elli_semantics instead.

String cleanKnowledgeQuery(String q){
  q=normalizeInput(q);

  const char* prefixes[]={
    "what is meant by ",
    "what is ",
    "what are ",
    "who is ",
    "who was ",
    "tell me about ",
    "define ",
    "explain ",
    "meaning of ",
    "give me information about "
  };

  for(size_t i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);i++){
    String p=prefixes[i];

    if(q.startsWith(p)){
      q.remove(0,p.length());
      break;
    }
  }

  q.trim();

  // Important for dictionary/entity lookup:
  // "what is a tumbler?" must become "tumbler", not "a tumbler".
  //
  // The old v0.6.0 kept the article, which weakened the search and
  // allowed results such as "Tumbler Ridge" to look relevant.
  if(q.startsWith("a ")){
    q.remove(0,2);
  }
  else if(q.startsWith("an ")){
    q.remove(0,3);
  }
  else if(q.startsWith("the ")){
    q.remove(0,4);
  }

  q.trim();

  return q;
}

bool isDefinitionStyle(String q){
  q=normalizeInput(q);

  const char* p[]={
    "what is ",
    "what are ",
    "what is meant by ",
    "who is ",
    "who was ",
    "tell me about ",
    "define ",
    "explain ",
    "meaning of "
  };

  for(size_t i=0;i<sizeof(p)/sizeof(p[0]);i++){
    if(q.startsWith(p[i])) return true;
  }

  return false;
}


// =====================================================
//                 WEB ANSWER CLEANER
// =====================================================

bool lineLooksLikeWikiNoise(String line){
  line.trim();

  if(!line.length()) return true;

  String lower=line;
  lower.toLowerCase();

  if(line.startsWith("==")) return true;

  const char* noise[]={
    "etymology",
    "pronunciation",
    "rhymes:",
    "homophone:",
    "references",
    "further reading",
    "external links",
    "anagrams",
    "see also"
  };

  for(size_t i=0;i<sizeof(noise)/sizeof(noise[0]);i++){
    if(lower.indexOf(noise[i])>=0) return true;
  }

  return false;
}


String cleanWebAnswer(String input,int maxChars,int maxSentences){
  input=stripHtml(input);

  String filtered;
  filtered.reserve(min((int)input.length(),maxChars+120));

  int start=0;

  while(start<input.length()){
    int end=input.indexOf('\n',start);

    if(end<0) end=input.length();

    String line=input.substring(start,end);
    line.trim();

    if(!lineLooksLikeWikiNoise(line)){
      if(filtered.length()) filtered+=" ";
      filtered+=line;
    }

    start=end+1;

    if(filtered.length()>maxChars+150) break;
  }

  while(filtered.indexOf("  ")>=0){
    filtered.replace("  "," ");
  }

  filtered.trim();

  if(!filtered.length()) return "";

  String out;
  out.reserve(min((int)filtered.length(),maxChars+8));

  int sentences=0;

  for(size_t i=0;i<filtered.length();i++){
    char c=filtered[i];

    out+=c;

    if(c=='.' || c=='!' || c=='?'){
      // Avoid treating decimal points as sentence endings.
      bool decimalPoint=
        c=='.' &&
        i>0 &&
        i+1<filtered.length() &&
        isdigit((unsigned char)filtered[i-1]) &&
        isdigit((unsigned char)filtered[i+1]);

      if(!decimalPoint){
        sentences++;

        if(sentences>=maxSentences) break;
      }
    }

    if(out.length()>=maxChars){
      break;
    }
  }

  out.trim();

  if(out.length()>=maxChars && !out.endsWith(".")){
    int lastSpace=out.lastIndexOf(' ');

    if(lastSpace>maxChars-80){
      out.remove(lastSpace);
    }

    out+="...";
  }

  return out;
}


// =====================================================
//                 JSON NUMBER HELPERS
// =====================================================

bool extractJsonNumberValue(
  const String& json,
  const String& key,
  double& value
){
  String target="\""+key+"\"";
  int searchFrom=0;

  while(true){
    int kp=json.indexOf(target,searchFrom);

    if(kp<0) return false;

    int colon=json.indexOf(':',kp+target.length());

    if(colon<0) return false;

    int p=colon+1;

    while(
      p<json.length() &&
      (
        json[p]==' ' ||
        json[p]=='\t' ||
        json[p]=='\r' ||
        json[p]=='\n'
      )
    ){
      p++;
    }

    // Skip a string-valued copy of the same key and try the
    // next occurrence. This matters for APIs that provide
    // both "current_units" and numeric "current" values.
    if(p<json.length() && json[p]=='"'){
      searchFrom=kp+target.length();
      continue;
    }

    int start=p;

    if(p<json.length() && (json[p]=='-' || json[p]=='+')){
      p++;
    }

    bool digit=false;
    bool dot=false;

    while(p<json.length()){
      char c=json[p];

      if(isdigit((unsigned char)c)){
        digit=true;
        p++;
        continue;
      }

      if(c=='.' && !dot){
        dot=true;
        p++;
        continue;
      }

      if(c=='e' || c=='E'){
        p++;

        if(p<json.length() && (json[p]=='+' || json[p]=='-')){
          p++;
        }

        continue;
      }

      break;
    }

    if(digit){
      value=json.substring(start,p).toDouble();
      return true;
    }

    searchFrom=kp+target.length();
  }
}


bool extractFirstNumber(const String& s,double& value){
  int start=-1;
  bool dot=false;

  for(int i=0;i<s.length();i++){
    char c=s[i];

    if(
      isdigit((unsigned char)c) ||
      (
        (c=='-' || c=='+') &&
        i+1<s.length() &&
        isdigit((unsigned char)s[i+1])
      )
    ){
      start=i;
      break;
    }
  }

  if(start<0) return false;

  int p=start;

  if(s[p]=='-' || s[p]=='+'){
    p++;
  }

  while(p<s.length()){
    char c=s[p];

    if(isdigit((unsigned char)c)){
      p++;
      continue;
    }

    if(c=='.' && !dot){
      dot=true;
      p++;
      continue;
    }

    break;
  }

  value=s.substring(start,p).toDouble();
  return true;
}


String formatWholeNumber(uint64_t value){
  char buffer[32];
  snprintf(
    buffer,
    sizeof(buffer),
    "%llu",
    (unsigned long long)value
  );

  String digits=String(buffer);
  String out;
  int count=0;

  for(int i=digits.length()-1;i>=0;i--){
    String one=String(digits[i]);
    out=one+out;
    count++;

    if(count==3 && i>0){
      out=","+out;
      count=0;
    }
  }

  return out;
}


// =====================================================
//                  SOURCE CATALOG
// =====================================================
//
// v0.6.0 starts a 50+ source architecture. Sources marked
// enabled=true have a working connector in THIS build.
// The rest are catalogued for later connector phases.
//
// KIRA does NOT query all sources. The Source Sense Router
// chooses a small pool based on the question type.
//
// =====================================================

struct WebSourceEntry {
  const char* name;
  const char* category;
  bool enabled;
};

const WebSourceEntry WEB_SOURCE_CATALOG[]={
  {"DuckDuckGo Instant Answer","general",true},
  {"DictionaryAPI.dev","dictionary",true},
  {"Simple English Wikipedia","general",true},
  {"English Wikipedia","general",true},
  {"Wikidata","entity",true},
  {"Wiktionary","dictionary",true},
  {"REST Countries","geography",true},
  {"Open-Meteo Geocoding","weather",true},
  {"Open-Meteo Weather","weather",true},
  {"Frankfurter","currency",true},
  {"Open Library","books",true},
  {"Crossref","research",true},
  {"PyPI","python",true},
  {"npm Registry","javascript",true},
  {"PubChem PUG REST","chemistry",true},

  {"Wikibooks","education",false},
  {"Wikiversity","education",false},
  {"Wikispecies","biology",false},
  {"Wikivoyage","travel",false},
  {"Wikinews","news",false},
  {"arXiv","research",false},
  {"PubMed","medicine/research",false},
  {"Europe PMC","medicine/research",false},
  {"OpenAlex","research",false},
  {"Semantic Scholar","research",false},
  {"OpenCitations","research",false},
  {"DOAJ","research",false},
  {"World Bank Open Data","economics",false},
  {"OpenStreetMap Nominatim","geography",false},
  {"OpenStreetMap Overpass","geography",false},
  {"GeoNames","geography",false},
  {"USGS Earthquake","earth science",false},
  {"NOAA / NWS","weather",false},
  {"GBIF","biology",false},
  {"iNaturalist","biology",false},
  {"UniProt","biology",false},
  {"ChEMBL","chemistry",false},
  {"RCSB Protein Data Bank","biology",false},
  {"MusicBrainz","music",false},
  {"TVMaze","television",false},
  {"Stack Exchange","technology",false},
  {"GitHub API","technology",false},
  {"crates.io","programming",false},
  {"Maven Central","programming",false},
  {"Hacker News","technology/news",false},
  {"Open Food Facts","food",false},
  {"Open-Meteo Air Quality","air quality",false},
  {"Nager.Date","holidays",false},
  {"Wikimedia Commons","media",false},
  {"RFC Editor","internet standards",false},
  {"IETF Datatracker","internet standards",false},
  {"DBpedia","structured knowledge",false},
  {"SEC EDGAR","companies",false},
  {"OpenCorporates","companies",false},
  {"OpenStreetMap Wiki","geography",false}
};

const size_t WEB_SOURCE_COUNT=
  sizeof(WEB_SOURCE_CATALOG)/
  sizeof(WEB_SOURCE_CATALOG[0]);


int enabledWebSourceCount(){
  int count=0;

  for(size_t i=0;i<WEB_SOURCE_COUNT;i++){
    if(WEB_SOURCE_CATALOG[i].enabled) count++;
  }

  return count;
}


void printWebSourceCatalog(){
  Serial.println();
  Serial.println("========== KIRA WEB SOURCE CATALOG ==========");

  Serial.print("Catalogued sources : ");
  Serial.println(WEB_SOURCE_COUNT);

  Serial.print("Enabled connectors : ");
  Serial.println(enabledWebSourceCount());

  Serial.println();
  Serial.println("ENABLED NOW:");

  for(size_t i=0;i<WEB_SOURCE_COUNT;i++){
    if(!WEB_SOURCE_CATALOG[i].enabled) continue;

    Serial.print("  + ");
    Serial.print(WEB_SOURCE_CATALOG[i].name);
    Serial.print(" [");
    Serial.print(WEB_SOURCE_CATALOG[i].category);
    Serial.println("]");
  }

  Serial.println();
  Serial.println("PLANNED / NOT YET CONNECTED:");

  for(size_t i=0;i<WEB_SOURCE_COUNT;i++){
    if(WEB_SOURCE_CATALOG[i].enabled) continue;

    Serial.print("  - ");
    Serial.print(WEB_SOURCE_CATALOG[i].name);
    Serial.print(" [");
    Serial.print(WEB_SOURCE_CATALOG[i].category);
    Serial.println("]");
  }

  Serial.println("=============================================");
}


// =====================================================
//                SOURCE SENSE CLASSIFIER
// =====================================================

const int WEB_INTENT_WORD       = 1;
const int WEB_INTENT_GENERAL    = 2;
const int WEB_INTENT_PERSON     = 3;
const int WEB_INTENT_COMPANY    = 4;
const int WEB_INTENT_SCIENCE    = 5;
const int WEB_INTENT_ASTRONOMY  = 6;
const int WEB_INTENT_GEOGRAPHY  = 7;
const int WEB_INTENT_WEATHER    = 8;
const int WEB_INTENT_CURRENCY   = 9;
const int WEB_INTENT_BOOK       = 10;
const int WEB_INTENT_RESEARCH   = 11;
const int WEB_INTENT_PYTHON     = 12;
const int WEB_INTENT_NPM        = 13;
const int WEB_INTENT_CHEMISTRY  = 14;
const int WEB_INTENT_NEWS       = 15;


const char* webIntentName(int intent){
  switch(intent){
    case WEB_INTENT_WORD:      return "WORD / DEFINITION";
    case WEB_INTENT_PERSON:    return "PERSON";
    case WEB_INTENT_COMPANY:   return "COMPANY";
    case WEB_INTENT_SCIENCE:   return "SCIENCE";
    case WEB_INTENT_ASTRONOMY: return "ASTRONOMY";
    case WEB_INTENT_GEOGRAPHY: return "GEOGRAPHY";
    case WEB_INTENT_WEATHER:   return "WEATHER";
    case WEB_INTENT_CURRENCY:  return "CURRENCY";
    case WEB_INTENT_BOOK:      return "BOOK";
    case WEB_INTENT_RESEARCH:  return "RESEARCH";
    case WEB_INTENT_PYTHON:    return "PYTHON PACKAGE";
    case WEB_INTENT_NPM:       return "NPM PACKAGE";
    case WEB_INTENT_CHEMISTRY: return "CHEMISTRY";
    case WEB_INTENT_NEWS:      return "CURRENT / NEWS";
    default:                   return "GENERAL KNOWLEDGE";
  }
}


bool containsCurrencyClue(String q){
  q=normalizeInput(q);

  const char* clues[]={
    "currency",
    "exchange rate",
    " usd",
    " inr",
    " eur",
    " gbp",
    " jpy",
    " aud",
    " cad",
    " chf",
    " cny",
    " rupee",
    " rupees",
    " dollar",
    " dollars",
    " euro",
    " euros",
    " pound sterling",
    " yen"
  };

  for(size_t i=0;i<sizeof(clues)/sizeof(clues[0]);i++){
    if(q.indexOf(clues[i])>=0) return true;
  }

  return false;
}


int wordCount(String q){
  q.trim();

  if(!q.length()) return 0;

  int count=1;

  for(size_t i=0;i<q.length();i++){
    if(q[i]==' ') count++;
  }

  return count;
}


int classifyWebIntent(String q){
  q=normalizeInput(q);

  if(
    q.indexOf("weather")>=0 ||
    q.indexOf("forecast")>=0 ||
    q.indexOf("temperature in ")>=0 ||
    q.indexOf("temperature at ")>=0 ||
    q.indexOf("rain in ")>=0 ||
    q.indexOf("humidity in ")>=0 ||
    q.indexOf("wind in ")>=0
  ){
    return WEB_INTENT_WEATHER;
  }

  if(containsCurrencyClue(q)){
    return WEB_INTENT_CURRENCY;
  }

  if(
    q.indexOf("pypi")>=0 ||
    q.indexOf("python package")>=0
  ){
    return WEB_INTENT_PYTHON;
  }

  if(
    q.indexOf("npm package")>=0 ||
    q.startsWith("npm ") ||
    q.indexOf(" node package")>=0
  ){
    return WEB_INTENT_NPM;
  }

  if(
    q.indexOf("molecular formula")>=0 ||
    q.indexOf("molecular weight")>=0 ||
    q.indexOf("chemical formula")>=0 ||
    q.indexOf("iupac")>=0 ||
    q.indexOf("pubchem")>=0 ||
    q.indexOf("chemical compound")>=0
  ){
    return WEB_INTENT_CHEMISTRY;
  }

  if(
    q.indexOf("isbn")>=0 ||
    q.indexOf("book ")>=0 ||
    q.indexOf("novel ")>=0 ||
    q.startsWith("who wrote ") ||
    q.startsWith("author of ") ||
    q.indexOf("author of ")>=0
  ){
    return WEB_INTENT_BOOK;
  }

  if(
    q.indexOf("doi")>=0 ||
    q.indexOf("research paper")>=0 ||
    q.indexOf("journal article")>=0 ||
    q.indexOf("paper about ")>=0 ||
    q.indexOf("study about ")>=0
  ){
    return WEB_INTENT_RESEARCH;
  }

  if(
    q.indexOf("latest news")>=0 ||
    q.indexOf("news about")>=0 ||
    q.indexOf("current news")>=0 ||
    q.indexOf("what happened today")>=0 ||
    q.indexOf("today's news")>=0
  ){
    return WEB_INTENT_NEWS;
  }

  if(
    q.indexOf("sun")>=0 ||
    q.indexOf("moon")>=0 ||
    q.indexOf("planet")>=0 ||
    q.indexOf("star")>=0 ||
    q.indexOf("galaxy")>=0 ||
    q.indexOf("black hole")>=0 ||
    q.indexOf("asteroid")>=0 ||
    q.indexOf("comet")>=0 ||
    q.indexOf("astronomy")>=0 ||
    q.indexOf("cosmology")>=0 ||
    q.indexOf("spacetime")>=0 ||
    q.indexOf("space time")>=0 ||
    q.indexOf("space-time")>=0 ||
    q.indexOf("relativity")>=0 ||
    q.indexOf("space ")>=0
  ){
    return WEB_INTENT_ASTRONOMY;
  }

  if(
    q.indexOf("capital of ")>=0 ||
    q.indexOf("population of ")>=0 ||
    q.indexOf("country ")>=0 ||
    q.indexOf("continent")>=0 ||
    q.indexOf("geography")>=0 ||
    q.indexOf("country code")>=0 ||
    q.indexOf("flag of ")>=0
  ){
    return WEB_INTENT_GEOGRAPHY;
  }

  if(
    q.indexOf("ceo")>=0 ||
    q.indexOf("company")>=0 ||
    q.indexOf("corporation")>=0 ||
    q.indexOf("headquarters")>=0 ||
    q.indexOf("founder of ")>=0
  ){
    return WEB_INTENT_COMPANY;
  }

  if(
    q.startsWith("who is ") ||
    q.startsWith("who was ") ||
    q.indexOf("born")>=0 ||
    q.indexOf("birthday")>=0 ||
    q.indexOf("nationality")>=0
  ){
    return WEB_INTENT_PERSON;
  }

  String subject=cleanKnowledgeQuery(q);

  if(
    q.startsWith("define ") ||
    q.startsWith("meaning of ") ||
    q.indexOf("synonym of ")>=0 ||
    q.indexOf("antonym of ")>=0 ||
    (
      isDefinitionStyle(q) &&
      wordCount(subject)<=3
    )
  ){
    return WEB_INTENT_WORD;
  }

  if(
    q.indexOf("physics")>=0 ||
    q.indexOf("biology")>=0 ||
    q.indexOf("chemistry")>=0 ||
    q.indexOf("science")>=0 ||
    q.indexOf("photosynthesis")>=0 ||
    q.indexOf("gravity")>=0 ||
    q.indexOf("atom")>=0 ||
    q.indexOf("molecule")>=0
  ){
    return WEB_INTENT_SCIENCE;
  }

  return WEB_INTENT_GENERAL;
}


void printSourceSense(String question){
  int intent=classifyWebIntent(question);

  Serial.println();
  Serial.println("========== SOURCE SENSE ==========");

  Serial.print("Question type : ");
  Serial.println(webIntentName(intent));

  Serial.print("Query         : ");
  Serial.println(cleanKnowledgeQuery(question));

  Serial.print("Preferred pool: ");

  switch(intent){
    case WEB_INTENT_WORD:
      Serial.println("semantic concept resolution -> evidence-scored sources");
      break;

    case WEB_INTENT_WEATHER:
      Serial.println("Open-Meteo Geocoding -> Open-Meteo Weather");
      break;

    case WEB_INTENT_CURRENCY:
      Serial.println("Frankfurter -> general knowledge fallback");
      break;

    case WEB_INTENT_BOOK:
      Serial.println("Open Library -> general knowledge fallback");
      break;

    case WEB_INTENT_RESEARCH:
      Serial.println("Crossref -> Wikipedia/Wikidata fallback");
      break;

    case WEB_INTENT_PYTHON:
      Serial.println("PyPI -> general knowledge fallback");
      break;

    case WEB_INTENT_NPM:
      Serial.println("npm Registry -> general knowledge fallback");
      break;

    case WEB_INTENT_CHEMISTRY:
      Serial.println("PubChem -> Wikipedia/Wikidata fallback");
      break;

    case WEB_INTENT_GEOGRAPHY:
      Serial.println("REST Countries -> Wikidata -> Wikipedia");
      break;

    case WEB_INTENT_PERSON:
    case WEB_INTENT_COMPANY:
      Serial.println("Wikidata -> Wikipedia -> DuckDuckGo");
      break;

    case WEB_INTENT_SCIENCE:
    case WEB_INTENT_ASTRONOMY:
      Serial.println("Wikipedia -> Wikidata -> SimpleWiki -> DuckDuckGo");
      break;

    case WEB_INTENT_NEWS:
      Serial.println("Phase-1 current-news pool not connected yet");
      break;

    default:
      Serial.println("DuckDuckGo -> Wikidata -> SimpleWiki -> Wikipedia");
      break;
  }

  Serial.println("==================================");
}


// =====================================================
//                      HTTPS
// =====================================================

bool httpsGet(const String& url,String& body,int& code){
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(20000);

  HTTPClient https;
  https.setTimeout(20000);
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if(!https.begin(client,url)){
    code=-1000;
    return false;
  }

  https.addHeader(
    "User-Agent",
    "KIRA-ESP32/1.0-alpha educational-assistant"
  );

  code=https.GET();

  if(code>0){
    body=https.getString();
  }

  https.end();

  return code>=200 && code<300;
}


// =====================================================
//                 COMMON SOURCE HELPERS
// =====================================================

bool tryDuck(
  const String& question,
  String& answer,
  String& source,
  int& confidence
){
  String url=
    "https://api.duckduckgo.com/?q="+
    urlEncode(question)+
    "&format=json&no_html=1&no_redirect=1&skip_disambig=1";

  Serial.println("[SOURCE] DuckDuckGo Instant Answer");

  String body;
  int code=0;

  if(!httpsGet(url,body,code)){
    Serial.print("[SOURCE] HTTP ");
    Serial.println(code);
    return false;
  }

  String a=extractJsonString(body,"Answer");
  String d=extractJsonString(body,"Definition");
  String ab=extractJsonString(body,"AbstractText");
  String src=extractJsonString(body,"AbstractSource");

  if(a.length()){
    answer=cleanWebAnswer(a,500,3);
    source="DuckDuckGo Instant Answer";
    confidence=90;
    return answer.length()>0;
  }

  if(d.length()){
    answer=cleanWebAnswer(d,500,3);
    source="DuckDuckGo definition";
    confidence=85;
    return answer.length()>0;
  }

  if(ab.length()){
    String cleaned=cleanWebAnswer(ab,600,4);

    if(
      cleaned.length() &&
      isRelevantResult(
        cleanKnowledgeQuery(question),
        cleaned
      )
    ){
      answer=cleaned;
      source=src.length()?src:"DuckDuckGo knowledge";
      confidence=75;
      return true;
    }
  }

  return false;
}



// =====================================================
//          STRICT COMMON-WORD TITLE MATCHING
// =====================================================
//
// For a definition request such as "what is a tumbler?":
//
// GOOD:
//   Tumbler
//   Tumbler (glass)
//
// BAD:
//   Tumbler Ridge
//   Tumbler pigeons
//
// v0.6.0 only checked whether the word "tumbler" appeared somewhere
// in the result. That was too weak for common-word definitions.
// =====================================================

String normalizeTitleKey(String text){
  text=normalizeInput(text);

  // Remove a parenthetical qualifier:
  // "tumbler (glass)" -> "tumbler"
  int paren=text.indexOf(" (");

  if(paren>0){
    text=text.substring(0,paren);
  }

  text.trim();
  return text;
}


String singularish(String text){
  text=normalizeTitleKey(text);

  if(text.endsWith("ies") && text.length()>4){
    text.remove(text.length()-3);
    text+="y";
  }
  else if(text.endsWith("s") && text.length()>3){
    text.remove(text.length()-1);
  }

  return text;
}


bool titleMatchesCommonWord(
  const String& term,
  const String& title
){
  String a=normalizeTitleKey(term);
  String b=normalizeTitleKey(title);

  if(a==b) return true;

  // Tiny plural tolerance.
  if(singularish(a)==singularish(b)){
    return true;
  }

  return false;
}


bool looksLikeDisambiguationText(String text){
  text=normalizeInput(text);

  return
    text.indexOf("may refer to")>=0 ||
    text.indexOf("can refer to")>=0 ||
    text.indexOf("may also refer to")>=0 ||
    text.indexOf("disambiguation")>=0;
}


// JSON Unicode helpers.
// v0.6.1 used these operations inside extractJsonStringAfter()
// but the helper functions themselves were accidentally omitted.

int jsonHexValue(char c){
  if(c>='0' && c<='9') return c-'0';
  if(c>='A' && c<='F') return c-'A'+10;
  if(c>='a' && c<='f') return c-'a'+10;
  return -1;
}

void appendJsonUTF8(String& output,uint16_t code){
  if(code<=0x7F){
    output+=(char)code;
  }
  else if(code<=0x7FF){
    output+=(char)(0xC0 | ((code>>6)&0x1F));
    output+=(char)(0x80 | (code&0x3F));
  }
  else{
    output+=(char)(0xE0 | ((code>>12)&0x0F));
    output+=(char)(0x80 | ((code>>6)&0x3F));
    output+=(char)(0x80 | (code&0x3F));
  }
}

// Read a JSON string value starting from a particular key occurrence.
// Returns the position after the parsed value through nextSearchPos.
bool extractJsonStringAfter(
  const String& json,
  const String& key,
  int searchFrom,
  String& value,
  int& nextSearchPos
){
  String target="\""+key+"\"";

  int keyPos=json.indexOf(target,searchFrom);

  if(keyPos<0){
    return false;
  }

  int colon=json.indexOf(':',keyPos+target.length());

  if(colon<0){
    return false;
  }

  int quote=json.indexOf('"',colon+1);

  if(quote<0){
    return false;
  }

  String out;
  bool escaped=false;

  for(int i=quote+1;i<json.length();i++){
    char c=json[i];

    if(escaped){
      if(c=='n') out+='\n';
      else if(c=='r') out+='\r';
      else if(c=='t') out+='\t';
      else if(c=='"') out+='"';
      else if(c=='\\') out+='\\';
      else if(c=='/') out+='/';
      else if(c=='u' && i+4<json.length()){
        int h1=jsonHexValue(json[i+1]);
        int h2=jsonHexValue(json[i+2]);
        int h3=jsonHexValue(json[i+3]);
        int h4=jsonHexValue(json[i+4]);

        if(h1>=0 && h2>=0 && h3>=0 && h4>=0){
          uint16_t code=
            (h1<<12) |
            (h2<<8)  |
            (h3<<4)  |
            h4;

          appendJsonUTF8(out,code);
          i+=4;
        }
      }
      else{
        out+=c;
      }

      escaped=false;
      continue;
    }

    if(c=='\\'){
      escaped=true;
      continue;
    }

    if(c=='"'){
      value=out;
      nextSearchPos=i+1;
      return true;
    }

    out+=c;
  }

  return false;
}


bool fetchMediaWikiPageByTitle(
  const String& host,
  const String& label,
  const String& requestedTerm,
  const String& title,
  String& answer,
  String& source,
  int& confidence,
  int baseConfidence
){
  String url=
    "https://"+host+
    "/w/api.php"
    "?action=query"
    "&prop=extracts"
    "&exintro=1"
    "&explaintext=1"
    "&exsentences=4"
    "&redirects=1"
    "&format=json"
    "&formatversion=2"
    "&titles="+urlEncode(title);

  String body;
  int code=0;

  if(!httpsGet(url,body,code)){
    Serial.print("[SOURCE] ");
    Serial.print(label);
    Serial.print(" page HTTP ");
    Serial.println(code);
    return false;
  }

  String resolvedTitle=extractJsonString(body,"title");
  String extract=extractJsonString(body,"extract");

  extract=cleanWebAnswer(extract,650,4);

  if(extract.length()<20){
    return false;
  }

  if(!titleMatchesCommonWord(requestedTerm,resolvedTitle)){
    Serial.print("[SOURCE] Rejected title: ");
    Serial.println(resolvedTitle);
    return false;
  }

  if(looksLikeDisambiguationText(extract)){
    Serial.print("[SOURCE] Rejected disambiguation page: ");
    Serial.println(resolvedTitle);
    return false;
  }

  // For multi-word definitions, an exact page title is not enough.
  // Keep only a sentence that explicitly names one of the GENERIC
  // concept variants generated from the requested phrase.
  if(semanticWordCount(requestedTerm)>=2){
    String focused=semanticFocusDefinition(requestedTerm,extract);

    if(!focused.length()){
      Serial.println("[SOURCE] Rejected: page lead shifts away from the requested concept.");
      return false;
    }

    extract=focused;
  }

  answer=extract;
  source=label+": "+resolvedTitle;
  confidence=baseConfidence;
  return true;
}


bool tryMediaWikiCommonWord(
  const String& host,
  const String& label,
  const String& question,
  String& answer,
  String& source,
  int& confidence,
  int baseConfidence
){
  String term=cleanKnowledgeQuery(question);

  if(term.length()<2 || wordCount(term)>3){
    return false;
  }

  Serial.print("[SOURCE] ");
  Serial.print(label);
  Serial.println(" strict word search");

  // Search several results, then accept ONLY an exact/common-word title.
  // This prevents "tumbler" -> "Tumbler Ridge".
  String searchUrl=
    "https://"+host+
    "/w/api.php"
    "?action=query"
    "&list=search"
    "&srnamespace=0"
    "&srlimit=8"
    "&format=json"
    "&utf8=1"
    "&srsearch="+urlEncode(term);

  String body;
  int code=0;

  if(!httpsGet(searchUrl,body,code)){
    Serial.print("[SOURCE] HTTP ");
    Serial.println(code);
    return false;
  }

  // Keep a few strict candidates. Search order is already relevance-ranked.
  String candidates[6];
  int candidateCount=0;
  int searchPos=0;

  while(candidateCount<6){
    String title;
    int nextPos=0;

    if(
      !extractJsonStringAfter(
        body,
        "title",
        searchPos,
        title,
        nextPos
      )
    ){
      break;
    }

    searchPos=nextPos;

    if(titleMatchesCommonWord(term,title)){
      bool duplicate=false;

      for(int i=0;i<candidateCount;i++){
        if(candidates[i]==title){
          duplicate=true;
          break;
        }
      }

      if(!duplicate){
        candidates[candidateCount++]=title;
      }
    }
    else{
      // Helpful Serial debugging so we can see WHY something was skipped.
      if(normalizeInput(title).startsWith(normalizeInput(term))){
        Serial.print("[SOURCE] Skipping related-but-different title: ");
        Serial.println(title);
      }
    }
  }

  if(candidateCount==0){
    Serial.println("[SOURCE] No strict title match found.");
    return false;
  }

  // Prefer a parenthetical title over a bare disambiguation title when
  // both are present. For example, "Tumbler (glass)" is more useful than
  // a bare "Tumbler" disambiguation page.
  for(int pass=0;pass<2;pass++){
    for(int i=0;i<candidateCount;i++){
      bool hasQualifier=candidates[i].indexOf(" (")>0;

      if(
        (pass==0 && !hasQualifier) ||
        (pass==1 && hasQualifier)
      ){
        continue;
      }

      if(
        fetchMediaWikiPageByTitle(
          host,
          label,
          term,
          candidates[i],
          answer,
          source,
          confidence,
          baseConfidence
        )
      ){
        return true;
      }
    }
  }

  return false;
}


bool tryWikipediaCommonWord(
  const String& question,
  String& answer,
  String& source,
  int& confidence
){
  return tryMediaWikiCommonWord(
    "en.wikipedia.org",
    "Wikipedia",
    question,
    answer,
    source,
    confidence,
    90
  );
}


bool trySimpleWikiCommonWord(
  const String& question,
  String& answer,
  String& source,
  int& confidence
){
  return tryMediaWikiCommonWord(
    "simple.wikipedia.org",
    "Simple Wikipedia",
    question,
    answer,
    source,
    confidence,
    84
  );
}


bool tryWikidataExactWord(
  const String& question,
  String& answer,
  String& source,
  int& confidence
){
  String term=cleanKnowledgeQuery(question);

  if(term.length()<2 || wordCount(term)>3){
    return false;
  }

  String url=
    "https://www.wikidata.org/w/api.php"
    "?action=wbsearchentities"
    "&language=en"
    "&uselang=en"
    "&limit=5"
    "&format=json"
    "&search="+urlEncode(term);

  Serial.println("[SOURCE] Wikidata strict word search");

  String body;
  int code=0;

  if(!httpsGet(url,body,code)){
    Serial.print("[SOURCE] HTTP ");
    Serial.println(code);
    return false;
  }

  int searchPos=0;

  for(int tries=0;tries<5;tries++){
    String label;
    int nextPos=0;

    if(
      !extractJsonStringAfter(
        body,
        "label",
        searchPos,
        label,
        nextPos
      )
    ){
      break;
    }

    searchPos=nextPos;

    if(!titleMatchesCommonWord(term,label)){
      continue;
    }

    // Starting from this label, grab the next description.
    String description;
    int afterDescription=0;

    if(
      !extractJsonStringAfter(
        body,
        "description",
        searchPos,
        description,
        afterDescription
      )
    ){
      continue;
    }

    description=cleanWebAnswer(description,420,2);

    if(description.length()<8){
      continue;
    }

    answer=label+" — "+description+".";

    source="Wikidata";
    confidence=80;
    return true;
  }

  return false;
}


bool tryDictionaryApi(
  const String& question,
  String& answer,
  String& source,
  int& confidence
){
  String term=cleanKnowledgeQuery(question);

  if(wordCount(term)>3 || term.length()<2){
    return false;
  }

  Serial.println("[SOURCE] DictionaryAPI.dev");

  String url=
    "https://api.dictionaryapi.dev/api/v2/entries/en/"+
    urlEncode(term);

  String body;
  int code=0;

  if(!httpsGet(url,body,code)){
    Serial.print("[SOURCE] HTTP ");
    Serial.println(code);
    return false;
  }

  String word=extractJsonString(body,"word");
  String definition=extractJsonString(body,"definition");

  definition=cleanWebAnswer(definition,480,3);

  if(definition.length()<8){
    return false;
  }

  if(
    !isRelevantResult(
      term,
      word+" "+definition
    )
  ){
    return false;
  }

  answer=definition;
  source="DictionaryAPI.dev";
  confidence=95;
  return true;
}


bool tryWikidata(
  const String& question,
  String& answer,
  String& source,
  int& confidence
){
  String q=cleanKnowledgeQuery(question);

  if(q.length()<2) return false;

  String url=
    "https://www.wikidata.org/w/api.php"
    "?action=wbsearchentities"
    "&language=en"
    "&uselang=en"
    "&limit=1"
    "&format=json"
    "&search="+urlEncode(q);

  Serial.println("[SOURCE] Wikidata");

  String body;
  int code=0;

  if(!httpsGet(url,body,code)){
    Serial.print("[SOURCE] HTTP ");
    Serial.println(code);
    return false;
  }

  String label=extractJsonString(body,"label");
  String description=extractJsonString(body,"description");

  description=cleanWebAnswer(description,400,2);

  if(label.length()<2 || description.length()<8){
    return false;
  }

  if(!isRelevantResult(q,label+" "+description)){
    Serial.println("[SOURCE] Rejected: low relevance.");
    return false;
  }

  // Generic semantic evidence gate. This prevents a broad entity such as
  // "India" from answering a narrower ranking/comparison question merely
  // because one query token matched.
  if(!semanticCandidateAcceptable(question,label,description)){
    Serial.print("[SOURCE] Wikidata rejected semantic score ");
    Serial.println(semanticEvidenceScore(question,label,description));
    return false;
  }

  answer=label+" — "+description+".";
  source="Wikidata";
  confidence=max(82,semanticEvidenceScore(question,label,description));
  return true;
}


bool fetchMediaWikiLooseTitle(
  const String& host,
  const String& label,
  const String& question,
  const String& title,
  String& answer,
  String& source,
  int& confidence,
  int baseConfidence
){
  String url=
    "https://"+host+
    "/w/api.php"
    "?action=query"
    "&prop=extracts"
    "&exintro=1"
    "&explaintext=1"
    "&exsentences=4"
    "&redirects=1"
    "&format=json"
    "&formatversion=2"
    "&titles="+urlEncode(title);

  String body;
  int code=0;

  if(!httpsGet(url,body,code)){
    return false;
  }

  String resolvedTitle=extractJsonString(body,"title");
  String extract=extractJsonString(body,"extract");
  extract=cleanWebAnswer(extract,650,4);

  if(extract.length()<20 || looksLikeDisambiguationText(extract)){
    return false;
  }

  int score=semanticEvidenceScore(
    question,
    resolvedTitle,
    extract
  );

  if(!semanticCandidateAcceptable(question,resolvedTitle,extract)){
    Serial.print("[SOURCE] Rejected semantic score ");
    Serial.print(score);
    Serial.print(" for: ");
    Serial.println(resolvedTitle);
    return false;
  }

  answer=extract;
  source=label+": "+resolvedTitle;
  confidence=min(97,max(baseConfidence,score));
  return true;
}


bool tryMediaWikiSummary(
  const String& host,
  const String& label,
  const String& question,
  String& answer,
  String& source,
  int& confidence,
  int baseConfidence
){
  String q=cleanKnowledgeQuery(question);

  // Search several candidates, score them using the query structure,
  // then fetch only the best two. This is generic and does not contain
  // topic-specific rules such as population/university/etc.
  String url=
    "https://"+host+
    "/w/api.php"
    "?action=query"
    "&list=search"
    "&srnamespace=0"
    "&srlimit=6"
    "&format=json"
    "&utf8=1"
    "&srsearch="+urlEncode(q);

  Serial.print("[SOURCE] ");
  Serial.print(label);
  Serial.println(" semantic search");

  String body;
  int code=0;

  if(!httpsGet(url,body,code)){
    Serial.print("[SOURCE] HTTP ");
    Serial.println(code);
    return false;
  }

  struct Candidate {
    String title;
    String snippet;
    int score;
  };

  Candidate best[2];
  best[0].score=-10000;
  best[1].score=-10000;

  int pos=0;

  for(int i=0;i<6;i++){
    String title;
    String snippet;
    int nextTitle=0;
    int nextSnippet=0;

    if(!extractJsonStringAfter(body,"title",pos,title,nextTitle)){
      break;
    }

    if(!extractJsonStringAfter(body,"snippet",nextTitle,snippet,nextSnippet)){
      break;
    }

    pos=nextSnippet;
    snippet=stripHtml(snippet);

    int score=semanticEvidenceScore(q,title,snippet);

    if(score>best[0].score){
      best[1]=best[0];
      best[0].title=title;
      best[0].snippet=snippet;
      best[0].score=score;
    }
    else if(score>best[1].score){
      best[1].title=title;
      best[1].snippet=snippet;
      best[1].score=score;
    }
  }

  for(int i=0;i<2;i++){
    if(best[i].score<0 || !best[i].title.length()) continue;

    Serial.print("[SOURCE] Candidate ");
    Serial.print(i+1);
    Serial.print(" score ");
    Serial.print(best[i].score);
    Serial.print(": ");
    Serial.println(best[i].title);

    if(fetchMediaWikiLooseTitle(
      host,label,question,best[i].title,
      answer,source,confidence,baseConfidence
    )){
      return true;
    }
  }

  return false;
}

bool trySimpleWiki(
  const String& question,
  String& answer,
  String& source,
  int& confidence
){
  return tryMediaWikiSummary(
    "simple.wikipedia.org",
    "Simple Wikipedia",
    question,
    answer,
    source,
    confidence,
    84
  );
}


bool tryWikipedia(
  const String& question,
  String& answer,
  String& source,
  int& confidence
){
  return tryMediaWikiSummary(
    "en.wikipedia.org",
    "Wikipedia",
    question,
    answer,
    source,
    confidence,
    88
  );
}


String extractWiktionaryDefinition(String extract){
  // Prefer a normal part-of-speech section and avoid
  // etymology/pronunciation dumps.
  const char* headings[]={
    "=== Noun ===",
    "=== Proper noun ===",
    "=== Verb ===",
    "=== Adjective ===",
    "=== Adverb ==="
  };

  int sectionStart=-1;

  for(size_t i=0;i<sizeof(headings)/sizeof(headings[0]);i++){
    sectionStart=extract.indexOf(headings[i]);

    if(sectionStart>=0){
      sectionStart+=strlen(headings[i]);
      break;
    }
  }

  if(sectionStart<0){
    return cleanWebAnswer(extract,430,3);
  }

  int sectionEnd=extract.indexOf("===",sectionStart);

  if(sectionEnd<0){
    sectionEnd=extract.length();
  }

  String section=extract.substring(sectionStart,sectionEnd);

  String best="";
  int bestScore=-10000;

  int start=0;

  while(start<section.length()){
    int end=section.indexOf('\n',start);

    if(end<0) end=section.length();

    String line=section.substring(start,end);
    line.trim();

    String lower=line;
    lower.toLowerCase();

    if(
      line.length()>8 &&
      !line.startsWith("==") &&
      lower.indexOf("(plural ")<0 &&
      lower.indexOf("(comparative ")<0
    ){
      int score=10;

      if(
        lower.indexOf("(archaic)")>=0 ||
        lower.indexOf("(obsolete)")>=0 ||
        lower.indexOf("(dated)")>=0 ||
        lower.indexOf("(rare)")>=0
      ){
        score-=40;
      }

      if(
        lower.indexOf("drinking")>=0 ||
        lower.indexOf("glass")>=0 ||
        lower.indexOf("cup")>=0
      ){
        score+=8;
      }

      if(score>bestScore){
        bestScore=score;
        best=line;
      }
    }

    start=end+1;
  }

  return cleanWebAnswer(best,430,3);
}


bool tryWiktionary(
  const String& question,
  String& answer,
  String& source,
  int& confidence
){
  if(!isDefinitionStyle(question)){
    return false;
  }

  String q=cleanKnowledgeQuery(question);

  if(wordCount(q)>3 || q.length()<2){
    return false;
  }

  String url=
    "https://en.wiktionary.org/w/api.php"
    "?action=query"
    "&prop=extracts"
    "&explaintext=1"
    "&exchars=1600"
    "&redirects=1"
    "&format=json"
    "&formatversion=2"
    "&titles="+urlEncode(q);

  Serial.println("[SOURCE] Wiktionary");

  String body;
  int code=0;

  if(!httpsGet(url,body,code)){
    Serial.print("[SOURCE] HTTP ");
    Serial.println(code);
    return false;
  }

  String title=extractJsonString(body,"title");
  String extract=extractJsonString(body,"extract");

  String definition=
    extractWiktionaryDefinition(extract);

  if(definition.length()<10){
    return false;
  }

  if(!isRelevantResult(q,title+" "+definition)){
    return false;
  }

  answer=definition;
  source="Wiktionary: "+title;
  confidence=58;
  return true;
}


// =====================================================
//                  GEOGRAPHY SOURCE
// =====================================================

String extractCountryFromQuestion(String q){
  q=normalizeInput(q);

  const char* phrases[]={
    "capital of ",
    "population of ",
    "country code of ",
    "tell me about ",
    "country "
  };

  for(size_t i=0;i<sizeof(phrases)/sizeof(phrases[0]);i++){
    String p=phrases[i];

    int pos=q.indexOf(p);

    if(pos>=0){
      String country=q.substring(pos+p.length());
      country.trim();

      if(country.endsWith(" country")){
        country.remove(country.length()-8);
        country.trim();
      }

      return country;
    }
  }

  return cleanKnowledgeQuery(q);
}


bool tryRestCountries(
  const String& question,
  String& answer,
  String& source,
  int& confidence
){
  String country=extractCountryFromQuestion(question);

  if(country.length()<2 || country.length()>50){
    return false;
  }

  Serial.println("[SOURCE] REST Countries");

  String url=
    "https://restcountries.com/v3.1/name/"+
    urlEncode(country)+
    "?fullText=true&fields=name,capital,region,subregion,population,cca2,cca3";

  String body;
  int code=0;

  if(!httpsGet(url,body,code)){
    // Retry fuzzy name lookup for aliases such as USA / UK.
    url=
      "https://restcountries.com/v3.1/name/"+
      urlEncode(country)+
      "?fields=name,capital,region,subregion,population,cca2,cca3";

    if(!httpsGet(url,body,code)){
      Serial.print("[SOURCE] HTTP ");
      Serial.println(code);
      return false;
    }
  }

  String common=extractJsonString(body,"common");
  String capital=extractJsonString(body,"capital");
  String region=extractJsonString(body,"region");
  String subregion=extractJsonString(body,"subregion");
  String cca2=extractJsonString(body,"cca2");

  double pop=0;
  bool hasPop=extractJsonNumberValue(body,"population",pop);

  if(common.length()<2){
    return false;
  }

  String q=normalizeInput(question);

  if(q.indexOf("capital")>=0 && capital.length()){
    answer="The capital of "+common+" is "+capital+".";
  }
  else if(q.indexOf("population")>=0 && hasPop){
    answer=
      common+
      " has a population of about "+
      formatWholeNumber((uint64_t)pop)+
      " people.";
  }
  else if(q.indexOf("country code")>=0 && cca2.length()){
    answer=
      common+
      "'s two-letter country code is "+
      cca2+
      ".";
  }
  else{
    answer=common;

    if(capital.length()){
      answer+="'s capital is "+capital;
    }

    if(region.length()){
      answer+=", and it is in "+region;
    }

    if(subregion.length()){
      answer+=" ("+subregion+")";
    }

    if(hasPop){
      answer+=". Its population is about "+formatWholeNumber((uint64_t)pop);
    }

    answer+=".";
  }

  source="REST Countries";
  confidence=96;
  return true;
}


// =====================================================
//                    WEATHER SOURCE
// =====================================================

String extractWeatherLocation(String q){
  q=normalizeInput(q);

  const char* phrases[]={
    "weather in ",
    "weather at ",
    "forecast in ",
    "forecast for ",
    "temperature in ",
    "temperature at ",
    "rain in ",
    "humidity in ",
    "wind in "
  };

  for(size_t i=0;i<sizeof(phrases)/sizeof(phrases[0]);i++){
    String p=phrases[i];
    int pos=q.indexOf(p);

    if(pos>=0){
      String place=q.substring(pos+p.length());
      place.trim();
      return place;
    }
  }

  return "";
}


const char* weatherCodeText(int code){
  if(code==0) return "clear sky";
  if(code==1) return "mainly clear";
  if(code==2) return "partly cloudy";
  if(code==3) return "overcast";
  if(code==45 || code==48) return "fog";
  if(code==51 || code==53 || code==55) return "drizzle";
  if(code==56 || code==57) return "freezing drizzle";
  if(code==61 || code==63 || code==65) return "rain";
  if(code==66 || code==67) return "freezing rain";
  if(code==71 || code==73 || code==75 || code==77) return "snow";
  if(code==80 || code==81 || code==82) return "rain showers";
  if(code==85 || code==86) return "snow showers";
  if(code==95 || code==96 || code==99) return "thunderstorms";
  return "mixed conditions";
}


bool tryOpenMeteo(
  const String& question,
  String& answer,
  String& source,
  int& confidence
){
  String place=extractWeatherLocation(question);

  if(!place.length()){
    return false;
  }

  Serial.println("[SOURCE] Open-Meteo Geocoding");

  String geoUrl=
    "https://geocoding-api.open-meteo.com/v1/search"
    "?name="+urlEncode(place)+
    "&count=1&language=en&format=json";

  String geoBody;
  int geoCode=0;

  if(!httpsGet(geoUrl,geoBody,geoCode)){
    Serial.print("[SOURCE] Geocoding HTTP ");
    Serial.println(geoCode);
    return false;
  }

  double lat=0;
  double lon=0;

  if(
    !extractJsonNumberValue(geoBody,"latitude",lat) ||
    !extractJsonNumberValue(geoBody,"longitude",lon)
  ){
    return false;
  }

  String resolved=extractJsonString(geoBody,"name");
  String country=extractJsonString(geoBody,"country");

  Serial.println("[SOURCE] Open-Meteo Weather");

  String weatherUrl=
    "https://api.open-meteo.com/v1/forecast"
    "?latitude="+String(lat,5)+
    "&longitude="+String(lon,5)+
    "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m"
    "&timezone=auto";

  String body;
  int code=0;

  if(!httpsGet(weatherUrl,body,code)){
    Serial.print("[SOURCE] Weather HTTP ");
    Serial.println(code);
    return false;
  }

  double temp=0;
  double apparent=0;
  double humidity=0;
  double weatherCode=0;
  double wind=0;

  if(!extractJsonNumberValue(body,"temperature_2m",temp)){
    return false;
  }

  extractJsonNumberValue(body,"apparent_temperature",apparent);
  extractJsonNumberValue(body,"relative_humidity_2m",humidity);
  extractJsonNumberValue(body,"weather_code",weatherCode);
  extractJsonNumberValue(body,"wind_speed_10m",wind);

  String location=
    resolved.length()?resolved:place;

  if(country.length()){
    location+=", "+country;
  }

  answer=
    "In "+location+
    ", it is "+String(temp,1)+
    " C with "+String(weatherCodeText((int)weatherCode))+
    ". It feels like "+String(apparent,1)+
    " C, humidity is about "+String(humidity,0)+
    "%, and wind speed is about "+String(wind,1)+
    " km/h.";

  source="Open-Meteo";
  confidence=98;
  return true;
}


// =====================================================
//                    CURRENCY SOURCE
// =====================================================

String canonicalCurrencyWord(String token){
  token=normalizeInput(token);

  if(token=="usd" || token=="dollar" || token=="dollars" || token=="us dollar" || token=="us dollars") return "USD";
  if(token=="inr" || token=="rupee" || token=="rupees" || token=="indian rupee" || token=="indian rupees") return "INR";
  if(token=="eur" || token=="euro" || token=="euros") return "EUR";
  if(token=="gbp" || token=="pound" || token=="pounds" || token=="pound sterling") return "GBP";
  if(token=="jpy" || token=="yen") return "JPY";
  if(token=="aud" || token=="australian dollar" || token=="australian dollars") return "AUD";
  if(token=="cad" || token=="canadian dollar" || token=="canadian dollars") return "CAD";
  if(token=="chf" || token=="swiss franc" || token=="swiss francs") return "CHF";
  if(token=="cny" || token=="yuan" || token=="renminbi") return "CNY";

  return "";
}


bool collectCurrencyCodes(String q,String& first,String& second){
  q=normalizeInput(q);

  const char* aliases[]={
    "usd","dollar","dollars",
    "inr","rupee","rupees",
    "eur","euro","euros",
    "gbp","pound sterling",
    "jpy","yen",
    "aud","australian dollar",
    "cad","canadian dollar",
    "chf","swiss franc",
    "cny","yuan","renminbi"
  };

  const size_t aliasCount=
    sizeof(aliases)/sizeof(aliases[0]);

  int firstPos=1000000;
  int secondPos=1000000;
  String firstCode="";
  String secondCode="";

  for(size_t i=0;i<aliasCount;i++){
    String alias=aliases[i];
    int pos=q.indexOf(alias);

    if(pos<0) continue;

    String code=canonicalCurrencyWord(alias);

    if(!code.length()) continue;

    if(pos<firstPos){
      if(firstCode.length() && code!=firstCode){
        secondPos=firstPos;
        secondCode=firstCode;
      }

      firstPos=pos;
      firstCode=code;
    }
    else if(
      code!=firstCode &&
      pos<secondPos
    ){
      secondPos=pos;
      secondCode=code;
    }
  }

  if(
    !firstCode.length() ||
    !secondCode.length()
  ){
    return false;
  }

  first=firstCode;
  second=secondCode;
  return true;
}

bool tryFrankfurter(
  const String& question,
  String& answer,
  String& source,
  int& confidence
){
  String from;
  String to;

  if(!collectCurrencyCodes(question,from,to)){
    return false;
  }

  double amount=1.0;
  extractFirstNumber(question,amount);

  Serial.println("[SOURCE] Frankfurter Currency");

  String url=
    "https://api.frankfurter.app/latest"
    "?amount="+String(amount,4)+
    "&from="+from+
    "&to="+to;

  String body;
  int code=0;

  if(!httpsGet(url,body,code)){
    Serial.print("[SOURCE] HTTP ");
    Serial.println(code);
    return false;
  }

  double converted=0;

  if(!extractJsonNumberValue(body,to,converted)){
    return false;
  }

  answer=
    formatNumber(amount)+
    " "+from+
    " is about "+
    formatNumber(converted)+
    " "+to+
    " at the returned exchange rate.";

  source="Frankfurter";
  confidence=98;
  return true;
}


// =====================================================
//                      BOOK SOURCE
// =====================================================

String cleanBookQuery(String q){
  q=normalizeInput(q);

  const char* phrases[]={
    "who is the author of ",
    "who wrote ",
    "author of ",
    "tell me about the book ",
    "tell me about book ",
    "book ",
    "novel "
  };

  for(size_t i=0;i<sizeof(phrases)/sizeof(phrases[0]);i++){
    String p=phrases[i];

    int pos=q.indexOf(p);

    if(pos>=0){
      q=q.substring(pos+p.length());
      break;
    }
  }

  q.trim();
  return q;
}


bool tryOpenLibrary(
  const String& question,
  String& answer,
  String& source,
  int& confidence
){
  String query=cleanBookQuery(question);

  if(query.length()<2){
    return false;
  }

  Serial.println("[SOURCE] Open Library");

  String url=
    "https://openlibrary.org/search.json"
    "?q="+urlEncode(query)+
    "&limit=1"
    "&fields=title,author_name,first_publish_year,key";

  String body;
  int code=0;

  if(!httpsGet(url,body,code)){
    Serial.print("[SOURCE] HTTP ");
    Serial.println(code);
    return false;
  }

  String title=extractJsonString(body,"title");
  String author=extractJsonString(body,"author_name");

  double year=0;
  bool hasYear=extractJsonNumberValue(body,"first_publish_year",year);

  if(title.length()<2){
    return false;
  }

  String q=normalizeInput(question);

  if(
    q.startsWith("who wrote ") ||
    q.indexOf("author of ")>=0
  ){
    if(!author.length()) return false;

    answer=
      title+
      " is listed as being written by "+
      author+
      ".";
  }
  else{
    answer=title;

    if(author.length()){
      answer+=" by "+author;
    }

    if(hasYear){
      answer+=" was first published around "+String((int)year);
    }

    answer+=".";
  }

  source="Open Library";
  confidence=92;
  return true;
}


// =====================================================
//                    RESEARCH SOURCE
// =====================================================

String cleanResearchQuery(String q){
  q=normalizeInput(q);

  const char* phrases[]={
    "research paper about ",
    "paper about ",
    "study about ",
    "journal article about ",
    "doi of "
  };

  for(size_t i=0;i<sizeof(phrases)/sizeof(phrases[0]);i++){
    String p=phrases[i];

    int pos=q.indexOf(p);

    if(pos>=0){
      q=q.substring(pos+p.length());
      break;
    }
  }

  q.trim();
  return q;
}


bool tryCrossref(
  const String& question,
  String& answer,
  String& source,
  int& confidence
){
  String query=cleanResearchQuery(question);

  if(query.length()<3){
    return false;
  }

  Serial.println("[SOURCE] Crossref");

  String url=
    "https://api.crossref.org/works"
    "?query.bibliographic="+urlEncode(query)+
    "&rows=1";

  String body;
  int code=0;

  if(!httpsGet(url,body,code)){
    Serial.print("[SOURCE] HTTP ");
    Serial.println(code);
    return false;
  }

  String title=extractJsonString(body,"title");
  String doi=extractJsonString(body,"DOI");
  String publisher=extractJsonString(body,"publisher");

  title=cleanWebAnswer(title,350,2);

  if(title.length()<5){
    return false;
  }

  answer="A close Crossref match is \""+title+"\"";

  if(publisher.length()){
    answer+=", published by "+publisher;
  }

  if(doi.length()){
    answer+=". DOI: "+doi;
  }

  answer+=".";

  source="Crossref";
  confidence=78;
  return true;
}


// =====================================================
//                  PACKAGE SOURCES
// =====================================================

String cleanPackageName(String q,const char* phrase){
  q=normalizeInput(q);

  int pos=q.indexOf(phrase);

  if(pos>=0){
    q=q.substring(pos+strlen(phrase));
  }

  q.replace("latest version of ","");
  q.replace("latest version","");
  q.replace("version","");
  q.replace("package","");
  q.replace("what is ","");

  q.trim();

  int space=q.indexOf(' ');

  if(space>0){
    q=q.substring(0,space);
  }

  q.trim();
  return q;
}


bool tryPyPI(
  const String& question,
  String& answer,
  String& source,
  int& confidence
){
  String pkg;

  if(normalizeInput(question).indexOf("python package")>=0){
    pkg=cleanPackageName(question,"python package");
  }
  else{
    pkg=cleanPackageName(question,"pypi");
  }

  if(pkg.length()<2){
    return false;
  }

  Serial.println("[SOURCE] PyPI");

  String url=
    "https://pypi.org/pypi/"+
    urlEncode(pkg)+
    "/json";

  String body;
  int code=0;

  if(!httpsGet(url,body,code)){
    Serial.print("[SOURCE] HTTP ");
    Serial.println(code);
    return false;
  }

  String name=extractJsonString(body,"name");
  String version=extractJsonString(body,"version");
  String summary=extractJsonString(body,"summary");

  summary=cleanWebAnswer(summary,420,2);

  if(name.length()<2){
    return false;
  }

  answer=name;

  if(version.length()){
    answer+=" version "+version;
  }

  if(summary.length()){
    answer+=" — "+summary;
  }

  if(!answer.endsWith(".")){
    answer+=".";
  }

  source="PyPI";
  confidence=98;
  return true;
}


bool tryNpm(
  const String& question,
  String& answer,
  String& source,
  int& confidence
){
  String pkg=cleanPackageName(question,"npm");

  if(pkg.length()<2){
    return false;
  }

  Serial.println("[SOURCE] npm Registry");

  String url=
    "https://registry.npmjs.org/"+
    urlEncode(pkg)+
    "/latest";

  String body;
  int code=0;

  if(!httpsGet(url,body,code)){
    Serial.print("[SOURCE] HTTP ");
    Serial.println(code);
    return false;
  }

  String name=extractJsonString(body,"name");
  String version=extractJsonString(body,"version");
  String description=extractJsonString(body,"description");

  description=cleanWebAnswer(description,420,2);

  if(name.length()<2){
    return false;
  }

  answer=name;

  if(version.length()){
    answer+=" version "+version;
  }

  if(description.length()){
    answer+=" — "+description;
  }

  if(!answer.endsWith(".")){
    answer+=".";
  }

  source="npm Registry";
  confidence=98;
  return true;
}


// =====================================================
//                    CHEMISTRY SOURCE
// =====================================================

String cleanChemistryQuery(String q){
  q=normalizeInput(q);

  const char* phrases[]={
    "molecular formula of ",
    "molecular weight of ",
    "chemical formula of ",
    "iupac name of ",
    "pubchem ",
    "chemical compound "
  };

  for(size_t i=0;i<sizeof(phrases)/sizeof(phrases[0]);i++){
    String p=phrases[i];

    int pos=q.indexOf(p);

    if(pos>=0){
      q=q.substring(pos+p.length());
      break;
    }
  }

  q.trim();
  return q;
}


bool tryPubChem(
  const String& question,
  String& answer,
  String& source,
  int& confidence
){
  String compound=cleanChemistryQuery(question);

  if(compound.length()<1 || compound.length()>80){
    return false;
  }

  Serial.println("[SOURCE] PubChem");

  String url=
    "https://pubchem.ncbi.nlm.nih.gov/rest/pug/compound/name/"+
    urlEncode(compound)+
    "/property/MolecularFormula,MolecularWeight,IUPACName/JSON";

  String body;
  int code=0;

  if(!httpsGet(url,body,code)){
    Serial.print("[SOURCE] HTTP ");
    Serial.println(code);
    return false;
  }

  String formula=extractJsonString(body,"MolecularFormula");
  String iupac=extractJsonString(body,"IUPACName");

  double weight=0;
  bool hasWeight=extractJsonNumberValue(body,"MolecularWeight",weight);

  if(!hasWeight){
    String weightText=
      extractJsonString(
        body,
        "MolecularWeight"
      );

    if(weightText.length()){
      weight=weightText.toDouble();
      hasWeight=weight>0;
    }
  }

  if(!formula.length() && !iupac.length() && !hasWeight){
    return false;
  }

  String q=normalizeInput(question);

  if(q.indexOf("molecular formula")>=0 || q.indexOf("chemical formula")>=0){
    if(!formula.length()) return false;

    answer=
      "The molecular formula of "+
      compound+
      " is "+
      formula+
      ".";
  }
  else if(q.indexOf("molecular weight")>=0){
    if(!hasWeight) return false;

    answer=
      "The molecular weight of "+
      compound+
      " is about "+
      formatNumber(weight)+
      " g/mol.";
  }
  else{
    answer=compound;

    if(formula.length()){
      answer+=" has molecular formula "+formula;
    }

    if(hasWeight){
      answer+=" and molecular weight about "+formatNumber(weight)+" g/mol";
    }

    if(iupac.length()){
      answer+=". Its IUPAC name is "+iupac;
    }

    answer+=".";
  }

  source="PubChem";
  confidence=97;
  return true;
}


// =====================================================
//                 SOURCE SENSE WEB BRAIN
// =====================================================

void emitWebAnswer(
  const String& answer,
  const String& source,
  int confidence
){
  // Any successful knowledge answer becomes short-term dialogue
  // context for follow-up pronouns such as "it", "this" and "that".
  lastKnowledgeAnswer=
    answer;

  Serial.println();
  Serial.println("====================================");

  Serial.print("[SOURCE] ");
  Serial.println(source);

  Serial.print("[CONFIDENCE] ");
  Serial.print(confidence);
  Serial.println("/100");

  Serial.println();

  elliSay(answer);

  Serial.println("====================================");
}


bool tryGeneralKnowledgePool(
  const String& question,
  String& answer,
  String& source,
  int& confidence
){
  // Direct answer first, then evidence-scored encyclopedia search.
  // Generic Wikidata descriptions come after semantic Wikipedia because
  // a broad entity description should not beat a narrower question.
  if(tryDuck(question,answer,source,confidence)) return true;
  if(tryWikipedia(question,answer,source,confidence)) return true;
  if(tryWikidata(question,answer,source,confidence)) return true;
  if(trySimpleWiki(question,answer,source,confidence)) return true;
  if(tryWiktionary(question,answer,source,confidence)) return true;

  return false;
}



// =====================================================
//                WEB-BASED RANDOM FACTS
// =====================================================

static String lastWebFactTitle="";

bool fetchWebFactPage(
  const String& host,
  const String& label,
  const String& title,
  const String& topic,
  String& answer,
  String& source,
  int& confidence
){
  String url=
    "https://"+host+
    "/w/api.php"
    "?action=query"
    "&prop=extracts"
    "&exintro=1"
    "&explaintext=1"
    "&exsentences=2"
    "&redirects=1"
    "&format=json"
    "&formatversion=2"
    "&titles="+urlEncode(title);

  String body;
  int code=0;

  if(!httpsGet(url,body,code)) return false;

  String resolved=extractJsonString(body,"title");
  String extract=cleanWebAnswer(extractJsonString(body,"extract"),430,2);

  if(
    extract.length()<45 ||
    looksLikeDisambiguationText(extract) ||
    resolved==lastWebFactTitle
  ){
    return false;
  }

  if(topic.length()){
    int score=semanticEvidenceScore(topic,resolved,extract);
    if(score<28) return false;
  }

  lastWebFactTitle=resolved;
  answer="Here's a web fact about "+resolved+": "+extract;
  source=label+": "+resolved;
  confidence=82;
  return true;
}


bool webRandomFact(
  const String& requestedTopic,
  String& answer,
  String& source,
  int& confidence
){
  if(WiFi.status()!=WL_CONNECTED){
    return false;
  }

  String topic=normalizeInput(requestedTopic);
  topic.trim();

  // Topic-specific fact: search several related pages, then choose one
  // of the top candidates. General fact: ask MediaWiki for random pages.
  if(topic.length()){
    String url=
      "https://en.wikipedia.org/w/api.php"
      "?action=query"
      "&list=search"
      "&srnamespace=0"
      "&srlimit=8"
      "&format=json"
      "&utf8=1"
      "&srsearch="+urlEncode(topic);

    String body;
    int code=0;

    if(httpsGet(url,body,code)){
      String titles[6];
      int count=0;
      int pos=0;

      while(count<6){
        String title;
        int next=0;
        if(!extractJsonStringAfter(body,"title",pos,title,next)) break;
        pos=next;

        if(
          title.length() &&
          title!=lastWebFactTitle
        ){
          titles[count++]=title;
        }
      }

      if(count){
        int start=random(count);

        for(int n=0;n<count;n++){
          int idx=(start+n)%count;

          if(fetchWebFactPage(
            "en.wikipedia.org","Wikipedia",
            titles[idx],topic,answer,source,confidence
          )){
            return true;
          }
        }
      }
    }
  }
  else{
    const char* hosts[]={"en.wikipedia.org","simple.wikipedia.org"};
    const char* labels[]={"Wikipedia","Simple Wikipedia"};

    for(int h=0;h<2;h++){
      String url=
        "https://"+String(hosts[h])+
        "/w/api.php"
        "?action=query"
        "&generator=random"
        "&grnnamespace=0"
        "&grnlimit=4"
        "&prop=extracts"
        "&exintro=1"
        "&explaintext=1"
        "&exsentences=2"
        "&format=json"
        "&formatversion=2";

      String body;
      int code=0;

      if(!httpsGet(url,body,code)) continue;

      int pos=0;

      for(int i=0;i<4;i++){
        String title;
        String extract;
        int nextTitle=0;
        int nextExtract=0;

        if(!extractJsonStringAfter(body,"title",pos,title,nextTitle)) break;
        if(!extractJsonStringAfter(body,"extract",nextTitle,extract,nextExtract)) break;
        pos=nextExtract;

        extract=cleanWebAnswer(extract,430,2);

        if(
          title.length() &&
          title!=lastWebFactTitle &&
          extract.length()>=45 &&
          !looksLikeDisambiguationText(extract)
        ){
          lastWebFactTitle=title;
          answer="Here's a web fact about "+title+": "+extract;
          source=String(labels[h])+": "+title;
          confidence=78;
          return true;
        }
      }
    }
  }

  return false;
}


void webBrain(const String& question){
  Serial.println();
  Serial.println("========== KIRA SOURCE SENSE WEB BRAIN v0.9.0 ==========");

  if(WiFi.status()!=WL_CONNECTED){
    KiraCacheRecord cached;

    if(kiraCacheLookup(question,cached,true)){
      String age=kiraCacheAgeText(cached.storedEpoch);
      emitWebAnswer(
        "My internet is unavailable, so this is stored knowledge from "+age+": "+cached.answer,
        "OFFLINE CACHE: "+cached.source,
        max(30,cached.confidence-(cached.stale?15:5))
      );
      return;
    }

    elliSay("I can't reach the internet, and I don't have a stored answer for that question yet.");
    return;
  }

  elliState=ELLI_SEARCHING;

  String cleaned=cleanKnowledgeQuery(question);
  int intent=classifyWebIntent(question);
  SemanticFrame semanticFrame=analyzeSemanticQuery(question);

  lastKnowledgeQuery=question;

  printSemanticFrame(semanticFrame);

  Serial.print("[WEB QUERY] ");
  Serial.println(cleaned);

  Serial.print("[SENSE] ");
  Serial.println(webIntentName(intent));

  String answer;
  String source;
  int confidence=0;
  bool found=false;

  switch(intent){

    case WEB_INTENT_WORD:
    {
      String term=cleanKnowledgeQuery(question);
      String variants[8];
      int variantCount=buildConceptVariants(term,variants,8);
      bool compound=semanticWordCount(term)>=2;

      Serial.print("[DEFINITION] ");
      Serial.println(compound ? "COMPOUND CONCEPT" : "COMMON WORD");

      if(compound){
        // Generic concept resolution: original phrase + joined/hyphenated
        // variants. No domain-specific word replacement table.
        for(int i=0;i<variantCount && !found;i++){
          found=tryWikidataExactWord(variants[i],answer,source,confidence);
        }

        for(int i=0;i<variantCount && !found;i++){
          found=tryWikipediaCommonWord(variants[i],answer,source,confidence);
        }

        if(!found){
          found=tryDuck(question,answer,source,confidence);
        }

        for(int i=0;i<variantCount && !found;i++){
          found=trySimpleWikiCommonWord(variants[i],answer,source,confidence);
        }
      }
      else{
        // Common-word path keeps the protection against irrelevant/
        // archaic first senses such as the earlier tumbler problem.
        found=tryWikipediaCommonWord(question,answer,source,confidence);

        if(!found) found=tryDictionaryApi(question,answer,source,confidence);
        if(!found) found=tryWikidataExactWord(question,answer,source,confidence);
        if(!found) found=trySimpleWikiCommonWord(question,answer,source,confidence);
        if(!found) found=tryWiktionary(question,answer,source,confidence);
        if(!found) found=tryDuck(question,answer,source,confidence);
      }

      break;
    }


    case WEB_INTENT_WEATHER:
      found=tryOpenMeteo(question,answer,source,confidence);

      if(!found){
        elliSay(
          "I recognized that as a weather question, but I need a place name such as 'weather in Yavatmal'."
        );
        return;
      }

      break;


    case WEB_INTENT_CURRENCY:
      found=tryFrankfurter(question,answer,source,confidence);

      if(!found){
        found=tryGeneralKnowledgePool(
          question,
          answer,
          source,
          confidence
        );
      }

      break;


    case WEB_INTENT_BOOK:
      found=tryOpenLibrary(question,answer,source,confidence);

      if(!found){
        found=tryGeneralKnowledgePool(
          question,
          answer,
          source,
          confidence
        );
      }

      break;


    case WEB_INTENT_RESEARCH:
      found=tryCrossref(question,answer,source,confidence);

      if(!found){
        found=tryWikipedia(question,answer,source,confidence);
      }

      if(!found){
        found=tryWikidata(question,answer,source,confidence);
      }

      break;


    case WEB_INTENT_PYTHON:
      found=tryPyPI(question,answer,source,confidence);

      if(!found){
        found=tryGeneralKnowledgePool(
          question,
          answer,
          source,
          confidence
        );
      }

      break;


    case WEB_INTENT_NPM:
      found=tryNpm(question,answer,source,confidence);

      if(!found){
        found=tryGeneralKnowledgePool(
          question,
          answer,
          source,
          confidence
        );
      }

      break;


    case WEB_INTENT_CHEMISTRY:
      found=tryPubChem(question,answer,source,confidence);

      if(!found){
        found=tryWikipedia(question,answer,source,confidence);
      }

      if(!found){
        found=tryWikidata(question,answer,source,confidence);
      }

      break;


    case WEB_INTENT_GEOGRAPHY:
      found=tryRestCountries(question,answer,source,confidence);

      if(!found){
        found=tryWikidata(question,answer,source,confidence);
      }

      if(!found){
        found=tryWikipedia(question,answer,source,confidence);
      }

      if(!found){
        found=tryDuck(question,answer,source,confidence);
      }

      break;


    case WEB_INTENT_PERSON:
    case WEB_INTENT_COMPANY:
      found=tryWikidata(question,answer,source,confidence);

      if(!found){
        found=tryWikipedia(question,answer,source,confidence);
      }

      if(!found){
        found=tryDuck(question,answer,source,confidence);
      }

      if(!found){
        found=trySimpleWiki(question,answer,source,confidence);
      }

      break;


    case WEB_INTENT_SCIENCE:
    case WEB_INTENT_ASTRONOMY:
      found=tryWikipedia(question,answer,source,confidence);

      if(!found){
        found=tryWikidata(question,answer,source,confidence);
      }

      if(!found){
        found=trySimpleWiki(question,answer,source,confidence);
      }

      if(!found){
        found=tryDuck(question,answer,source,confidence);
      }

      break;


    case WEB_INTENT_NEWS:
      // We intentionally do not pretend that encyclopedia sources
      // are a live-news engine. Current-news connectors come in the
      // next source phase.
      elliSay(
        "I recognized this as a current-news question. My live-news source pool isn't connected in this phase yet, so I won't guess using an encyclopedia."
      );
      return;


    default:
      found=tryGeneralKnowledgePool(
        question,
        answer,
        source,
        confidence
      );
      break;
  }

  if(found){
    answer=cleanWebAnswer(answer,650,4);

    if(answer.length()){
      lastKnowledgeAnswer=answer;

      emitWebAnswer(
        answer,
        source,
        confidence
      );

      // Save verified web knowledge for later offline use. Dynamic questions
      // receive shorter TTLs; static concepts remain useful much longer.
      kiraCacheStore(
        question,
        answer,
        source,
        confidence,
        kiraDefaultCacheTtl(question)
      );

      return;
    }
  }

  elliSay(
    "I couldn't find a source that matches the question closely enough. Try rephrasing it with the main subject and the exact fact you want."
  );
}

String deviceReply(const String& dev,bool state){
  const char* A[]={"Done, ","Sure, ","Got it, ","Okay, ","Alright, ","Yep, "};
  const char* B[]={"I've turned ","switching ","setting ","putting ","I turned ","I've set "};
  const char* C[]={" on."," on!"," to ON."," on now."," on for you."};
  const char* D[]={" off."," off!"," to OFF."," off now."," off for you."};
  static uint32_t last=0xFFFFFFFF;
  int a=random(6),b=random(6),c=random(5);
  uint32_t sig=a*10000UL+b*100UL+c; if(sig==last) c=(c+1)%5; last=a*10000UL+b*100UL+c;
  return String(A[a])+B[b]+"the "+dev+(state?C[c]:D[c]);
}

void printMemory(){
  Serial.println(); Serial.println("========== MEMORY ==========");
  Serial.print("Heap total   : "); Serial.println(ESP.getHeapSize());
  Serial.print("Heap free    : "); Serial.println(ESP.getFreeHeap());
  Serial.print("Min heap     : "); Serial.println(ESP.getMinFreeHeap());
  Serial.print("PSRAM total  : "); Serial.println(ESP.getPsramSize());
  Serial.print("PSRAM free   : "); Serial.println(ESP.getFreePsram());
}

void printStatus(){
  Serial.println(); Serial.println("========== KIRA STATUS ==========");
  Serial.print("CPU        : "); Serial.print(ESP.getCpuFreqMHz()); Serial.println(" MHz");
  Serial.print("Free RAM   : "); Serial.print(ESP.getFreeHeap()/1024.0,1); Serial.println(" KB");
  Serial.print("Free PSRAM : "); Serial.print(ESP.getFreePsram()/(1024.0*1024.0),2); Serial.println(" MB");
  Serial.print("Renderer   : "); Serial.print(measuredFPS); Serial.println(" FPS");
  Serial.print("Wi-Fi      : "); Serial.println(WiFi.status()==WL_CONNECTED?"CONNECTED":"DISCONNECTED");
  if(WiFi.status()==WL_CONNECTED){ Serial.print("RSSI       : "); Serial.print(WiFi.RSSI()); Serial.println(" dBm"); }
  Serial.print("Time       : "); Serial.println(currentTimeString());
  Serial.print("Stopwatch  : "); Serial.println(stopwatchRunning ? "RUNNING" : (stopwatchStoredMs > 0 ? "STOPPED" : "READY"));
  Serial.print("Timer      : "); Serial.println(timerRinging ? "RINGING" : (timerPaused ? "PAUSED" : (timerActive ? "RUNNING" : "OFF")));
  Serial.print("Alarms     : ");
  if(alarmRinging){
    Serial.print("RINGING + ");
    Serial.print(activeAlarmCount());
    Serial.println(" waiting");
  }else{
    Serial.print(activeAlarmCount());
    Serial.println(" saved");
  }
  Serial.print("Light      : "); Serial.println(onOff(mainLight));
  Serial.print("Fan        : "); Serial.println(onOff(fanState));
  Serial.print("Charger    : "); Serial.println(onOff(chargerState));
  Serial.print("Light 2    : "); Serial.println(onOff(secondLight));
}

static bool deviceActionPrefix(String q){
  q=normalizeInput(q);

  const char* const starts[]={
    "turn ","switch ","power ","start ","stop ",
    "enable ","disable ","activate ","deactivate ",
    "please turn ","please switch ","please power ",
    "can you turn ","can you switch ",
    "could you turn ","could you switch ",
    "would you turn ","would you switch ",
    "actually turn ","actually switch ","actually power "
  };

  for(size_t i=0;i<sizeof(starts)/sizeof(starts[0]);i++){
    if(q.startsWith(starts[i])) return true;
  }

  return false;
}


bool wantsOn(const String& input){
  String q=normalizeInput(input);

  if(
    q.startsWith("start ") ||
    q.startsWith("enable ") ||
    q.startsWith("activate ")
  ){
    return true;
  }

  return
    deviceActionPrefix(q) &&
    elliBoundedToken(q,"on");
}

bool wantsOff(const String& input){
  String q=normalizeInput(input);

  if(
    q.startsWith("stop ") ||
    q.startsWith("disable ") ||
    q.startsWith("deactivate ")
  ){
    return true;
  }

  return
    deviceActionPrefix(q) &&
    elliBoundedToken(q,"off");
}


static String lastDeviceTarget="";
static uint32_t lastDeviceContextMs=0;
static const uint32_t DEVICE_CONTEXT_TTL_MS=5UL*60UL*1000UL;


static void noteDeviceContext(const String& target){
  lastDeviceTarget=target;
  lastDeviceContextMs=millis();
}


static bool deviceContextLive(){
  return
    lastDeviceTarget.length() &&
    millis()-lastDeviceContextMs<=DEVICE_CONTEXT_TTL_MS;
}


static bool asksDeviceState(String q){
  q=normalizeInput(q);

  if(
    q.indexOf(" status")>=0 ||
    q.endsWith(" status") ||
    q.indexOf(" state")>=0 ||
    q.endsWith(" state")
  ){
    return true;
  }

  if(
    q.startsWith("is ") &&
    (q.endsWith(" on") || q.endsWith(" off"))
  ){
    return true;
  }

  return false;
}


static bool resolveRecentDevicePronoun(String& q){
  if(!deviceContextLive()){
    return false;
  }

  String n=normalizeInput(q);

  bool hasExplicitDevice=
    n.indexOf("fan")>=0 ||
    n.indexOf("charger")>=0 ||
    n.indexOf("light")>=0 ||
    n.indexOf("all devices")>=0 ||
    n.indexOf("everything")>=0;

  if(hasExplicitDevice){
    return false;
  }

  bool pronoun=
    elliBoundedToken(n,"it") ||
    elliBoundedToken(n,"that") ||
    elliBoundedToken(n,"this");

  bool on=
    wantsOn(n) ||
    n.indexOf("back on")>=0;

  bool off=
    wantsOff(n) ||
    n.indexOf("back off")>=0;

  if(!pronoun || (!on && !off)){
    return false;
  }

  q=
    String(on ? "turn on " : "turn off ")+
    lastDeviceTarget;

  Serial.print("[DEVICE CONTEXT] Pronoun resolved -> ");
  Serial.println(q);

  return true;
}


static bool handleDeviceStateQuery(String q){
  q=normalizeInput(q);

  if(!asksDeviceState(q)){
    return false;
  }

  if(
    q.indexOf("second light")>=0 ||
    q.indexOf("light 2")>=0 ||
    q.indexOf("light two")>=0
  ){
    noteDeviceContext("second light");
    elliSay(String("The second light is ")+onOff(secondLight)+".");
    return true;
  }

  if(q.indexOf("fan")>=0){
    noteDeviceContext("fan");
    elliSay(String("The fan is ")+onOff(fanState)+".");
    return true;
  }

  if(
    q.indexOf("charger")>=0 ||
    q.indexOf("charging")>=0
  ){
    noteDeviceContext("charger");
    elliSay(String("The charger is ")+onOff(chargerState)+".");
    return true;
  }

  if(q.indexOf("light")>=0){
    noteDeviceContext("light");
    elliSay(String("The main light is ")+onOff(mainLight)+".");
    return true;
  }

  return false;
}


// =====================================================
//                 UNIVERSAL LOCAL SOLVERS
// =====================================================
//
// These modules handle questions that do not need a
// web lookup: arithmetic, unit conversions, date/time,
// and a few device/system intents.
//
// =====================================================


// ---------------- MATH EXPRESSION PARSER ----------------

class TinyMathParser {
public:
  explicit TinyMathParser(const String& input)
    : s(input), pos(0), ok(true) {}

  double parse() {
    double v = parseExpression();
    skipSpaces();

    if (pos != s.length()) {
      ok = false;
    }

    return v;
  }

  bool success() const {
    return ok;
  }

private:
  String s;
  size_t pos;
  bool ok;

  void skipSpaces() {
    while (pos < s.length() && isspace((unsigned char)s[pos])) {
      pos++;
    }
  }

  bool match(char c) {
    skipSpaces();

    if (pos < s.length() && s[pos] == c) {
      pos++;
      return true;
    }

    return false;
  }

  bool matchWord(const char* word) {
    skipSpaces();

    size_t len = strlen(word);

    if (pos + len > s.length()) {
      return false;
    }

    for (size_t i = 0; i < len; i++) {
      char a = tolower((unsigned char)s[pos + i]);
      char b = tolower((unsigned char)word[i]);

      if (a != b) {
        return false;
      }
    }

    pos += len;
    return true;
  }

  double parseExpression() {
    double v = parseTerm();

    while (ok) {
      if (match('+')) {
        v += parseTerm();
      } else if (match('-')) {
        v -= parseTerm();
      } else {
        break;
      }
    }

    return v;
  }

  double parseTerm() {
    double v = parsePower();

    while (ok) {
      if (match('*')) {
        v *= parsePower();
      } else if (match('/')) {
        double d = parsePower();

        if (fabs(d) < 1e-12) {
          ok = false;
          return 0;
        }

        v /= d;
      } else {
        break;
      }
    }

    return v;
  }

  double parsePower() {
    double v = parseUnary();

    if (match('^')) {
      double exponent = parsePower();
      v = pow(v, exponent);
    }

    return v;
  }

  double parseUnary() {
    skipSpaces();

    if (match('+')) {
      return parseUnary();
    }

    if (match('-')) {
      return -parseUnary();
    }

    if (matchWord("sqrt")) {
      if (match('(')) {
        double v = parseExpression();

        if (!match(')') || v < 0) {
          ok = false;
          return 0;
        }

        return sqrt(v);
      }

      double v = parseUnary();

      if (v < 0) {
        ok = false;
        return 0;
      }

      return sqrt(v);
    }

    return parsePrimary();
  }

  double parsePrimary() {
    skipSpaces();

    if (match('(')) {
      double v = parseExpression();

      if (!match(')')) {
        ok = false;
      }

      return v;
    }

    return parseNumber();
  }

  double parseNumber() {
    skipSpaces();

    if (pos >= s.length()) {
      ok = false;
      return 0;
    }

    size_t start = pos;
    bool sawDigit = false;
    bool sawDot = false;

    while (pos < s.length()) {
      char c = s[pos];

      if (isdigit((unsigned char)c)) {
        sawDigit = true;
        pos++;
      } else if (c == '.' && !sawDot) {
        sawDot = true;
        pos++;
      } else {
        break;
      }
    }

    if (!sawDigit) {
      ok = false;
      return 0;
    }

    return s.substring(start, pos).toDouble();
  }
};


String formatNumber(double value) {
  if (isnan(value) || isinf(value)) {
    return "undefined";
  }

  if (fabs(value - round(value)) < 0.0000005 &&
      fabs(value) < 2147483647.0) {
    return String((long)round(value));
  }

  String out = String(value, 6);

  while (out.endsWith("0")) {
    out.remove(out.length() - 1);
  }

  if (out.endsWith(".")) {
    out.remove(out.length() - 1);
  }

  return out;
}


String normalizeMathExpression(String q) {
  // Keep decimal points for calculations.
  q.trim();
  q.toLowerCase();
  q.replace("?", "");
  q.replace("!", "");
  q.replace(",", "");
  q.replace(":", "");
  q.replace(";", "");
  q.replace("/", " / ");

  while (q.indexOf("  ") >= 0) {
    q.replace("  ", " ");
  }

  const char* prefixes[] = {
    "what is ",
    "calculate ",
    "compute ",
    "solve ",
    "work out ",
    "find "
  };

  for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
    String p = prefixes[i];

    if (q.startsWith(p)) {
      q.remove(0, p.length());
      break;
    }
  }

  q.replace("multiplied by", "*");
  q.replace("multiply by", "*");
  q.replace("times", "*");
  q.replace(" x ", " * ");
  q.replace("divided by", "/");
  q.replace("divide by", "/");
  q.replace("over", "/");
  q.replace("plus", "+");
  q.replace("minus", "-");
  q.replace("to the power of", "^");
  q.replace("power of", "^");
  q.replace("squared", "^2");
  q.replace("cubed", "^3");
  q.replace("square root of", "sqrt ");
  q.replace("square root", "sqrt ");

  while (q.indexOf("  ") >= 0) {
    q.replace("  ", " ");
  }

  q.trim();

  return q;
}


bool looksLikeMathQuestion(const String& original) {
  String q = normalizeInput(original);

  if (
    q.startsWith("calculate ") ||
    q.startsWith("compute ") ||
    q.startsWith("solve ") ||
    q.startsWith("work out ")
  ) {
    return true;
  }

  bool hasDigit = false;

  for (size_t i = 0; i < q.length(); i++) {
    if (isdigit((unsigned char)q[i])) {
      hasDigit = true;
      break;
    }
  }

  if (!hasDigit) {
    return false;
  }

  const char* clues[] = {
    "+", "-", "*", "/", "^",
    " plus ", " minus ", " times ",
    " multiplied by ", " divided by ",
    "square root", " squared", " cubed", " percent of ", "% of "
  };

  return containsAny(
    q,
    clues,
    sizeof(clues) / sizeof(clues[0])
  );
}


bool parseSimpleNumber(String text,double& value){
  text.trim();
  if(!text.length()) return false;
  bool dot=false;
  size_t start=0;
  if(text[0]=='+' || text[0]=='-') start=1;
  if(start>=text.length()) return false;
  for(size_t i=start;i<text.length();i++){
    char c=text[i];
    if(isdigit((unsigned char)c)) continue;
    if(c=='.' && !dot){ dot=true; continue; }
    return false;
  }
  value=text.toDouble();
  return true;
}


bool tryPercentOf(const String& question,double& result){
  String q=question;
  q.trim();
  q.toLowerCase();
  q.replace("?",""); q.replace("!",""); q.replace(",","");

  const char* prefixes[]={"what is ","calculate ","compute ","solve ","work out ","find "};
  for(size_t i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);i++){
    String p=prefixes[i];
    if(q.startsWith(p)){ q.remove(0,p.length()); break; }
  }

  q.replace("% of "," percent of ");
  int pos=q.indexOf(" percent of ");
  if(pos<0) return false;

  String left=q.substring(0,pos);
  String right=q.substring(pos+12);
  left.trim(); right.trim();

  double percent=0,base=0;
  if(!parseSimpleNumber(left,percent) || !parseSimpleNumber(right,base)) return false;
  result=(percent/100.0)*base;
  return true;
}


bool tryLocalMath(const String& question) {
  if (!looksLikeMathQuestion(question)) {
    return false;
  }

  double percentResult=0;
  if(tryPercentOf(question,percentResult)){
    elliSay("The answer is "+formatNumber(percentResult)+".");
    return true;
  }

  String expression =
    normalizeMathExpression(question);

  TinyMathParser parser(expression);

  double result =
    parser.parse();

  if (!parser.success()) {
    return false;
  }

  elliSay(
    "The answer is " +
    formatNumber(result) +
    "."
  );

  return true;
}


// ---------------- UNIT CONVERSION ----------------

enum UnitFamily {
  UNIT_NONE,
  UNIT_LENGTH,
  UNIT_MASS,
  UNIT_VOLUME,
  UNIT_TIME,
  UNIT_TEMPERATURE,
  UNIT_DATA
};


String canonicalUnit(String u) {
  u = normalizeInput(u);

  if (u.endsWith("s") && u.length() > 2) {
    if (
      u != "ms" &&
      u != "lbs"
    ) {
      u.remove(u.length() - 1);
    }
  }

  if (u == "meter" || u == "metre") return "m";
  if (u == "kilometer" || u == "kilometre") return "km";
  if (u == "centimeter" || u == "centimetre") return "cm";
  if (u == "millimeter" || u == "millimetre") return "mm";

  if (u == "inch") return "in";
  if (u == "foot" || u == "feet") return "ft";
  if (u == "yard") return "yd";
  if (u == "mile") return "mi";

  if (u == "gram") return "g";
  if (u == "kilogram") return "kg";
  if (u == "milligram") return "mg";
  if (u == "pound" || u == "lb" || u == "lbs") return "lb";
  if (u == "ounce") return "oz";

  if (u == "liter" || u == "litre") return "l";
  if (u == "milliliter" || u == "millilitre") return "ml";
  if (u == "cup") return "cup";

  if (u == "second" || u == "sec") return "s";
  if (u == "minute" || u == "min") return "min";
  if (u == "hour" || u == "hr") return "h";
  if (u == "day") return "day";

  // Data-size conversions use 1024-based computer units.
  if (u == "byte" || u == "b") return "b";
  if (u == "kilobyte" || u == "kb") return "kb";
  if (u == "megabyte" || u == "mb") return "mb";
  if (u == "gigabyte" || u == "gb") return "gb";
  if (u == "terabyte" || u == "tb") return "tb";

  if (
    u == "celsius" ||
    u == "degree celsius" ||
    u == "degrees celsius" ||
    u == "c"
  ) return "c";

  if (
    u == "fahrenheit" ||
    u == "degree fahrenheit" ||
    u == "degrees fahrenheit" ||
    u == "f"
  ) return "f";

  if (
    u == "kelvin" ||
    u == "k"
  ) return "k";

  return u;
}


bool getUnitInfo(
  String unit,
  int& family,
  double& scaleToBase
) {
  unit = canonicalUnit(unit);

  family = UNIT_NONE;
  scaleToBase = 0.0;

  if (unit == "mm") { family = UNIT_LENGTH; scaleToBase = 0.001; return true; }
  if (unit == "cm") { family = UNIT_LENGTH; scaleToBase = 0.01; return true; }
  if (unit == "m")  { family = UNIT_LENGTH; scaleToBase = 1.0; return true; }
  if (unit == "km") { family = UNIT_LENGTH; scaleToBase = 1000.0; return true; }
  if (unit == "in") { family = UNIT_LENGTH; scaleToBase = 0.0254; return true; }
  if (unit == "ft") { family = UNIT_LENGTH; scaleToBase = 0.3048; return true; }
  if (unit == "yd") { family = UNIT_LENGTH; scaleToBase = 0.9144; return true; }
  if (unit == "mi") { family = UNIT_LENGTH; scaleToBase = 1609.344; return true; }

  if (unit == "mg") { family = UNIT_MASS; scaleToBase = 0.001; return true; }
  if (unit == "g")  { family = UNIT_MASS; scaleToBase = 1.0; return true; }
  if (unit == "kg") { family = UNIT_MASS; scaleToBase = 1000.0; return true; }
  if (unit == "oz") { family = UNIT_MASS; scaleToBase = 28.349523125; return true; }
  if (unit == "lb") { family = UNIT_MASS; scaleToBase = 453.59237; return true; }

  if (unit == "ml")  { family = UNIT_VOLUME; scaleToBase = 1.0; return true; }
  if (unit == "l")   { family = UNIT_VOLUME; scaleToBase = 1000.0; return true; }
  if (unit == "cup") { family = UNIT_VOLUME; scaleToBase = 236.5882365; return true; }

  if (unit == "s")   { family = UNIT_TIME; scaleToBase = 1.0; return true; }
  if (unit == "min") { family = UNIT_TIME; scaleToBase = 60.0; return true; }
  if (unit == "h")   { family = UNIT_TIME; scaleToBase = 3600.0; return true; }
  if (unit == "day") { family = UNIT_TIME; scaleToBase = 86400.0; return true; }

  if (unit == "b")  { family = UNIT_DATA; scaleToBase = 1.0; return true; }
  if (unit == "kb") { family = UNIT_DATA; scaleToBase = 1024.0; return true; }
  if (unit == "mb") { family = UNIT_DATA; scaleToBase = 1024.0*1024.0; return true; }
  if (unit == "gb") { family = UNIT_DATA; scaleToBase = 1024.0*1024.0*1024.0; return true; }
  if (unit == "tb") { family = UNIT_DATA; scaleToBase = 1024.0*1024.0*1024.0*1024.0; return true; }

  if (unit == "c" || unit == "f" || unit == "k") {
    family = UNIT_TEMPERATURE;
    scaleToBase = 1.0;
    return true;
  }

  return false;
}


bool parseConversionQuestion(
  String q,
  double& value,
  String& fromUnit,
  String& toUnit
) {
  // Keep decimal points in numeric values.
  q.trim();
  q.toLowerCase();
  q.replace("?", "");
  q.replace("!", "");
  q.replace(",", "");
  q.replace(":", "");
  q.replace(";", "");

  while (q.indexOf("  ") >= 0) {
    q.replace("  ", " ");
  }

  if (q.startsWith("convert ")) {
    q.remove(0, 8);
  }

  if (q.startsWith("how many ")) {
    return false;
  }

  int toPos = q.indexOf(" to ");

  if (toPos < 0) {
    int inPos = q.indexOf(" in ");

    if (inPos >= 0) {
      toPos = inPos;
    }
  }

  if (toPos < 0) {
    return false;
  }

  String left =
    q.substring(0, toPos);

  String right =
    q.substring(toPos + 4);

  left.trim();
  right.trim();

  int firstSpace =
    left.indexOf(' ');

  if (firstSpace < 0) {
    return false;
  }

  String numberText =
    left.substring(0, firstSpace);

  fromUnit =
    left.substring(firstSpace + 1);

  toUnit =
    right;

  numberText.trim();
  fromUnit.trim();
  toUnit.trim();

  if (!numberText.length() ||
      !fromUnit.length() ||
      !toUnit.length()) {
    return false;
  }

  bool numeric = true;
  bool dot = false;
  size_t start = 0;

  if (numberText[0] == '-' || numberText[0] == '+') {
    start = 1;
  }

  for (size_t i = start; i < numberText.length(); i++) {
    char c = numberText[i];

    if (isdigit((unsigned char)c)) {
      continue;
    }

    if (c == '.' && !dot) {
      dot = true;
      continue;
    }

    numeric = false;
    break;
  }

  if (!numeric) {
    return false;
  }

  value =
    numberText.toDouble();

  return true;
}


bool tryUnitConversion(const String& question) {
  double value = 0;
  String fromUnit;
  String toUnit;

  if (
    !parseConversionQuestion(
      question,
      value,
      fromUnit,
      toUnit
    )
  ) {
    return false;
  }

  String from =
    canonicalUnit(fromUnit);

  String to =
    canonicalUnit(toUnit);

  int familyA = UNIT_NONE;
  int familyB = UNIT_NONE;
  double scaleA = 0.0;
  double scaleB = 0.0;

  bool validA =
    getUnitInfo(
      from,
      familyA,
      scaleA
    );

  bool validB =
    getUnitInfo(
      to,
      familyB,
      scaleB
    );

  if (
    !validA ||
    !validB ||
    familyA != familyB
  ) {
    return false;
  }

  double result = 0;

  if (familyA == UNIT_TEMPERATURE) {
    double celsius = 0;

    if (from == "c") {
      celsius = value;
    } else if (from == "f") {
      celsius =
        (value - 32.0) *
        5.0 / 9.0;
    } else {
      celsius =
        value - 273.15;
    }

    if (to == "c") {
      result = celsius;
    } else if (to == "f") {
      result =
        celsius * 9.0 / 5.0 +
        32.0;
    } else {
      result =
        celsius + 273.15;
    }
  } else {
    double base =
      value * scaleA;

    result =
      base / scaleB;
  }

  elliSay(
    formatNumber(value) +
    " " +
    fromUnit +
    " is about " +
    formatNumber(result) +
    " " +
    toUnit +
    "."
  );

  return true;
}


// =====================================================
//                     LOCAL DATE
// =====================================================

String currentDateString(){
  struct tm t;

  if(!getLocalTime(&t,500)){
    // Same on-demand recovery as currentTimeString().
    // This protects the date utility if boot-time NTP failed.
    if(WiFi.status()==WL_CONNECTED){
      syncTime();
    }

    if(!getLocalTime(&t,1200)){
      return "NOT SYNCED";
    }
  }

  char b[40];

  strftime(
    b,
    sizeof(b),
    "%A, %d %B %Y",
    &t
  );

  return String(b);
}


// ---------------- WEB QUESTION TYPE ----------------
//
// Keeps the router explicit. Anything not solved locally
// can still fall back to the multi-source web brain.
//

bool likelyKnowledgeQuestion(const String& q) {
  const char* const questionOpeners[] = {
    "what ",
    "who ",
    "where ",
    "when ",
    "why ",
    "how ",
    "which ",
    "define ",
    "explain ",
    "tell me about ",
    "give me information about ",
    "meaning of ",
    "difference between ",
    "compare "
  };

  if (
    containsAny(
      q,
      questionOpeners,
      sizeof(questionOpeners) / sizeof(questionOpeners[0])
    )
  ) {
    return true;
  }

  // v0.8.1: DO NOT treat every short sentence as a web query.
  // Personal statements such as "school was tiring today" must stay local.
  //
  // Keep one narrow shorthand form for compact knowledge phrases:
  // "gravity of sun", "capital of japan", etc.
  bool personal =
    q.startsWith("i ") ||
    q.startsWith("my ") ||
    q.startsWith("we ") ||
    q.indexOf(" i ") >= 0 ||
    q.indexOf(" my ") >= 0;

  if (
    !personal &&
    q.length() >= 5 &&
    q.length() <= 60 &&
    q.indexOf(" of ") >= 0
  ) {
    return true;
  }

  return false;
}


void processCommand(String raw){
  raw.trim();

  if(!raw.length()) return;

  // Phase 4B: input has arrived, so Elli enters THINKING
  // before KIRA begins routing / local processing / AI work.
 elliVisualCommandStart();

  currentTurnElliUtterance="";

  uint32_t requestStart = millis();
  uint32_t heapBefore = ESP.getFreeHeap();
  uint32_t psramBefore = ESP.getFreePsram();

  String speechNormalized=
    normalizeSpeechUtterance(raw);

  bool wake=false;

  String q=
    stripWakeSmart(
      speechNormalized,
      wake
    );

  q=
    fuzzyNormalizeIntentWords(q);

  kiraDevObserveInput(raw,q);

  logUserUtterance(
    raw,
    q
  );

  Serial.println();
  Serial.print("[YOU] ");
  Serial.println(raw);

  Serial.print("[LANG] ");
  Serial.println(q);

  Serial.print("[INTENT] ");
  Serial.println(
    utteranceIntentName(
      detectUtteranceIntent(q)
    )
  );

  if(wake){
    Serial.print("[WAKE] Elli detected");

    if(q.length()){
      Serial.print(" -> ");
      Serial.println(q);
    }else{
      Serial.println();
    }
  }

  String route="LOCAL";

  // Natural device continuation:
  //   "turn on the fan" -> "actually turn it off"
  // Rewrites only an on/off command with a recent, unambiguous device target.
  resolveRecentDevicePronoun(q);

  // Intent Arbiter V1 launches in SHADOW MODE.
  // It observes the final normalized/context-resolved request,
  // but does not control any working route yet.
  ElliArbiterDecision arbiterDecision;
  bool arbiterDecisionReady=false;

  // Keep the user's own normalized sentence separate from any
  // context-expanded research form.
  String originalTurnQ=
    q;

  // If a follow-up itself causes Elli to ask another question, keep
  // the RESOLVED knowledge request as the next dialogue origin rather
  // than collapsing the conversation down to only the last short answer.
  String dialogueOriginForObservation=
    originalTurnQ;

  // ---------------------------------------------------
  // KIRA v1 SHORT-TERM CONVERSATION CONTEXT
  //
  // Pending clarification MUST have first priority.
  // We also suppress the automatic period greeting while
  // KIRA is waiting for a follow-up, otherwise a reply like
  // "the monument" could trigger both context handling and
  // normal conversation routing.
  // ---------------------------------------------------
  bool pendingV1Context=
    kiraV1ShouldConsumePendingInput(
      q
    );


  // ===================================================
  // CONVERSATION STATE V2
  // ===================================================

  ElliDialogueResolution dialogueResolution;

  bool dialogueConsumed=false;
  bool dialogueKnowledgeResume=false;
  bool dialogueLocalReply=false;
  bool dialogueRoutineTime=false;
  bool dialogueCancelled=false;


  if(
    !pendingV1Context
  ){

    dialogueConsumed=
      elliDialogueConsumeInput(
        q,
        dialogueResolution
      );


    if(dialogueConsumed){

      if(
        dialogueResolution.kind==
        ELLI_DIALOGUE_RESOLVE_KNOWLEDGE
      ){

        dialogueKnowledgeResume=true;


        q=
          normalizeInput(
            elliDialogueBuildKnowledgeResume(
              dialogueResolution
            )
          );


        dialogueOriginForObservation=
          q;


        Serial.print(
          "[DIALOGUE V2] Knowledge continuation -> "
        );

        Serial.println(
          q
        );
      }

      else if(
        dialogueResolution.kind==
        ELLI_DIALOGUE_RESOLVE_LOCAL
      ){

        dialogueLocalReply=true;
      }

      else if(
        dialogueResolution.kind==
        ELLI_DIALOGUE_RESOLVE_ROUTINE_TIME
      ){

        dialogueRoutineTime=true;
      }

      else if(
        dialogueResolution.kind==
        ELLI_DIALOGUE_RESOLVE_CANCELLED
      ){

        dialogueCancelled=true;
      }
    }
  }


  // ===================================================
  // CONVERSATION V3 — TOPIC CONTINUITY / CORRECTIONS
  // ===================================================
  ElliConversationV3Result conversationV3Result;
  bool conversationV3LocalReply=false;
  bool conversationV3Rewritten=false;

  if(
    !pendingV1Context &&
    !dialogueConsumed
  ){
    conversationV3Result=
      elliConversationV3Preprocess(q);

    if(
      conversationV3Result.action==
      ELLI_CONV3_LOCAL_REPLY
    ){
      conversationV3LocalReply=true;
      Serial.println(
        "[DIALOGUE V3] Local continuation handled."
      );
    }
    else if(
      conversationV3Result.action==
      ELLI_CONV3_REWRITE_KNOWLEDGE
    ){
      q=normalizeInput(
        conversationV3Result.rewrittenQuery
      );
      conversationV3Rewritten=true;

      Serial.print(
        "[DIALOGUE V3] Knowledge continuation -> "
      );
      Serial.println(q);
    }
  }


  bool localReferenceClarification=false;

  bool sameTurnReferenceAnchor=
    elliHasSameTurnReferenceAnchor(
      q
    );

  if(
    sameTurnReferenceAnchor
  ){
    Serial.println(
      "[CONTEXT] Same-turn reference resolved locally; no clarification needed."
    );
  }


  if(
    !pendingV1Context &&
    !dialogueConsumed &&
    !conversationV3LocalReply
  ){

    // Expire an unanswered local reference prompt.
    if(
      pendingReferenceQuestion.length() &&
      millis()-
      pendingReferenceSinceMs
      >
      REFERENCE_CONTEXT_TTL_MS
    ){

      pendingReferenceQuestion="";
      pendingReferenceSinceMs=0;
    }


    // The previous turn asked what "this / it / that" referred to.
    // A short fragment now completes that original question.
    if(
      pendingReferenceQuestion.length() &&
      elliLooksLikeShortReferenceAnswer(q)
    ){

      String referenceAnswer=
        q;


      q=
        pendingReferenceQuestion+
        " Context for the unresolved reference: "+
        referenceAnswer;


      Serial.print(
        "[CONTEXT] Reference completed -> "
      );

      Serial.println(
        q
      );


      pendingReferenceQuestion="";
      pendingReferenceSinceMs=0;
    }

    else{

      // A complete new question cancels an old local reference prompt.
      if(
        pendingReferenceQuestion.length() &&
        elliLooksLikeQuestionSyntax(q)
      ){

        pendingReferenceQuestion="";
        pendingReferenceSinceMs=0;
      }


      if(
        elliNeedsReferenceContext(q) &&
        !sameTurnReferenceAnchor &&
        !elliIsKnowledgeExpansionFollowUp(q)
      ){

        bool recentKnowledgeContext=

          lastKnowledgeQuery.length()

          &&

          millis()-
          lastKnowledgeContextMs
          <=
          KNOWLEDGE_CONTEXT_TTL_MS;


        if(recentKnowledgeContext){

          q+=
            " Conversation context: the previous knowledge question was '"+
            lastKnowledgeQuery+
            "'. Resolve pronouns or references in the current question from this context only when the reference is unambiguous";


          if(lastKnowledgeAnswer.length()){

            String compactAnswer=
              lastKnowledgeAnswer;


            if(
              compactAnswer.length()>260
            ){

              compactAnswer=
                compactAnswer.substring(
                  0,
                  260
                );
            }


            q+=
              " and the previous answer was '"+
              compactAnswer+
              "'";
          }


          Serial.println(
            "[CONTEXT] Resolved this/it/that from recent knowledge context."
          );
        }

        else{

          pendingReferenceQuestion=
            q;

          pendingReferenceSinceMs=
            millis();

          localReferenceClarification=true;
        }
      }
    }
  }


  // ===================================================
  // NATURAL KNOWLEDGE FOLLOW-UPS
  // ===================================================
  //
  // "tell me more", "go deeper", etc. reuse the recent topic
  // instead of becoming vague standalone searches.
  // ===================================================

  bool knowledgeExpansionMissingContext=false;


  if(
    !pendingV1Context &&
    !dialogueConsumed &&
    !conversationV3LocalReply &&
    !conversationV3Rewritten &&
    !localReferenceClarification
  ){

    bool wantsMore=
      elliIsKnowledgeExpansionFollowUp(q);


    if(wantsMore){

      bool recentKnowledge=

        lastKnowledgeQuery.length()

        &&

        millis()-
        lastKnowledgeContextMs
        <=
        KNOWLEDGE_CONTEXT_TTL_MS;


      if(recentKnowledge){

        q=
          lastKnowledgeQuery+
          " Explain this in more detail, add useful context, and avoid simply repeating the previous answer.";


        dialogueOriginForObservation=
          q;


        Serial.println(
          "[DIALOGUE V2] Expanded recent knowledge topic."
        );
      }

      else{

        knowledgeExpansionMissingContext=true;
      }
    }
  }


  // ===================================================
  // INTENT ARBITER V1 — SHADOW DECISION
  // ===================================================
  //
  // Run AFTER reference/context expansion so the arbiter sees the same
  // meaningful sentence the router is about to execute.
  //
  // Pending v1 clarification remains owned by the context engine.
  // ===================================================

  if(
    !pendingV1Context &&
    !localReferenceClarification &&
    !conversationV3LocalReply &&
    !dialogueLocalReply &&
    !dialogueRoutineTime &&
    !dialogueCancelled &&
    !knowledgeExpansionMissingContext &&
    !q.startsWith("arbiter sense ")
  ){

    arbiterDecision=
      elliArbitrate(
        q
      );

    arbiterDecisionReady=true;


    #if KIRA_INTENT_ARBITER_SHADOW

      printElliArbiterShadow(
        arbiterDecision
      );

    #endif
  }


  bool autoPeriodGreeting = false;
  bool greetingOnly = false;

  if(!pendingV1Context){
    greetingOnly=
      isGreetingOnlyInput(
        q,
        wake
      );

    // Do not queue a time-of-day greeting in front of a real question.
    // Automatic morning/afternoon/evening greetings are now reserved for
    // utterances that are actually greetings.
    if(
      greetingOnly
    ){
      autoPeriodGreeting=
        maybeAutomaticPeriodGreeting();
    }
  }

  // Study mode decorates only the copy sent into knowledge/web routing.
  // Local commands continue to use the untouched q string.
  String knowledgeRouteQ=
    elliStudyDecorateKnowledgeQuery(q);


  if(pendingV1Context){
    route="V1 CONTEXT RESUME";
    kiraV1HandlePendingInput(q);
  }

  // ---------------------------------------------------
  // 1) LOCAL CONVERSATION / PERSONALITY
  // ---------------------------------------------------

  else if(conversationV3LocalReply){

    route="CONVERSATION V3";

    elliSay(
      conversationV3Result.localReply
    );

    elliState=ELLI_IDLE;
  }


  else if(conversationV3Rewritten){
    route="CONVERSATION V3 AI";
    String evidenceQuery=conversationV3Result.evidenceQuery.length() ? conversationV3Result.evidenceQuery : elliConversationV3ActiveQuery();
    KiraV1::QueryFrame followupFrame;
    if(!KiraV1::analyzeQuery(evidenceQuery,followupFrame)){
      KiraV1::clearQueryFrame(followupFrame);
      followupFrame.raw=evidenceQuery;
      followupFrame.normalized=normalizeInput(evidenceQuery);
      followupFrame.intent=KiraV1::INTENT_FACT_LOOKUP;
      followupFrame.op=KiraV1::OP_NONE;
      followupFrame.expectedAnswer=KiraV1::ANSWER_TEXT;
      followupFrame.subject=evidenceQuery;
      followupFrame.entity=evidenceQuery;
      followupFrame.relation="conversation_followup";
      followupFrame.needsWeb=true;
      followupFrame.ambiguous=false;
      followupFrame.confidence=90;
    }
    bool followupHandled=false;

    // V1.8: conversation continuity survives loss of internet when the active
    // topic exists in the offline knowledge pack or learned verified cache.
    if(kiraNetworkMode()==KIRA_NET_OFFLINE){
      String offlineTitle,offlineAnswer;
      int offlineScore=0;
      if(kiraOfflineFindLesson(evidenceQuery,offlineTitle,offlineAnswer,offlineScore)){
        Serial.print("[CONVERSATION V3] Offline continuation: ");
        Serial.println(offlineTitle);
        elliSay(offlineAnswer+" [Offline continuation]");
        followupHandled=true;
      }
    }

    if(!followupHandled){
      KiraV1::AiWebResult followupResult;
      bool followupOk=KiraV1::askGroqWeb(followupFrame,conversationV3Result.rewrittenQuery,followupResult,"",false,evidenceQuery);
      if(followupOk && followupResult.answer.length()) elliSay(followupResult.answer);
      else if(followupOk && followupResult.needsClarification) elliSay("I still have the previous topic, but I need one more detail before I can continue it accurately.");
      else if(kiraNetworkMode()==KIRA_NET_OFFLINE) kiraOfflineHandleKnowledge(evidenceQuery);
      else elliSay("I still have the previous topic in context, but the online reasoning providers could not continue it right now.");
    }
    elliState=ELLI_IDLE;
  }


  else if(localReferenceClarification){

    route="LOCAL CONTEXT CLARIFY";

    elliSay(
      elliReferenceContextPrompt(
        q
      )
    );

    elliState=ELLI_IDLE;
  }


  else if(dialogueCancelled){

    route="DIALOGUE CONTEXT";

    elliSay(
      "Okay — I cleared that pending follow-up."
    );

    elliState=ELLI_IDLE;
  }


  else if(dialogueRoutineTime){

    route="PERSONAL ROUTINE";

    int hour=0;
    int minute=0;
    bool explicitMeridiem=false;


    if(
      !parseClockToken(
        dialogueResolution.userAnswer,
        hour,
        minute,
        explicitMeridiem
      )
    ){

      elliDialogueBeginRoutineTime(
        dialogueResolution.payload
      );

      elliSay(
        "I still couldn't read the time. Try something like 7 AM, 6:30 PM, or 18:30."
      );
    }

    else if(
      !explicitMeridiem &&
      hour>=1 &&
      hour<=12
    ){

      elliDialogueBeginRoutineTime(
        dialogueResolution.payload
      );

      elliSay(
        "That could be AM or PM. Which one do you mean?"
      );
    }

    else{

      addDailyRoutine(
        dialogueResolution.payload,
        hour,
        minute,
        0
      );
    }


    elliState=ELLI_IDLE;
  }


  else if(dialogueLocalReply){

    route="DIALOGUE FOLLOWUP";

    elliSay(
      elliDialogueComposeLocalReply(
        dialogueResolution
      )
    );

    elliState=ELLI_IDLE;
  }


  else if(knowledgeExpansionMissingContext){

    route="DIALOGUE CONTEXT";

    elliSay(
      "I can go deeper — tell me which topic you want me to continue."
    );

    elliState=ELLI_IDLE;
  }


  else if(
    q=="context status" ||
    q=="dialogue status" ||
    q=="conversation state"
  ){

    route="DIALOGUE STATUS";

    elliSay(
      elliDialogueStatus()+" "+
      elliConversationV3Status()
    );

    elliState=ELLI_IDLE;
  }


  else if(
    q=="cancel context" ||
    q=="clear context" ||
    q=="forget that context"
  ){

    route="DIALOGUE CONTEXT";

    elliDialogueClear();
    elliConversationV3Clear();
    kiraV1ClearPendingContext();

    pendingReferenceQuestion="";
    pendingReferenceSinceMs=0;

    elliSay(
      "Okay — pending conversation context cleared."
    );

    elliState=ELLI_IDLE;
  }


  else if(autoPeriodGreeting && greetingOnly){

    route="AUTO PERIOD GREETING";
    elliState=ELLI_IDLE;
  }


  // ---------------------------------------------------
  // LANGUAGE DIAGNOSTICS
  //
  // These MUST run before personal-statement handling.
  // Otherwise:
  //
  //   nlu sense i have been studying...
  //
  // is itself mistaken for a personal sentence.
  // ---------------------------------------------------

  else if(q.startsWith("intent sense ")){

    route="UTTERANCE SENSE";

    String probe=
      q.substring(
        13
      );

    probe.trim();

    printUtteranceSense(
      probe
    );

    elliState=ELLI_IDLE;
  }


  else if(q.startsWith("nlu sense ")){

    route="UNIVERSAL NLU SENSE";

    String probe=
      q.substring(
        10
      );

    probe.trim();

    printElliNLUFrame(
      probe
    );

    elliState=ELLI_IDLE;
  }


  else if(q.startsWith("semantic sense ")){

    route="SEMANTIC FRAME V2 SENSE";

    String probe=
      q.substring(
        15
      );

    probe.trim();

    printElliSemanticFrame(
      probe
    );

    elliState=ELLI_IDLE;
  }


  else if(q.startsWith("response sense ")){

    route="RESPONSE COMPOSER V1 SENSE";

    String probe=
      q.substring(
        15
      );

    probe.trim();

    printElliResponsePlan(
      probe
    );

    elliState=ELLI_IDLE;
  }


  else if(q.startsWith("arbiter sense ")){

    route="INTENT ARBITER V1 SENSE";

    String probe=
      q.substring(
        14
      );

    probe.trim();

    printElliArbiterDecision(
      probe
    );

    elliState=ELLI_IDLE;
  }


  // ===================================================
  // DEVELOPER / DIAGNOSTIC TOOLS V1.3
  // ===================================================

  else if(kiraDevHandleCommand(q)){
    route="DEVELOPER";
    elliState=ELLI_IDLE;
  }


  // ===================================================
  // NON-DESTRUCTIVE SELF-TEST / REGRESSION SUITE
  // ===================================================

  else if(kiraHandleSelfTestCommand(q)){
    route="SELF TEST";
    elliState=ELLI_IDLE;
  }


  // ===================================================
  // PHASE 5H AUTOMATION SCENES
  // ===================================================
  //
  // Scene commands still run INSIDE processCommand().
  // Voice therefore never bypasses the Universal Brain.
  // Physical GPIO is synchronized later by kiraBrainLoop().
  // ===================================================

  else if(
    kiraSceneHandleCommand(
      q,
      mainLight,
      fanState,
      chargerState,
      secondLight
    )
  ){
    route="LOCAL SCENE";
    elliState=ELLI_IDLE;
  }


  // ===================================================
  // KIRA V1.7 TOOL ENGINE
  // ===================================================
  // New productivity/utility tools run through one registry. Existing
  // proven clock/device/memory/web routes remain protected legacy bridges.

  else if(kiraToolHandleCommand(q)){
    route="TOOL "+kiraToolLastTool();
    elliState=ELLI_IDLE;
  }


  // ===================================================
  // HIGH-PRIORITY RUNTIME CONTROL INTERRUPT
  // ===================================================
  //
  // "Elli stop" becomes "stop" after wake-word stripping.
  //
  // Runtime state MUST beat:
  //   memory
  //   semantic lookup
  //   AI/web
  //
  // This is deliberately before the personal brain and web
  // router so a judge can always stop a running local process.
  // ===================================================

  else if(
    q=="stop" ||
    q=="stop now" ||
    q=="please stop"
  ){

    if(
      alarmRinging ||
      timerRinging
    ){

      route="LOCAL CLOCK ALERT";

      stopClockAlert();
    }

    else if(stopwatchRunning){

      route="LOCAL STOPWATCH";

      stopStopwatch();
    }

    else if(
      timerActive ||
      timerPaused
    ){

      route="LOCAL TIMER";

      cancelTimer();
    }

    else{

      route="LOCAL CONTROL";

      elliSay(
        "Nothing local is running right now, so there's nothing to stop."
      );
    }


    elliState=ELLI_IDLE;
  }


  // ---------------------------------------------------
  // STUDY MODE / QUIZ
  // ---------------------------------------------------

  else if(elliStudyHandleCommand(q)){
    route="STUDY MODE";
    elliState=ELLI_IDLE;
  }

  // ---------------------------------------------------
  // PERSONAL MEMORY V3
  // ---------------------------------------------------

  else if(handleMemoryV3Intent(q)){
    route="PERSONAL MEMORY V3";
    elliState=ELLI_IDLE;
  }

  // ---------------------------------------------------
  // PERSONAL MEMORY V2
  // ---------------------------------------------------

  else if(handleMemoryV2Intent(q)){
    route="PERSONAL MEMORY V2";
    elliState=ELLI_IDLE;
  }

  // ---------------------------------------------------
  // PERSONAL BRAIN RUNS BEFORE WEB/GENERAL CHAT
  // ---------------------------------------------------

  else if(handleRoutineIntent(q)){
    route="PERSONAL ROUTINE";
    elliState=ELLI_IDLE;
  }

  else if(handleTaskIntent(q)){
    route="PERSONAL TASK";
    elliState=ELLI_IDLE;
  }

  else if(
    detectUtteranceIntent(q)==UTT_PROFILE &&
    handleProfileMemoryIntent(q)
  ){
    route="PERSONAL MEMORY";
    elliState=ELLI_IDLE;
  }

  else if(
    detectUtteranceIntent(q)==UTT_PROFILE &&
    handlePersonalRecallIntent(q)
  ){
    route="PERSONAL MEMORY RECALL";
    elliState=ELLI_IDLE;
  }

  else if(handleSupportiveBrain(q)){
    route="SUPPORT / MOTIVATION";
    elliState=ELLI_IDLE;
  }

  // ---------------------------------------------------
  // UNIVERSAL PERSONAL STATEMENT
  // ---------------------------------------------------
  //
  // Open-vocabulary first-person statements that are not stable profile
  // facts are still understood locally. This prevents sentences such as
  // "I had a long day" from falling into LOCAL CLARIFY.
  // ---------------------------------------------------

  else if(
    detectUtteranceIntent(q)==UTT_STATEMENT &&
    handleNaturalPersonalStatement(q)
  ){
    route="PERSONAL CONVERSATION";
    elliState=ELLI_IDLE;
  }

  // ---------------------------------------------------
  // LOCAL DEVICE STATE QUERY
  // ---------------------------------------------------
  //
  // "is the fan on?" and "light status" are questions, but
  // they ask about KIRA's own actuator state and must beat AI/web.
  // ---------------------------------------------------

  else if(handleDeviceStateQuery(q)){
    route="LOCAL DEVICE";
    elliState=ELLI_IDLE;
  }

  // ---------------------------------------------------
  // LOCAL SOCIAL CONVERSATION
  // ---------------------------------------------------

  else if(handleConversation(q,wake)){
    route="LOCAL CHAT";
    elliState=ELLI_IDLE;
  }

  // ---------------------------------------------------
  // DETERMINISTIC LOCAL KNOWLEDGE
  //
  // IMPORTANT: this runs BEFORE AI/web.
  // Current date/time and calendar questions must never be
  // reinterpreted as generic web definitions.
  // ---------------------------------------------------

  else if(handleDeterministicPreWeb(q)){
    route="DETERMINISTIC LOCAL";
    elliState=ELLI_IDLE;
  }

  // ---------------------------------------------------
  // V1.8 OFFLINE / EXHIBITION KNOWLEDGE
  // ---------------------------------------------------
  // In forced/offline mode this runs before web. Exhibition mode also
  // prefers its local pack for fast, deterministic demonstrations.

  else if(kiraOfflineHandleKnowledge(knowledgeRouteQ)){
    route="OFFLINE KNOWLEDGE";
    lastKnowledgeQuery=q;
    lastKnowledgeContextMs=millis();
    elliState=ELLI_IDLE;
  }

  // ---------------------------------------------------
  // KIRA v1 UNIVERSAL QUERY / AI-WEB ROUTER
  // ---------------------------------------------------

  else if(kiraV1HandleStructuredWeb(knowledgeRouteQ)){

    route="V1 UNIVERSAL QUERY";

    lastKnowledgeQuery=
      q;

    lastKnowledgeContextMs=
      millis();

    elliState=ELLI_IDLE;
  }

  // ---------------------------------------------------
  // GENERIC SEMANTIC CORE
  //
  // Handles query structure (fact request, definition, ranking,
  // comparison, ambiguity) without hard-coding the topic being asked.
  // ---------------------------------------------------

  else if(handleSemanticPreWeb(knowledgeRouteQ)){

    route="SEMANTIC CORE";

    lastKnowledgeQuery=
      q;

    lastKnowledgeContextMs=
      millis();

    elliState=ELLI_IDLE;
  }

  else if(
    q=="recent conversation" ||
    q=="show recent conversation" ||
    q=="conversation context"
  ){
    route="CONVERSATION CONTEXT";
    printRecentConversation();
    elliState=ELLI_IDLE;
  }

  // ---------------------------------------------------
  // 2) PRIVACY-SENSITIVE LOCATION REQUESTS
  // ---------------------------------------------------

  else if(isPrivateResidenceQuery(q)){
    route="PRIVACY";

    elliSay(
      "I can look up public company headquarters or publicly listed executive information, but I won't search for someone's private home location."
    );

    elliState=ELLI_IDLE;
  }

  // ---------------------------------------------------
  // 3) LOCAL TIME / DATE / SYSTEM
  // ---------------------------------------------------

  else if(
    q=="time" ||
    q=="what time is it" ||
    q=="tell me the time" ||
    q=="current time"
  ){
    route="LOCAL TIME";
    elliSay("It is "+currentTimeString());
  }

  else if(
    q=="memory" ||
    q=="ram" ||
    q=="show memory" ||
    q=="show ram" ||
    q=="what is your ram" ||
    q=="memory status"
  ){
    route="LOCAL SYSTEM";
    printMemory();
  }

  else if(
    q=="status" ||
    q=="system status" ||
    q=="what is your status" ||
    q=="whats your status"
  ){
    route="LOCAL SYSTEM";
    printStatus();
  }


  else if(
    q=="brain status" ||
    q=="personal brain status"
  ){
    route="PERSONAL BRAIN STATUS";

    Serial.println();
    Serial.println("========== PERSONAL BRAIN STATUS ==========");
    Serial.print("Owner       : "); Serial.println(brainStore.ownerName);
    Serial.print("Memories    : "); Serial.println(memoryCount());
    Serial.print("Session mem : "); Serial.println(sessionMemoryCount());
    Serial.print("Memory V3   : "); Serial.println(memoryV3Status());
    Serial.print("Tasks       : "); Serial.println(activeTaskCount());
    Serial.print("Routines    : "); Serial.println(activeRoutineCount());
    Serial.print("AutoRemember: "); Serial.println(brainStore.autoRemember ? "ON" : "OFF");
    Serial.print("SD memory   : "); Serial.println(brainSdReady ? "MOUNTED" : "NOT MOUNTED");
    Serial.print("Study mode  : "); Serial.println(elliStudyModeActive() ? "ON" : "OFF");
    Serial.print("Conv V3     : "); Serial.println(elliConversationV3HasTopic() ? "ACTIVE" : "EMPTY");
    Serial.println("===========================================");
  }


  else if(
    q=="web sources" ||
    q=="show web sources" ||
    q=="source catalog" ||
    q=="show source catalog"
  ){
    route="WEB SOURCE CATALOG";
    printWebSourceCatalog();
  }

  else if(q.startsWith("source sense ")){
    route="WEB SOURCE SENSE";

    String probe=q.substring(13);
    probe.trim();

    if(probe.length()){
      printSourceSense(probe);
    }else{
      elliSay("Put a question after 'source sense'.");
    }
  }

  // ---------------------------------------------------
  // 4) CLOCK BRAIN: STOPWATCH / TIMER / ALARM
  // ---------------------------------------------------

  else if(
    q=="count until i say stop" ||
    q=="count untill i say stop" ||
    q=="count until i stop you" ||
    q=="count untill i stop you" ||
    q=="count" ||
    q=="start counting" ||
    q=="start stopwatch" ||
    q=="start the stopwatch" ||
    q=="count from now" ||
    q=="time me" ||
    q=="start timing me"
  ){
    route="LOCAL STOPWATCH";
    startStopwatch();
  }

  else if(
    q=="stop stopwatch" ||
    q=="stop the stopwatch" ||
    q=="stop counting"
  ){
    route="LOCAL STOPWATCH";
    stopStopwatch();
  }

  else if(
    q=="reset stopwatch" ||
    q=="reset the stopwatch" ||
    q=="clear stopwatch"
  ){
    route="LOCAL STOPWATCH";
    resetStopwatch();
  }

  else if(
    q=="how long has it been" ||
    q=="how long have you been counting" ||
    q=="stopwatch status" ||
    q=="whats the stopwatch at" ||
    q=="what is the stopwatch at"
  ){
    route="LOCAL STOPWATCH";

    if(stopwatchRunning || stopwatchStoredMs > 0){
      elliSay(
        "The stopwatch is at " +
        formatDurationMs(stopwatchElapsedMs()) +
        "."
      );
    }else{
      elliSay("The stopwatch hasn't been started yet.");
    }
  }

  else if(
    q=="pause timer" ||
    q=="pause the timer"
  ){
    route="LOCAL TIMER";
    pauseTimer();
  }

  else if(
    q=="resume timer" ||
    q=="resume the timer" ||
    q=="continue timer" ||
    q=="continue the timer"
  ){
    route="LOCAL TIMER";
    resumeTimer();
  }

  else if(
    q=="cancel timer" ||
    q=="cancel the timer" ||
    q=="clear timer" ||
    q=="stop timer" ||
    q=="stop the timer"
  ){
    route="LOCAL TIMER";
    cancelTimer();
  }

  else if(
    q=="how much time is left" ||
    q=="time left" ||
    q=="timer status" ||
    q=="how long is left on the timer" ||
    q=="how much time left on timer"
  ){
    route="LOCAL TIMER";

    if(timerRinging){
      elliSay("The timer has finished and the alert is active.");
    }
    else if(timerActive){
      elliSay(
        String(timerPaused ? "The paused timer has " : "The timer has ") +
        formatDurationMs(timerRemainingNowMs()) +
        " remaining."
      );
    }else{
      elliSay("There isn't an active timer right now.");
    }
  }

  else if(
    q.startsWith("set a timer") ||
    q.startsWith("set timer") ||
    q.startsWith("timer for ") ||
    q.startsWith("count down for ") ||
    q.startsWith("count down ") ||
    q.startsWith("countdown for ") ||
    q.startsWith("countdown ") ||
    q.startsWith("remind me in ")
  ){
    route="LOCAL TIMER";

    uint32_t durationMs = 0;

    if(parseDurationMs(q,durationMs)){
      startTimer(durationMs);
    }else{
      elliSay(
        "I couldn't read that timer duration. Try something like 'timer for 5 minutes' or 'timer for 1 hour 20 minutes'."
      );
    }
  }

  else if(
    q=="list alarms" ||
    q=="show alarms" ||
    q=="what alarms are set" ||
    q=="what alarms do i have" ||
    q=="alarm status" ||
    q=="alarms"
  ){
    route="LOCAL ALARM";
    listAlarms();
  }

  else if(
    q=="cancel all alarms" ||
    q=="delete all alarms" ||
    q=="clear all alarms" ||
    q=="remove all alarms"
  ){
    route="LOCAL ALARM";
    cancelAllAlarms();
  }

  else if(
    q.startsWith("cancel alarm ") ||
    q.startsWith("delete alarm ") ||
    q.startsWith("remove alarm ")
  ){
    route="LOCAL ALARM";

    int number = 0;

    if(parseAlarmNumber(q,number)){
      cancelAlarmNumber(number);
    }else{
      elliSay("Tell me the alarm number, for example 'cancel alarm 2'.");
    }
  }

  else if(
    q=="cancel alarm" ||
    q=="cancel the alarm" ||
    q=="delete alarm" ||
    q=="clear alarm"
  ){
    route="LOCAL ALARM";

    int count = activeAlarmCount();

    if(count == 0){
      elliSay("There isn't a saved alarm to cancel.");
    }
    else if(count == 1){
      cancelAlarmNumber(1);
    }
    else{
      elliSay(
        "You have " +
        String(count) +
        " alarms. Say 'list alarms', then 'cancel alarm 2', or say 'cancel all alarms'."
      );
    }
  }

  else if(
    q=="what alarm is set" ||
    q=="what is my alarm" ||
    q=="when is my alarm"
  ){
    route="LOCAL ALARM";
    listAlarms();
  }

  else if(
    q.startsWith("set an alarm") ||
    q.startsWith("set another alarm") ||
    q.startsWith("set alarm") ||
    q.startsWith("alarm at ") ||
    q.startsWith("alarm for ") ||
    q.startsWith("wake me at ") ||
    q.startsWith("wake me up at ")
  ){
    route="LOCAL ALARM";

    int h = 0;
    int m = 0;

    if(parseAlarmTime(q,h,m)){
      String normalizedAlarm = normalizeInput(q);

      bool forceTomorrow =
        normalizedAlarm.indexOf("tomorrow") >= 0;

      bool forceToday =
        normalizedAlarm.indexOf("today") >= 0;

      bool explicitDaily =
        normalizedAlarm.indexOf("every day") >= 0 ||
        normalizedAlarm.indexOf("everyday") >= 0 ||
        normalizedAlarm.indexOf("daily") >= 0;

      bool explicitOnce =
        normalizedAlarm.indexOf("once") >= 0;

      // Default behavior:
      //   no date -> daily repeating alarm
      //   today/tomorrow/once -> one-time
      //   daily/every day -> repeating
      bool repeatDaily =
        explicitDaily ||
        (
          !forceTomorrow &&
          !forceToday &&
          !explicitOnce
        );

      addAlarm(
        h,
        m,
        forceTomorrow,
        forceToday,
        repeatDaily
      );
    }else{
      elliSay(
        "I couldn't read that alarm time. Try 'set alarm for 6:30 AM', 'alarm at 18:30', or 'set alarm for 7 AM tomorrow'."
      );
    }
  }

  // A bare "stop" is context-aware. Specific commands such
  // as "stop fan" continue to the device router below.
  else if(q=="stop"){
    if(alarmRinging || timerRinging){
      route="LOCAL CLOCK ALERT";
      stopClockAlert();
    }
    else if(stopwatchRunning){
      route="LOCAL STOPWATCH";
      stopStopwatch();
    }
    else if(timerActive || timerPaused){
      route="LOCAL TIMER";
      cancelTimer();
    }
    else{
      route="LOCAL CLOCK";
      elliSay("There isn't an active stopwatch, timer, or alert to stop.");
    }
  }

  else if(
    q=="stop alarm" ||
    q=="stop the alarm" ||
    q=="silence alarm" ||
    q=="silence the alarm"
  ){
    route="LOCAL ALARM";

    if(alarmRinging){
      stopClockAlert();
    }
    else if(activeAlarmCount() == 1){
      cancelAlarmNumber(1);
    }
    else if(activeAlarmCount() > 1){
      elliSay("No alarm is ringing. You have multiple saved alarms, so say 'list alarms' or 'cancel alarm 2'.");
    }
    else{
      elliSay("There isn't an active alarm right now.");
    }
  }

  else if(
    q=="stop timer alert" ||
    q=="stop the timer alert"
  ){
    route="LOCAL CLOCK ALERT";
    stopClockAlert();
  }

  // ---------------------------------------------------
  // 5) LOCAL CALCULATOR
  // ---------------------------------------------------

  else if(tryLocalMath(q)){
    route="LOCAL CALCULATOR";
  }

  // ---------------------------------------------------
  // 6) LOCAL UNIT CONVERTER
  // ---------------------------------------------------

  else if(tryUnitConversion(q)){
    route="LOCAL CONVERTER";
  }

  // ---------------------------------------------------
  // 7) DEVICE COMMANDS
  // ---------------------------------------------------

  else if(
    q.indexOf("dim")>=0 ||
    q.indexOf("brightness")>=0 ||
    q.indexOf("brighter")>=0 ||
    q.indexOf("dimmer")>=0
  ){
    route="LOCAL DEVICE";

    elliSay(
      "I can switch relay lights on or off, but this relay setup cannot change brightness."
    );
  }

  else if(
    (
      q.indexOf("everything")>=0 ||
      q.indexOf("all devices")>=0 ||
      q=="all on"
    ) &&
    (
      wantsOn(q) ||
      q=="all on"
    )
  ){
    route="LOCAL DEVICE";

    mainLight=
      fanState=
      chargerState=
      secondLight=
      true;

    noteDeviceContext("all devices");

    elliSay(
      "Done, all four devices are ON."
    );
  }

  else if(
    (
      q.indexOf("everything")>=0 ||
      q.indexOf("all devices")>=0 ||
      q=="all off"
    ) &&
    (
      wantsOff(q) ||
      q=="all off"
    )
  ){
    route="LOCAL DEVICE";

    mainLight=
      fanState=
      chargerState=
      secondLight=
      false;

    noteDeviceContext("all devices");

    elliSay(
      "Done, all four devices are OFF."
    );
  }

  else if(
    q.indexOf("second light")>=0 ||
    q.indexOf("light 2")>=0 ||
    q.indexOf("light two")>=0
  ){
    if(wantsOn(q)||q=="second light on"){
      route="LOCAL DEVICE";
      secondLight=true;
      noteDeviceContext("second light");
      elliSay(deviceReply("second light",true));
    }
    else if(wantsOff(q)||q=="second light off"){
      route="LOCAL DEVICE";
      secondLight=false;
      noteDeviceContext("second light");
      elliSay(deviceReply("second light",false));
    }
    else if(asksDeviceState(q)){
      route="LOCAL DEVICE";
      noteDeviceContext("second light");
      elliSay(String("The second light is ")+onOff(secondLight)+".");
    }
    else{
      route="WEB";
      webBrain(q);
    }
  }

  else if(q.indexOf("fan")>=0){
    if(wantsOn(q)||q=="fan on"){
      route="LOCAL DEVICE";
      fanState=true;
      noteDeviceContext("fan");
      elliSay(deviceReply("fan",true));
    }
    else if(wantsOff(q)||q=="fan off"){
      route="LOCAL DEVICE";
      fanState=false;
      noteDeviceContext("fan");
      elliSay(deviceReply("fan",false));
    }
    else if(asksDeviceState(q)){
      route="LOCAL DEVICE";
      noteDeviceContext("fan");
      elliSay(String("The fan is ")+onOff(fanState)+".");
    }
    else{
      route="WEB";
      webBrain(q);
    }
  }

  else if(
    q.indexOf("charger")>=0 ||
    q.indexOf("charging")>=0
  ){
    if(wantsOn(q)||q=="charger on"){
      route="LOCAL DEVICE";
      chargerState=true;
      noteDeviceContext("charger");
      elliSay(deviceReply("charger",true));
    }
    else if(wantsOff(q)||q=="charger off"){
      route="LOCAL DEVICE";
      chargerState=false;
      noteDeviceContext("charger");
      elliSay(deviceReply("charger",false));
    }
    else if(asksDeviceState(q)){
      route="LOCAL DEVICE";
      noteDeviceContext("charger");
      elliSay(String("The charger is ")+onOff(chargerState)+".");
    }
    else{
      route="WEB";
      webBrain(q);
    }
  }

  else if(q.indexOf("light")>=0){
    if(wantsOn(q)||q=="light on"){
      route="LOCAL DEVICE";
      mainLight=true;
      noteDeviceContext("light");
      elliSay(deviceReply("main light",true));
    }
    else if(wantsOff(q)||q=="light off"){
      route="LOCAL DEVICE";
      mainLight=false;
      noteDeviceContext("light");
      elliSay(deviceReply("main light",false));
    }
    else if(asksDeviceState(q)){
      route="LOCAL DEVICE";
      noteDeviceContext("light");
      elliSay(String("The main light is ")+onOff(mainLight)+".");
    }
    else{
      route="WEB";
      webBrain(q);
    }
  }

  // ---------------------------------------------------
  // 8) MUSIC PLACEHOLDER
  // ---------------------------------------------------

  else if(
    q.indexOf("play music")>=0 ||
    q.indexOf("play a song")>=0
  ){
    route="LOCAL MUSIC";

    elliSay(
      "Music is planned, but the amplifier and SD hardware are not connected in this ESP32-only test build yet."
    );
  }

  // ---------------------------------------------------
  // 9) MULTI-SOURCE WEB KNOWLEDGE
  // ---------------------------------------------------

  else if(likelyKnowledgeQuestion(q)){

    route="WEB";

    lastKnowledgeQuery=
      q;

    lastKnowledgeContextMs=
      millis();

    webBrain(q);
  }

  // ---------------------------------------------------
  // 10) FINAL FALLBACK
  // ---------------------------------------------------

  else{

    ElliNLUFrame finalNLU=
      elliAnalyzeNLU(q);


    if(
      finalNLU.kind==
      ELLI_SENTENCE_STATEMENT
    ){

      route="LOCAL STATEMENT";

      // Neutral acknowledgement: understand the sentence without
      // pretending an unverified factual statement is true and without
      // forcing the user to rephrase ordinary English.
      elliSay(
        elliNaturalStatementAcknowledgement(q)
      );
    }

    else{

      route="LOCAL CLARIFY";

      // Only genuinely unresolved fragments reach this point.
      elliSay(
        "I caught the words, but I don't yet have enough structure to know whether you want information, an action, or something remembered."
      );
    }
  }

  elliState=ELLI_IDLE;


  // ===================================================
  // CONVERSATION V3 — OBSERVE SUCCESSFUL KNOWLEDGE TURN
  // ===================================================
  bool toolKnowledgeTurn=
    route.startsWith("TOOL ") &&
    kiraToolLastTool()=="dictionary.lookup";

  if(
    currentTurnElliUtterance.length() &&
    (
      route=="V1 UNIVERSAL QUERY" ||
      route=="SEMANTIC CORE" ||
      route=="CONVERSATION V3 AI" ||
      toolKnowledgeTurn
    )
  ){
    elliConversationV3ObserveKnowledge(
      originalTurnQ,
      knowledgeRouteQ,
      currentTurnElliUtterance
    );

    String activeTopic=
      elliConversationV3ActiveQuery();

    if(activeTopic.length()){
      lastKnowledgeQuery=activeTopic;
      lastKnowledgeContextMs=millis();
    }
  }


  // ===================================================
  // CONVERSATION STATE V2 — OBSERVE FINAL ELLI RESPONSE
  // ===================================================
  //
  // Skip when another dedicated context engine already owns the
  // next turn.
  // ===================================================

  if(
    currentTurnElliUtterance.length() &&
    !kiraV1HasPendingContext() &&
    !pendingReferenceQuestion.length() &&
    !elliDialogueHasPending()
  ){

    elliDialogueObserveTurn(
      dialogueOriginForObservation,
      route,
      currentTurnElliUtterance
    );
  }


  #if KIRA_INTENT_ARBITER_SHADOW

    if(
      arbiterDecisionReady
    ){

      elliArbiterCompareRoute(
        arbiterDecision,
        route
      );
    }

  #endif


  // Session-only history is committed after routing, so commands such as
  // "repeat my last question" see the previous turn rather than themselves.
  kiraHistoryObserveUser(raw,originalTurnQ);
  kiraHistoryObserveRoute(originalTurnQ,route);

  uint32_t elapsed=millis()-requestStart;
  uint32_t heapAfter=ESP.getFreeHeap();
  uint32_t psramAfter=ESP.getFreePsram();

  kiraDevObserveRoute(
    route,
    elapsed,
    heapBefore,
    heapAfter,
    psramBefore,
    psramAfter
  );

  Serial.println();
  Serial.println("------ UNIVERSAL ROUTER STATS ------");

  Serial.print("Route            : ");
  Serial.println(route);

  Serial.print("Total time       : ");
  Serial.print(elapsed);
  Serial.println(" ms");

  Serial.print("Heap before      : ");
  Serial.println(heapBefore);

  Serial.print("Heap after       : ");
  Serial.println(heapAfter);

  Serial.print("Heap difference  : ");
  Serial.println(
    (int32_t)heapBefore-
    (int32_t)heapAfter
  );

  Serial.print("PSRAM before     : ");
  Serial.println(psramBefore);

  Serial.print("PSRAM after      : ");
  Serial.println(psramAfter);

  Serial.println("-----------------------------------");
}

void updateRenderer(){
  uint32_t now=millis();
  if(now-lastFrameTime>=FRAME_INTERVAL_MS){ lastFrameTime=now; frameCounter++; }
  if(now-fpsTimer>=1000){ measuredFPS=frameCounter; frameCounter=0; fpsTimer=now; }
}

void kiraBrainSetup(){
  Serial.begin(115200); delay(1500);
  randomSeed(esp_random());
  for(int i=0;i<18;i++) lastSig[i]=0xFFFFFFFF;

  Serial.println();
  Serial.println("======================================");
  Serial.println("         KIRA UNIVERSAL BRAIN v1.8-alpha");
  Serial.println("======================================");
  Serial.println("[PERSONALITY] 18 daily-life intents + large sentence vocabulary");
  Serial.println("[PERSONALITY] ~180 replies per intent");
  Serial.println("[PERSONALITY] 3,240+ local combinations");
  Serial.println("[ROUTER] Personal brain + tasks + routines + clock + devices + Source Sense web");
  Serial.println("[WEB] Source Sense Router + 50+ source catalog");
  Serial.println("[WEB] 15 source connectors enabled in Phase 1");
  Serial.println("[WEB] Strict common-word title filtering enabled");
  Serial.println("[BRAIN] Searchable personal memory + NVS cache + optional SD database");
  Serial.println("[LANG] Modular speech cleanup + safer fuzzy correction + utterance intent sense");
  Serial.println("[MEMORY] Personal Memory V3: categories + importance + today/session/permanent lifetimes");
  Serial.println("[DIALOGUE] Conversation V3: topic continuity + corrections");
  Serial.println("[STUDY] Study Mode + AI-generated MCQ quiz engine");
  Serial.println("[DEV] Developer diagnostics + last-route/query/provider inspection");
  Serial.println("[VERIFY] Cross-source answer confidence + verified-cache gate");
  Serial.println("[TOOLS] V1.7 registry + permissions + safe 2-tool chaining");
  Serial.println("[NETWORK] V1.8 smart dual-Wi-Fi + forced offline + exhibition modes");
  Serial.println("[SCHOOL] V1.8 Class X timetable + unique-subject bag assistant + reminder");
  Serial.println("[OFFLINE] V1.8 built-in exhibition knowledge + learned verified NVS cache + pending queue");
  Serial.println("[STUDY] V2 online teaching/quiz + offline exhibition fallback");
  Serial.println("[PRODUCTIVITY] Persistent notes + Tasks V2 + language/history tools");
  Serial.println("[SELFTEST] Non-destructive regression suite enabled");
  Serial.println("[KNOWLEDGE] Generic concept variants; no topic-specific concept replacements");
  Serial.println("[SEMANTIC] Generic query frame + evidence scoring + web facts");
  Serial.println("[SUPPORT] Respectful motivation / wellbeing companion mode enabled");
  Serial.print("CPU        : "); Serial.print(ESP.getCpuFreqMHz()); Serial.println(" MHz");
  Serial.print("Flash      : "); Serial.print(ESP.getFlashChipSize()/(1024*1024)); Serial.println(" MB");
  Serial.print("PSRAM      : "); Serial.print(ESP.getPsramSize()/(1024*1024)); Serial.println(" MB");
  Serial.print("Free Heap  : "); Serial.println(ESP.getFreeHeap());
  Serial.print("Free PSRAM : "); Serial.println(ESP.getFreePsram());

  beginClockStorage();
  beginBrainStorage();

  connectWiFi();
  if(WiFi.status()==WL_CONNECTED) syncTime();

  // SD is optional. If no card is attached, KIRA continues with NVS.
  beginBrainSD();

  // Build/migrate searchable long-term memory after storage is ready.
  elliMemoryBegin();
  elliMemoryV2Begin();
  elliMemoryV3Begin();

  // RAM-only conversation / study layers and diagnostics.
  elliConversationV3Begin();
  elliStudyBegin();
  kiraVerificationBegin();
  kiraDevToolsBegin();

  // V1.8 resilient local layers. These use NVS/static flash and remain
  // available with no internet or SD card.
  kiraSchoolBegin();
  kiraOfflineBegin();

  // Phase 5H automation scenes. Scenes only modify the same
  // Universal logical device states used by Serial/voice/touch.
  kiraScenesBegin();

  // V1.7 registry owns persistent notes + PSRAM session history and bridges
  // selected proven V1.5 utilities without replacing the safe router.
  kiraToolEngineBegin();

  // Self-test starts after the tool registry so V1.6/V1.7 diagnostics can
  // inspect the initialized registry/storage without mutating user data.
  kiraSelfTestBegin();

  // KIRA v1: short-term context + persistent offline knowledge cache.
  kiraV1Begin();

  Serial.println();
  Serial.println("======================================");
  Serial.println("              ELLI READY");
  Serial.println("======================================");
  Serial.println("Try:");
  Serial.println("hey elli");
  Serial.println("hi elli how are you");
  Serial.println("hey elli turn on the fan");
  Serial.println("elli what is gravity");
  Serial.println("calculate (25+7)*3");
  Serial.println("convert 5 km to mi");
  Serial.println("what is the date");
  Serial.println("elli count until i say stop");
  Serial.println("set timer for 30 seconds");
  Serial.println("set alarm for 6:30 am");
  Serial.println("set another alarm for 7:00 am tomorrow");
  Serial.println("list alarms");
  Serial.println("cancel alarm 2");
  Serial.println("cancel all alarms");
  Serial.println("im bored");
  Serial.println("what are you doing");
  Serial.println("what is a tumbler");
  Serial.println("what is meant by quantum computing");
  Serial.println("what is space time");
  Serial.println("which is the highest populated city in india");
  Serial.println("which is the fastest animal on earth");
  Serial.println("which is the best university in the world");
  Serial.println("can you tell me any facts");
  Serial.println("tell me a quantum physics fact");
  Serial.println("which is the highest populated city in india");
  Serial.println("which is the best university in world");
  Serial.println("can you tell me any facts");
  Serial.println("another fact");
  Serial.println("what is space time");
  Serial.println("what is spacetime");
  Serial.println("weather in Yavatmal");
  Serial.println("100 usd to inr");
  Serial.println("capital of Japan");
  Serial.println("who wrote The Hobbit");
  Serial.println("molecular formula of water");
  Serial.println("web sources");
  Serial.println("source sense what is tumbler");
  Serial.println("my name is Atharva");
  Serial.println("what is my name");
  Serial.println("remember that i like electronics");
  Serial.println("what do you remember about me");
  Serial.println("i go to tuition at 3:45 pm daily");
  Serial.println("list routines");
  Serial.println("remind me to finish physics tomorrow at 7 pm");
  Serial.println("list tasks");
  Serial.println("complete task 1");
  Serial.println("motivate me");
  Serial.println("intent sense my name is Atharva");
  Serial.println("brain status");
  Serial.println("remember that i like electronics");
  Serial.println("what do i like");
  Serial.println("remember that my project is called KIRA");
  Serial.println("what is my project called");
  Serial.println("when do i go to tuition");
  Serial.println("memory search electronics");
  Serial.println("remember for this conversation that my test topic is robotics");
  Serial.println("show session memory");
  Serial.println("forget that i like electronics");
  Serial.println("study neuromorphic computing");
  Serial.println("study style simple");
  Serial.println("quiz me on electricity");
  Serial.println("quiz score");
  Serial.println("test all");
  Serial.println("show tool registry");
  Serial.println("tool status");
  Serial.println("network mode");
  Serial.println("exhibition mode on");
  Serial.println("timetable monday");
  Serial.println("fill my bag for wednesday");
  Serial.println("offline knowledge status");
  Serial.println("teach me esp32");
  Serial.println("save a note that test display tomorrow");
  Serial.println("show my notes");
  Serial.println("add finish physics homework to tasks");
  Serial.println("show completed tasks");
  Serial.println("calculate 18 percent of 2500");
  Serial.println("5 gb in mb");
  Serial.println("translate good morning to hindi");
  Serial.println("synonyms of intelligent");
  Serial.println("show last 5 queries");
  Serial.println("calculate 18 percent of 2500 and save the answer as a note");
  Serial.println("developer help");
  Serial.println("show last route");
  Serial.println("show provider status");
  Serial.println("show last verification");
  Serial.println("memory v3 status");
  Serial.println("memory categories");
  Serial.println("remember as project that KIRA uses ESP32-S3");
  Serial.println("remember important that my main project is KIRA");
  Serial.println("remember for today that test mode is enabled");
  Serial.println("context status");
  Serial.println("repeat that");
  Serial.println("summarize that");
  Serial.println("no i meant wifi");

  fpsTimer=millis(); lastFrameTime=millis();
}

void kiraBrainLoop(){

  updateRenderer();

  maintainWiFi();

  kiraOfflineTick();

  kiraSchoolTick();

  updateClockBrain();

  updatePersonalScheduler();


  // ==========================================================
  // GLOBAL BACKGROUND SPEECH
  // ==========================================================
  //
  // Timers, alarms, reminders and other subsystems can call
  // elliSay() without a user command.
  // ==========================================================

  kiraTtsFlushPending();


  // ==========================================================
  // ONE UNIVERSAL INPUT PATH
  // ==========================================================

  String command =
    "";


  bool fromVoice =
    kiraVoiceTakeCommand(
      command
    );


  if(
    !fromVoice &&
    Serial.available()
  ){

    command =
      Serial.readStringUntil(
        '\n'
      );


    command.trim();
  }


  // ==========================================================
  // UNIVERSAL BRAIN
  // ==========================================================

  if(
    command.length()
  ){

    processCommand(
      command
    );


    kiraDeviceIOApply(
      mainLight,
      fanState,
      chargerState,
      secondLight
    );


    elliVisualCommandEnd();


    // ========================================================
    // GLOBAL TTS HANDOFF
    // ========================================================
    //
    // Every elliSay() response generated by this command is
    // spoken here: local/offline, scene, timer, memory, tools,
    // Serial, offline knowledge, errors and online AI.
    // ========================================================

    bool ttsSpoken =
      kiraTtsFlushPending();


    if(
      fromVoice
    ){

      kiraVoiceCommandFinished(
        ttsSpoken
      );
    }
  }


  delay(
    1
  );
}