#include <Arduino.h>
#include "kira_tool_engine.h"
#include "kira_notes.h"
#include "kira_history.h"
#include "kira_language_tools.h"
#include "kira_network_v2.h"
#include "kira_school.h"
#include "kira_offline_brain.h"
#include "kira_next_config.h"
#include "kira_metrics.h"
#include "kira_diagnostics.h"

String normalizeInput(String s);
void elliSay(const String& s);

// Legacy-brain functions deliberately bridged instead of duplicated.
bool tryLocalMath(const String& question);
bool tryUnitConversion(const String& question);
bool handleTaskIntent(String q);
extern String lastElliUtterance;

namespace {

static const KiraToolSpec TOOLS[]={
  {"notes.create","Create a persistent note",KIRA_TOOL_SAFE_WRITE,true},
  {"notes.list","List saved notes",KIRA_TOOL_READ_ONLY,true},
  {"notes.search","Search saved notes",KIRA_TOOL_READ_ONLY,true},
  {"notes.delete","Delete one note",KIRA_TOOL_SAFE_WRITE,true},
  {"notes.clear","Clear all notes with confirmation",KIRA_TOOL_DESTRUCTIVE,true},
  {"tasks.create","Create a task with optional deadline",KIRA_TOOL_SAFE_WRITE,true},
  {"tasks.list","List pending/completed tasks",KIRA_TOOL_READ_ONLY,true},
  {"tasks.complete","Complete a task",KIRA_TOOL_SAFE_WRITE,true},
  {"tasks.reopen","Reopen a completed task",KIRA_TOOL_SAFE_WRITE,true},
  {"tasks.delete","Delete a task",KIRA_TOOL_SAFE_WRITE,true},
  {"tasks.clear_completed","Clear completed tasks with confirmation",KIRA_TOOL_DESTRUCTIVE,true},
  {"calculator.solve","Local arithmetic and percentages",KIRA_TOOL_READ_ONLY,true},
  {"unit.convert","Local unit conversion",KIRA_TOOL_READ_ONLY,true},
  {"dictionary.lookup","Definitions, synonyms and antonyms",KIRA_TOOL_NETWORK,true},
  {"translation.translate","Translate text through AI providers",KIRA_TOOL_NETWORK,true},
  {"history.read","Read session query/route/tool history",KIRA_TOOL_READ_ONLY,true},
  {"tools.inspect","Inspect the tool registry and last tool",KIRA_TOOL_READ_ONLY,true},
  {"network.status","Inspect dual-Wi-Fi/connectivity state",KIRA_TOOL_READ_ONLY,true},
  {"network.control","Rescan Wi-Fi or switch offline/exhibition modes",KIRA_TOOL_NETWORK,true},
  {"school.timetable","Read the local Class X timetable",KIRA_TOOL_READ_ONLY,true},
  {"school.bag","Run the unique-subject bag checklist",KIRA_TOOL_SAFE_WRITE,true},
  {"school.reminder","Manage the local bag reminder/cutoff",KIRA_TOOL_SAFE_WRITE,true},
  {"offline.knowledge","Inspect local exhibition knowledge/cache",KIRA_TOOL_READ_ONLY,true},
  {"offline.pending","Manage pending online questions",KIRA_TOOL_SAFE_WRITE,true},
  {"study.teach","Study Mode V2 online/offline teaching bridge",KIRA_TOOL_NETWORK,false},
  {"memory.manage","Existing Personal Memory V3 bridge",KIRA_TOOL_SAFE_WRITE,false},
  {"timer.control","Existing clock/timer bridge",KIRA_TOOL_SAFE_WRITE,false},
  {"alarm.control","Existing persistent alarm bridge",KIRA_TOOL_SAFE_WRITE,false},
  {"device.control","Existing device-action bridge",KIRA_TOOL_DEVICE_ACTION,false},
  {"web.search","Existing verified web/AI bridge",KIRA_TOOL_NETWORK,false},
  {"study.quiz","Existing Study Mode bridge",KIRA_TOOL_NETWORK,false},
  {"system.inspect","KIRA Next full runtime diagnostics",KIRA_TOOL_READ_ONLY,true}
};

static const uint8_t TOOL_COUNT=sizeof(TOOLS)/sizeof(TOOLS[0]);

String lastTool="NONE";
String lastAction="NONE";
bool lastSuccess=false;
uint32_t lastLatency=0;
uint32_t executionCounter=0;
uint32_t rejectedCounter=0;
uint32_t nextStructuredCallId=1;

static bool startsWithAny(const String& q,const char* const values[],size_t count){
  for(size_t i=0;i<count;i++) if(q.startsWith(values[i])) return true;
  return false;
}

static bool hasNaturalTaskScheduleCue(const String& q){
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

static bool looksLikeTask(const String& q){
  if(q=="list tasks" || q=="show tasks" || q=="pending tasks" ||
     q=="show completed tasks" || q=="completed tasks" ||
     q=="show all tasks" || q=="task status" ||
     q=="clear completed tasks" || q=="confirm clear completed tasks") return true;

  const char* prefixes[]={
    "remind me to ","remind me about ","add task ","add a task ",
    "create task ","create a task ","task ","remember to ",
    "complete task ","mark task ","finish task ","reopen task ",
    "cancel task ","delete task ","remove task ","set task "
  };
  if(startsWithAny(q,prefixes,sizeof(prefixes)/sizeof(prefixes[0]))) return true;

  return q.startsWith("add ") && (
    q.endsWith(" to tasks") || q.endsWith(" to my tasks") || hasNaturalTaskScheduleCue(q)
  );
}

static bool looksLikeMath(const String& q){
  if(q.startsWith("calculate ") || q.startsWith("compute ") || q.startsWith("solve ") || q.startsWith("work out ")) return true;
  if(q.indexOf(" percent of ")>=0 || q.indexOf("% of ")>=0) return true;

  bool digit=false;
  for(size_t i=0;i<q.length();i++) if(isdigit((unsigned char)q[i])) { digit=true; break; }
  if(!digit) return false;

  return q.indexOf("+")>=0 || q.indexOf("*")>=0 || q.indexOf("^")>=0 ||
    q.indexOf(" divided by ")>=0 || q.indexOf(" multiplied by ")>=0 ||
    q.indexOf(" plus ")>=0 || q.indexOf(" minus ")>=0 ||
    q.indexOf(" square root")>=0 || q.indexOf(" squared")>=0 || q.indexOf(" cubed")>=0;
}

static bool looksLikeUnit(const String& q){
  if(q.startsWith("convert ")) return true;
  if(!q.length() || !(isdigit((unsigned char)q[0]) || q[0]=='-' || q[0]=='+')) return false;
  return q.indexOf(" in ")>=0 || q.indexOf(" to ")>=0;
}

static bool noteCommand(const String& q){
  return q=="note" || q=="notes" || q=="show notes" || q=="show my notes" || q=="list notes" ||
    q=="notes status" || q=="note status" || q=="clear notes" || q=="clear all notes" ||
    q=="delete all notes" || q=="confirm clear notes" ||
    q.startsWith("save a note") || q.startsWith("save note") || q.startsWith("add a note") ||
    q.startsWith("add note") || q.startsWith("note that ") || q.startsWith("important note ") ||
    q.startsWith("save important note") || q.startsWith("save an important note") ||
    q.startsWith("search notes for ") || q.startsWith("find notes for ") ||
    q.startsWith("find note about ") || q.startsWith("show note ") ||
    q.startsWith("delete note ") || q.startsWith("remove note ");
}

static bool historyCommand(const String& q){
  return q=="history status" || q=="conversation history status" ||
    q=="what did i ask before" || q=="what was my last question" ||
    q=="repeat my last question" || q=="repeat your last answer" ||
    q=="what was your last answer" || q=="show last 5 queries" ||
    q=="show query history" || q=="query history" || q=="show last 5 routes" ||
    q=="show route history" || q=="route history" || q=="show last 5 tools" ||
    q=="show tool history" || q=="tool history" || q=="show conversation history" ||
    q=="conversation history";
}

static bool networkCommand(const String& q){
  return q=="network mode" || q=="offline status" || q=="network status" || q=="wifi status" ||
    q=="show wifi signal" || q=="show wifi networks" || q=="provider health" || q=="show provider health" ||
    q=="wifi rescan" || q=="wifi scan" || q=="retry internet" || q=="reconnect wifi" ||
    q=="offline mode on" || q=="offline mode off" || q=="force offline on" || q=="force offline off" ||
    q=="exhibition mode on" || q=="exhibition mode off" || q=="start exhibition mode" ||
    q=="stop exhibition mode" || q=="exhibition mode" || q=="exhibition status";
}

static bool systemInspectCommand(const String& q){
  return
    q=="system health" ||
    q=="health status" ||
    q=="diagnostics" ||
    q=="diagnostic status" ||
    q=="system diagnostics" ||
    q=="kira diagnostics" ||
    q=="system status" ||
    q=="runtime status";
}


static bool offlineCommand(const String& q){
  return q=="offline knowledge status" || q=="knowledge status" || q=="cache status" || q=="offline cache status" ||
    q=="show cached answers" || q=="show offline cache" || q=="show pending queries" || q=="pending queries" ||
    q=="show pending questions" || q=="clear pending queries" || q=="clear pending questions" ||
    q=="retry pending queries" || q=="retry pending questions" || q=="retry pending query" ||
    q.startsWith("delete pending query ") || q.startsWith("delete pending question ");
}

static String schoolToolName(const String& q){
  if(q.indexOf("bag reminder")>=0 || q.indexOf("bag cutoff")>=0) return "school.reminder";
  if(q.indexOf("my bag")>=0 || q=="bag status" || q=="continue bag" || q=="continue my bag" ||
     q=="cancel bag" || q=="restart bag" || q=="done" || q=="packed" || q.startsWith("done ") || q.startsWith("packed ")) return "school.bag";
  return "school.timetable";
}

static String notesAction(const String& q){
  if(q=="clear notes" || q=="clear all notes" || q=="delete all notes" || q=="confirm clear notes") return "clear";
  if(q.startsWith("delete note ") || q.startsWith("remove note ")) return "delete";
  if(q.startsWith("search notes") || q.startsWith("find note")) return "search";
  if(q.startsWith("show note ") || q=="notes" || q.indexOf("show")>=0 || q.indexOf("list")>=0) return "list";
  return "create";
}

static String tasksAction(const String& q){
  if(q=="clear completed tasks" || q=="confirm clear completed tasks") return "clear_completed";
  if(q.startsWith("reopen task ")) return "reopen";
  if(q.startsWith("complete task ") || q.startsWith("mark task ") || q.startsWith("finish task ")) return "complete";
  if(q.startsWith("cancel task ") || q.startsWith("delete task ") || q.startsWith("remove task ")) return "delete";
  if(q.indexOf("show")>=0 || q.indexOf("list")>=0 || q=="pending tasks" || q=="completed tasks" || q=="task status") return "list";
  return "create";
}

static const KiraToolSpec* findToolSpec(
  const String& toolName
){
  for(
    uint8_t i=0;
    i<TOOL_COUNT;
    i++
  ){
    if(
      toolName==
      TOOLS[i].name
    ){
      return &TOOLS[i];
    }
  }

  return nullptr;
}


static bool explicitConfirmationPresent(
  const String& normalized
){
  return
    normalized.startsWith(
      "confirm "
    ) ||
    normalized.indexOf(
      " confirm "
    )>=0;
}


static String actionForTool(
  const String& tool,
  const String& q
){
  if(tool.startsWith("network.")){
    return
      tool=="network.status"
        ? "status"
        : "control";
  }

  if(tool.startsWith("school.")){
    return tool.substring(7);
  }

  if(tool.startsWith("offline.")){
    return
      tool=="offline.pending"
        ? "pending"
        : "inspect";
  }

  if(tool.startsWith("notes.")){
    return notesAction(q);
  }

  if(tool.startsWith("tasks.")){
    return tasksAction(q);
  }

  if(tool=="calculator.solve") return "solve";
  if(tool=="unit.convert") return "convert";
  if(tool=="translation.translate") return "translate";
  if(tool=="dictionary.lookup") return "lookup";
  if(tool=="history.read") return "read";
  if(tool=="tools.inspect") return "inspect";
  if(tool=="system.inspect") return "diagnostics";

  if(tool=="study.teach") return "teach";
  if(tool=="study.quiz") return "quiz";
  if(tool=="memory.manage") return "manage";
  if(tool=="timer.control") return "control";
  if(tool=="alarm.control") return "control";
  if(tool=="device.control") return "control";
  if(tool=="web.search") return "search";

  return "run";
}


static void rememberExecution(const String& tool,const String& action,bool success,uint32_t start){
  lastTool=tool;
  lastAction=action;
  lastSuccess=success;
  lastLatency=millis()-start;
  executionCounter++;

#if KIRA_TYPED_TOOLS_ENABLED
  kiraMetricsIncrement(
    KIRA_METRIC_TOOL_CALLS
  );

  if(!success){
    kiraMetricsIncrement(
      KIRA_METRIC_TOOL_FAILURES
    );
  }
#endif

  kiraHistoryObserveTool(tool,action,success,lastLatency);

  Serial.print("[TOOL] "); Serial.print(tool); Serial.print("."); Serial.print(action);
  Serial.print(" | "); Serial.print(success ? "OK" : "FAIL"); Serial.print(" | ");
  Serial.print(lastLatency); Serial.println(" ms");
}

static bool executeEngineOwnedCall(
  const KiraToolCall& call,
  bool allowChain,
  KiraToolResult* structuredResult
){
  uint32_t start=
    millis();

  bool success=false;

  const String& q=
    call.input;

  const String& tool=
    call.tool;

  String action=
    call.action;


  if(tool.startsWith("network.")){
    success=
      kiraNetworkHandleCommand(q);
  }
  else if(tool.startsWith("school.")){
    success=
      kiraSchoolHandleCommand(q);
  }
  else if(tool.startsWith("offline.")){
    success=
      kiraOfflineHandleCommand(q);
  }
  else if(tool.startsWith("notes.")){
    success=
      kiraNotesHandleCommand(q);
  }
  else if(tool.startsWith("tasks.")){
    success=
      handleTaskIntent(q);
  }
  else if(tool=="calculator.solve"){
    success=
      tryLocalMath(q);
  }
  else if(tool=="unit.convert"){
    success=
      tryUnitConversion(q);
  }
  else if(tool=="translation.translate"){
    success=
      kiraHandleTranslationTool(q);
  }
  else if(tool=="dictionary.lookup"){
    success=
      kiraHandleDictionaryTool(q);
  }
  else if(tool=="history.read"){
    success=
      kiraHistoryHandleCommand(q);
  }
  else if(tool=="system.inspect"){
    kiraDiagnosticsPrintFull();

    elliSay(
      "KIRA diagnostics are printed in Serial Monitor."
    );

    success=true;
  }


  if(success){
    rememberExecution(
      tool,
      action,
      true,
      start
    );

    if(structuredResult){
      structuredResult->callId=
        call.id;

      structuredResult->tool=
        tool;

      structuredResult->action=
        action;

      structuredResult->status=
        KIRA_TOOL_CALL_OK;

      structuredResult->handled=
        true;

      structuredResult->success=
        true;

      structuredResult->latencyMs=
        millis()-start;

      structuredResult->detail=
        "OK";
    }

    return true;
  }


  // Classification is intentionally broader than concrete execution.
  // If the concrete parser declines, preserve the proven legacy router.
  if(!allowChain){
    if(structuredResult){
      structuredResult->callId=
        call.id;

      structuredResult->tool=
        tool;

      structuredResult->action=
        action;

      structuredResult->status=
        KIRA_TOOL_CALL_FAILED;

      structuredResult->handled=
        false;

      structuredResult->success=
        false;

      structuredResult->latencyMs=
        millis()-start;

      structuredResult->detail=
        "Concrete parser declined";
    }

    return false;
  }


  rememberExecution(
    tool,
    action,
    false,
    start
  );

  if(structuredResult){
    structuredResult->callId=
      call.id;

    structuredResult->tool=
      tool;

    structuredResult->action=
      action;

    structuredResult->status=
      KIRA_TOOL_CALL_FAILED;

    structuredResult->handled=
      true;

    structuredResult->success=
      false;

    structuredResult->latencyMs=
      millis()-start;

    structuredResult->detail=
      "Execution failed";
  }

  return false;
}


static bool executeSingle(
  String q,
  bool allowChain=false
){
  KiraToolCall call={};

  if(
    !kiraToolPlanCommand(
      q,
      call
    )
  ){
    return false;
  }


  // Protected legacy bridges remain handled by the proven brain path.
  if(!call.engineOwned){
    return false;
  }


  kiraToolPrintCall(
    call
  );


  return
    executeEngineOwnedCall(
      call,
      allowChain,
      nullptr
    );
}


static int findSaveNoteChain(String q,String& left){
  const char* markers[]={
    " and save the answer as a note",
    " and save the result as a note",
    " and save it as a note",
    " and add the answer to notes",
    " and add the result to notes"
  };
  for(size_t i=0;i<sizeof(markers)/sizeof(markers[0]);i++){
    int p=q.indexOf(markers[i]);
    if(p>0){ left=q.substring(0,p); left.trim(); return p; }
  }
  return -1;
}

static bool executeSafeTwoToolChain(String q){
  String left;
  if(findSaveNoteChain(q,left)<0) return false;

  String firstTool=kiraToolClassifyCommand(left);
  if(firstTool!="calculator.solve" && firstTool!="unit.convert") return false;

  // Chaining V1 is deliberately conservative: read-only local computation
  // may feed one safe-write note. NETWORK, DEVICE_ACTION and DESTRUCTIVE
  // tools cannot be chained automatically.
  if(kiraToolPermissionFor(firstTool)!=KIRA_TOOL_READ_ONLY ||
     kiraToolPermissionFor("notes.create")!=KIRA_TOOL_SAFE_WRITE){
    return false;
  }

  Serial.print("[TOOL PLAN] 1="); Serial.print(firstTool);
  Serial.println(" -> 2=notes.create");

  if(!executeSingle(left,true)){
    elliSay("The first tool in that chain failed, so I didn't save a note.");
    return true;
  }

  String result=lastElliUtterance;
  result.trim();
  if(!result.length()){
    elliSay("The first tool finished without a saveable result, so I didn't create the note.");
    return true;
  }

  uint32_t start=millis();
  uint16_t id=0;
  String error;
  bool saved=kiraNotesCreate("Tool result: "+result,false,id,error);
  rememberExecution("notes.create","create",saved,start);

  if(saved) elliSay("I also saved that result as note id "+String(id)+".");
  else elliSay("The calculation worked, but I couldn't save the note: "+error);
  return true;
}

static void printToolStatusDetails(){
  Serial.println();
  Serial.println("========== TOOL ENGINE STATUS ==========");
  Serial.print("Registered tools : "); Serial.println(TOOL_COUNT);
  Serial.print("Executions       : "); Serial.println(executionCounter);
  Serial.print("Rejected calls   : "); Serial.println(rejectedCounter);
  Serial.println("Typed calls      : ENABLED");
  Serial.print("Last tool        : "); Serial.println(lastTool);
  Serial.print("Last action      : "); Serial.println(lastAction);
  Serial.print("Last success     : "); Serial.println(lastSuccess ? "YES" : "NO");
  Serial.print("Last latency     : "); Serial.print(lastLatency); Serial.println(" ms");
  Serial.print("Notes            : "); Serial.println(kiraNotesStatus());
  Serial.print("History          : "); Serial.println(kiraHistoryStatus());
  Serial.println("========================================");
}

} // namespace

void kiraToolEngineBegin(){
  lastTool="NONE";
  lastAction="NONE";
  lastSuccess=false;
  lastLatency=0;
  executionCounter=0;
  rejectedCounter=0;
  nextStructuredCallId=1;

  kiraNotesBegin();
  kiraHistoryBegin();

  Serial.print("[TOOLS V2] Structured registry ready: ");
  Serial.print(TOOL_COUNT);
  Serial.println(" tools | typed calls + permission metadata + protected legacy bridges");
}

const char* kiraToolPermissionName(KiraToolPermission permission){
  switch(permission){
    case KIRA_TOOL_READ_ONLY: return "READ_ONLY";
    case KIRA_TOOL_SAFE_WRITE: return "SAFE_WRITE";
    case KIRA_TOOL_NETWORK: return "NETWORK";
    case KIRA_TOOL_DEVICE_ACTION: return "DEVICE_ACTION";
    case KIRA_TOOL_DESTRUCTIVE: return "DESTRUCTIVE";
    default: return "UNKNOWN";
  }
}

uint8_t kiraToolRegistryCount(){ return TOOL_COUNT; }

bool kiraToolRegistryHealthy(){
  if(TOOL_COUNT<10) return false;
  for(uint8_t i=0;i<TOOL_COUNT;i++){
    if(!TOOLS[i].name || !strlen(TOOLS[i].name)) return false;
    for(uint8_t j=i+1;j<TOOL_COUNT;j++) if(String(TOOLS[i].name)==String(TOOLS[j].name)) return false;
  }
  return true;
}

KiraToolPermission kiraToolPermissionFor(const String& toolName){
  for(uint8_t i=0;i<TOOL_COUNT;i++) if(toolName==TOOLS[i].name) return TOOLS[i].permission;
  return KIRA_TOOL_READ_ONLY;
}

String kiraToolClassifyCommand(const String& original){
  String q=normalizeInput(original);

  if(
    systemInspectCommand(q)
  ){
    return "system.inspect";
  }

  if(networkCommand(q)){
    if(q=="network mode" || q=="offline status" || q=="network status" || q=="wifi status" ||
       q=="show wifi signal" || q=="show wifi networks" || q=="provider health" || q=="show provider health" ||
       q=="exhibition mode" || q=="exhibition status") return "network.status";
    return "network.control";
  }

  if(kiraSchoolLooksLikeCommand(q)) return schoolToolName(q);
  if(offlineCommand(q)){
    if(q.indexOf("pending")>=0) return "offline.pending";
    return "offline.knowledge";
  }

  // Study tools remain protected legacy bridges: classification is visible
  // to V1.7 diagnostics, but execution intentionally falls through to
  // elliStudyHandleCommand() in the proven brain router.
  if(q.startsWith("teach me ") || q.startsWith("teach ")) return "study.teach";
  if(q.startsWith("quiz me") || q=="next question" || q=="quiz score") return "study.quiz";

  if(noteCommand(q)){
    String a=notesAction(q);
    if(a=="create") return "notes.create";
    if(a=="search") return "notes.search";
    if(a=="delete") return "notes.delete";
    if(a=="clear") return "notes.clear";
    return "notes.list";
  }

  if(looksLikeTask(q)){
    String a=tasksAction(q);
    if(a=="create") return "tasks.create";
    if(a=="complete") return "tasks.complete";
    if(a=="reopen") return "tasks.reopen";
    if(a=="delete") return "tasks.delete";
    if(a=="clear_completed") return "tasks.clear_completed";
    return "tasks.list";
  }

  if(kiraLooksLikeTranslationTool(q)) return "translation.translate";
  if(kiraLooksLikeDictionaryTool(q)) return "dictionary.lookup";
  if(historyCommand(q)) return "history.read";
  if(looksLikeMath(q)) return "calculator.solve";
  if(looksLikeUnit(q)) return "unit.convert";

  return "";
}

const char* kiraToolCallStatusName(
  KiraToolCallStatus status
){
  switch(status){
    case KIRA_TOOL_CALL_UNPLANNED: return "UNPLANNED";
    case KIRA_TOOL_CALL_READY: return "READY";
    case KIRA_TOOL_CALL_EXECUTING: return "EXECUTING";
    case KIRA_TOOL_CALL_OK: return "OK";
    case KIRA_TOOL_CALL_FAILED: return "FAILED";
    case KIRA_TOOL_CALL_REJECTED: return "REJECTED";
    case KIRA_TOOL_CALL_LEGACY_BRIDGE: return "LEGACY_BRIDGE";
  }

  return "UNKNOWN";
}


bool kiraToolPlanCommand(
  const String& original,
  KiraToolCall& outCall
){
  outCall=
    KiraToolCall{};

  String q=
    normalizeInput(
      original
    );

  String tool=
    kiraToolClassifyCommand(
      q
    );

  if(!tool.length()){
    return false;
  }


  const KiraToolSpec* spec=
    findToolSpec(
      tool
    );

  if(!spec){
    Serial.print(
      "[TOOL PLAN] registry miss: "
    );

    Serial.println(
      tool
    );

    return false;
  }


  outCall.id=
    nextStructuredCallId++;

  if(nextStructuredCallId==0){
    nextStructuredCallId=1;
  }

  outCall.tool=
    tool;

  outCall.action=
    actionForTool(
      tool,
      q
    );

  outCall.input=
    q;

  outCall.permission=
    spec->permission;

  outCall.engineOwned=
    spec->engineOwned;

  outCall.requiresNetwork=
    spec->permission==
      KIRA_TOOL_NETWORK;

  outCall.requiresConfirmation=
    spec->permission==
      KIRA_TOOL_DESTRUCTIVE;

  outCall.confirmationPresent=
    explicitConfirmationPresent(
      q
    );

  outCall.status=
    spec->engineOwned
      ? KIRA_TOOL_CALL_READY
      : KIRA_TOOL_CALL_LEGACY_BRIDGE;

  return true;
}


bool kiraToolAuthorizeCall(
  const KiraToolCall& call,
  String& reason
){
  reason="";


  if(
    call.status==
      KIRA_TOOL_CALL_UNPLANNED ||
    !call.tool.length()
  ){
    reason=
      "Call is not planned";

    return false;
  }


  const KiraToolSpec* spec=
    findToolSpec(
      call.tool
    );

  if(!spec){
    reason=
      "Tool is not registered";

    return false;
  }


  if(
    spec->permission!=
    call.permission
  ){
    reason=
      "Permission metadata mismatch";

    return false;
  }


  if(
    call.requiresConfirmation &&
    !call.confirmationPresent
  ){
    reason=
      "Explicit confirmation required";

    return false;
  }


  // DEVICE_ACTION is never executed directly here. KIRA's established
  // device firewall remains the only path that may authorize device state.

  return true;
}


bool kiraToolExecuteCall(
  const KiraToolCall& call,
  KiraToolResult& outResult
){
  outResult=
    KiraToolResult{};

  outResult.callId=
    call.id;

  outResult.tool=
    call.tool;

  outResult.action=
    call.action;


  String reason;

  if(
    !kiraToolAuthorizeCall(
      call,
      reason
    )
  ){
    rejectedCounter++;

    kiraMetricsIncrement(
      KIRA_METRIC_TOOL_REJECTIONS
    );

    outResult.status=
      KIRA_TOOL_CALL_REJECTED;

    outResult.handled=
      true;

    outResult.success=
      false;

    outResult.detail=
      reason;

    Serial.print(
      "[TOOL AUTH] REJECT id="
    );

    Serial.print(
      call.id
    );

    Serial.print(
      " tool="
    );

    Serial.print(
      call.tool
    );

    Serial.print(
      " reason="
    );

    Serial.println(
      reason
    );

    return false;
  }


  if(!call.engineOwned){
    outResult.status=
      KIRA_TOOL_CALL_LEGACY_BRIDGE;

    outResult.handled=
      false;

    outResult.success=
      false;

    outResult.detail=
      "Protected legacy bridge";

    return false;
  }


  return
    executeEngineOwnedCall(
      call,
      false,
      &outResult
    );
}


void kiraToolPrintCall(
  const KiraToolCall& call
){
#if KIRA_TYPED_TOOLS_ENABLED
  Serial.print(
    "[TOOL CALL] id="
  );

  Serial.print(
    call.id
  );

  Serial.print(
    " tool="
  );

  Serial.print(
    call.tool
  );

  Serial.print(
    " action="
  );

  Serial.print(
    call.action
  );

  Serial.print(
    " permission="
  );

  Serial.print(
    kiraToolPermissionName(
      call.permission
    )
  );

  Serial.print(
    " owner="
  );

  Serial.print(
    call.engineOwned
      ? "ENGINE"
      : "LEGACY"
  );

  Serial.print(
    " status="
  );

  Serial.println(
    kiraToolCallStatusName(
      call.status
    )
  );
#else
  (void)call;
#endif
}


void kiraToolPrintResult(
  const KiraToolResult& result
){
#if KIRA_TYPED_TOOLS_ENABLED
  Serial.print(
    "[TOOL RESULT] id="
  );

  Serial.print(
    result.callId
  );

  Serial.print(
    " tool="
  );

  Serial.print(
    result.tool
  );

  Serial.print(
    " status="
  );

  Serial.print(
    kiraToolCallStatusName(
      result.status
    )
  );

  Serial.print(
    " latency="
  );

  Serial.print(
    result.latencyMs
  );

  Serial.print(
    "ms detail="
  );

  Serial.println(
    result.detail
  );
#else
  (void)result;
#endif
}


bool kiraToolRecognizesChain(const String& original){
  String q=normalizeInput(original);
  String left;
  if(findSaveNoteChain(q,left)<0) return false;
  String first=kiraToolClassifyCommand(left);
  return first=="calculator.solve" || first=="unit.convert";
}

String kiraToolLastTool(){ return lastTool; }
String kiraToolLastAction(){ return lastAction; }
uint32_t kiraToolExecutionCount(){ return executionCounter; }
uint32_t kiraToolRejectedCount(){ return rejectedCounter; }

String kiraToolStatus(){
  return String("Tool Engine: ")+String(TOOL_COUNT)+" registered, last="+lastTool+".";
}

void kiraToolPrintRegistry(){
  Serial.println();
  Serial.println("========== KIRA TOOL REGISTRY ==========");
  for(uint8_t i=0;i<TOOL_COUNT;i++){
    Serial.print(i+1); Serial.print(". "); Serial.print(TOOLS[i].name);
    Serial.print(" | "); Serial.print(kiraToolPermissionName(TOOLS[i].permission));
    Serial.print(" | "); Serial.print(TOOLS[i].engineOwned ? "ENGINE" : "LEGACY-BRIDGE");
    Serial.print(" | "); Serial.println(TOOLS[i].description);
  }
  Serial.println("========================================");
}

void kiraToolPrintLast(){
  Serial.println();
  Serial.println("========== LAST TOOL ==========");
  Serial.print("Tool       : "); Serial.println(lastTool);
  Serial.print("Action     : "); Serial.println(lastAction);
  Serial.print("Permission : "); Serial.println(kiraToolPermissionName(kiraToolPermissionFor(lastTool)));
  Serial.print("Success    : "); Serial.println(lastSuccess ? "YES" : "NO");
  Serial.print("Latency    : "); Serial.print(lastLatency); Serial.println(" ms");
  Serial.println("===============================");
}

bool kiraToolHandleCommand(String q){
  q=normalizeInput(q);

  if(q=="tool status" || q=="tools status"){
    uint32_t start=millis();
    printToolStatusDetails();
    elliSay(kiraToolStatus());
    rememberExecution("tools.inspect","status",true,start);
    return true;
  }
  if(q=="show tool registry" || q=="tool registry" || q=="show tools"){
    uint32_t start=millis();
    kiraToolPrintRegistry();
    elliSay("The tool registry is printed in Serial Monitor.");
    rememberExecution("tools.inspect","registry",true,start);
    return true;
  }
  if(q=="show last tool" || q=="last tool"){
    uint32_t start=millis();
    String previous=lastTool;
    kiraToolPrintLast();
    elliSay(previous=="NONE" ? "No tool has run yet." : "Last tool details printed in Serial Monitor.");
    rememberExecution("tools.inspect","last",true,start);
    return true;
  }

  if(executeSafeTwoToolChain(q)) return true;
  return executeSingle(q,false);
}
