#include <Arduino.h>
#include <WiFi.h>
#include "kira_devtools.h"
#include "elli_query_frame.h"
#include "elli_conversation_v3.h"
#include "elli_memory.h"
#include "elli_study.h"
#include "kira_v1_router.h"
#include "kira_ai_web.h"
#include "kira_answer_verify.h"
#include "kira_tool_engine.h"
#include "kira_notes.h"
#include "kira_history.h"

String normalizeInput(String s);
void elliSay(const String& s);

namespace {

bool debugEnabled = false;
String lastRaw;
String lastNormalized;
String pendingRaw;
String pendingNormalized;
String lastRouteName = "NONE";
uint32_t lastElapsed = 0;
int32_t lastHeapDelta = 0;
int32_t lastPsramDelta = 0;
uint32_t bootMs = 0;

static String durationText(uint32_t ms){
  uint32_t seconds = ms / 1000UL;
  uint32_t days = seconds / 86400UL;
  seconds %= 86400UL;
  uint32_t hours = seconds / 3600UL;
  seconds %= 3600UL;
  uint32_t minutes = seconds / 60UL;
  seconds %= 60UL;

  String out;
  if(days){ out += String(days) + "d "; }
  if(days || hours){ out += String(hours) + "h "; }
  if(days || hours || minutes){ out += String(minutes) + "m "; }
  out += String(seconds) + "s";
  return out;
}

static void printLastRoute(){
  Serial.println();
  Serial.println("========== LAST ROUTE ==========");
  Serial.print("Raw input      : "); Serial.println(lastRaw);
  Serial.print("Normalized     : "); Serial.println(lastNormalized);
  Serial.print("Route          : "); Serial.println(lastRouteName);
  Serial.print("Elapsed        : "); Serial.print(lastElapsed); Serial.println(" ms");
  Serial.print("Heap delta     : "); Serial.println(lastHeapDelta);
  Serial.print("PSRAM delta    : "); Serial.println(lastPsramDelta);
  Serial.println("================================");
}

static void printContext(){
  Serial.println();
  Serial.println("========== CONTEXT STATUS ==========");
  Serial.print("Conversation V3 : "); Serial.println(elliConversationV3Status());
  Serial.print("Active topic    : "); Serial.println(elliConversationV3ActiveQuery());
  Serial.print("V1 clarification: "); Serial.println(kiraV1HasPendingContext() ? "PENDING" : "NONE");
  Serial.print("Session memories: "); Serial.println(sessionMemoryCount());
  Serial.print("Study mode      : "); Serial.println(elliStudyModeActive() ? "ON" : "OFF");
  Serial.println("====================================");
}

static void printSystem(){
  Serial.println();
  Serial.println("========== DEVELOPER STATUS ==========");
  Serial.print("Debug extras    : "); Serial.println(debugEnabled ? "ON" : "OFF");
  Serial.print("Uptime          : "); Serial.println(durationText(millis() - bootMs));
  Serial.print("CPU             : "); Serial.print(ESP.getCpuFreqMHz()); Serial.println(" MHz");
  Serial.print("Free heap       : "); Serial.println(ESP.getFreeHeap());
  Serial.print("Free PSRAM      : "); Serial.println(ESP.getFreePsram());
  Serial.print("Wi-Fi           : "); Serial.println(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "OFFLINE");
  if(WiFi.status() == WL_CONNECTED){
    Serial.print("RSSI            : "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
  }
  Serial.print("Last route      : "); Serial.println(lastRouteName);
  Serial.print("Memory V3       : "); Serial.println(memoryV3Status());
  Serial.print("Verification    : "); Serial.println(kiraLastVerificationSummary());
  Serial.print("Tool engine     : "); Serial.println(kiraToolStatus());
  Serial.print("Notes           : "); Serial.println(kiraNotesStatus());
  Serial.print("History         : "); Serial.println(kiraHistoryStatus());
  Serial.println("======================================");
}

static void printHelp(){
  Serial.println();
  Serial.println("========== KIRA DEVELOPER COMMANDS ==========");
  Serial.println("debug on / debug off / debug status");
  Serial.println("developer status");
  Serial.println("show last route");
  Serial.println("show last query");
  Serial.println("show query frame");
  Serial.println("query frame <question>");
  Serial.println("show context");
  Serial.println("show provider status");
  Serial.println("show last verification");
  Serial.println("show tool registry / show last tool / show tool history");
  Serial.println("show last 5 queries / show last 5 routes");
  Serial.println("show heap / show psram / show uptime");
  Serial.println("test all");
  Serial.println("=============================================");
}

static void printFrameFor(String query){
  query.trim();
  if(!query.length()){
    elliSay("I don't have a query to inspect yet.");
    return;
  }

  KiraV1::QueryFrame frame;
  if(KiraV1::analyzeQuery(query, frame)){
    KiraV1::printQueryFrame(frame);
  }else{
    Serial.print("[DEV] QueryFrame did not classify: ");
    Serial.println(query);
  }
}

} // namespace

void kiraDevToolsBegin(){
  bootMs = millis();
  debugEnabled = false;
  lastRaw = "";
  lastNormalized = "";
  pendingRaw = "";
  pendingNormalized = "";
  lastRouteName = "NONE";
  Serial.println("[DEV V1] Developer diagnostics ready. Type 'developer help'.");
}

bool kiraDevDebugEnabled(){ return debugEnabled; }
String kiraDevLastRoute(){ return lastRouteName; }
String kiraDevLastQuery(){ return lastNormalized; }

void kiraDevObserveInput(const String& raw,const String& normalized){
  pendingRaw = raw;
  pendingNormalized = normalized;
}

void kiraDevObserveRoute(
  const String& route,
  uint32_t elapsedMs,
  uint32_t heapBefore,
  uint32_t heapAfter,
  uint32_t psramBefore,
  uint32_t psramAfter
){
  // Developer/self-test inspection commands should not erase the meaningful
  // user query they are trying to inspect.
  if(route != "DEVELOPER" && route != "SELF TEST") {
    lastRaw = pendingRaw;
    lastNormalized = pendingNormalized;
    lastRouteName = route;
    lastElapsed = elapsedMs;
    lastHeapDelta = (int32_t)heapBefore - (int32_t)heapAfter;
    lastPsramDelta = (int32_t)psramBefore - (int32_t)psramAfter;
  }

  if(debugEnabled){
    Serial.print("[DEV TRACE] route="); Serial.print(route);
    Serial.print(" time="); Serial.print(elapsedMs); Serial.print("ms");
    Serial.print(" heapDelta="); Serial.print(lastHeapDelta);
    Serial.print(" psramDelta="); Serial.println(lastPsramDelta);
  }
}

bool kiraDevHandleCommand(String q){
  q = normalizeInput(q);

  if(q == "developer help" || q == "dev help"){
    printHelp();
    elliSay("Developer command list printed in Serial Monitor.");
    return true;
  }

  if(q == "debug on"){
    debugEnabled = true;
    elliSay("Developer debug extras are on. Existing core router logs remain unchanged.");
    return true;
  }
  if(q == "debug off"){
    debugEnabled = false;
    elliSay("Developer debug extras are off. Existing core router logs remain unchanged.");
    return true;
  }
  if(q == "debug status"){
    elliSay(String("Developer debug extras are ") + (debugEnabled ? "on." : "off."));
    return true;
  }

  if(q == "developer status" || q == "dev status"){
    printSystem();
    elliSay("Developer status printed in Serial Monitor.");
    return true;
  }

  if(q == "show last route" || q == "last route"){
    printLastRoute();
    elliSay("Last routing decision printed in Serial Monitor.");
    return true;
  }

  if(q == "show last query" || q == "last query"){
    Serial.print("[DEV] Last raw query: "); Serial.println(lastRaw);
    Serial.print("[DEV] Last normalized query: "); Serial.println(lastNormalized);
    elliSay(lastNormalized.length() ? "Last query printed in Serial Monitor." : "There is no previous query yet.");
    return true;
  }

  if(q == "show query frame" || q == "last query frame"){
    printFrameFor(lastNormalized);
    elliSay("QueryFrame inspection complete.");
    return true;
  }

  if(q.startsWith("query frame ")){
    printFrameFor(q.substring(12));
    elliSay("QueryFrame inspection complete.");
    return true;
  }

  if(q == "show context" || q == "developer context"){
    printContext();
    elliSay("Conversation context status printed in Serial Monitor.");
    return true;
  }

  if(q == "show provider status" || q == "provider status" || q == "show providers"){
    KiraV1::printAiProviderStatus();
    elliSay(KiraV1::aiProviderStatusCompact());
    return true;
  }

  if(q == "show last verification" || q == "verification status" || q == "show verification"){
    kiraPrintLastVerification();
    elliSay(kiraLastVerificationSummary());
    return true;
  }

  if(q == "show heap"){
    elliSay("Free heap is " + String(ESP.getFreeHeap()) + " bytes.");
    return true;
  }
  if(q == "show psram"){
    elliSay("Free PSRAM is " + String(ESP.getFreePsram()) + " bytes.");
    return true;
  }
  if(q == "show uptime"){
    elliSay("KIRA uptime is " + durationText(millis() - bootMs) + ".");
    return true;
  }

  return false;
}
