#include <Arduino.h>
#include <WiFi.h>
#include "kira_selftest.h"
#include "elli_language.h"
#include "elli_intent.h"
#include "elli_query_frame.h"
#include "kira_ai_web.h"
#include "elli_memory.h"
#include "kira_storage.h"
#include "elli_conversation_v3.h"
#include "elli_study.h"
#include "kira_answer_verify.h"
#include "kira_devtools.h"
#include "kira_tool_engine.h"
#include "kira_notes.h"
#include "kira_history.h"
#include "kira_language_tools.h"
#include "kira_network_v2.h"
#include "kira_school.h"
#include "kira_offline_brain.h"

String normalizeInput(String s);
void elliSay(const String& s);

namespace {

struct TestCounters {
  uint16_t passed = 0;
  uint16_t failed = 0;
  uint16_t warnings = 0;
};

void printResult(TestCounters& c, bool ok, const String& name){
  if(ok){
    c.passed++;
    Serial.print("[PASS] ");
  }else{
    c.failed++;
    Serial.print("[FAIL] ");
  }
  Serial.println(name);
}

void printWarning(TestCounters& c, const String& name){
  c.warnings++;
  Serial.print("[WARN] ");
  Serial.println(name);
}

bool containsNormalized(const String& value, const String& needle){
  return normalizeInput(value).indexOf(normalizeInput(needle)) >= 0;
}

void runParserTests(TestCounters& c){
  String cleaned = normalizeSpeechUtterance("tellme more about it");
  printResult(c, normalizeInput(cleaned) == "tell me more about it", "language: tellme -> tell me");

  String decimalCleaned = normalizeSpeechUtterance("2.5 kg in grams");
  printResult(c, normalizeInput(decimalCleaned) == "2.5 kg in grams", "language: decimal 2.5 is preserved");
  String dottedTime = normalizeSpeechUtterance("alarm at 3.45 pm");
  printResult(c, normalizeInput(dottedTime).indexOf("3:45 pm") >= 0, "language: dotted clock time becomes 3:45 pm");
  printResult(c, normalizeInput("2.50 volts") == "2.50 volts", "normalizer: numeric decimal punctuation survives");
  printResult(c, normalizeInput("alarm at 19:00") == "alarm at 19:00", "normalizer: numeric clock colon survives");

  printResult(c, detectUtteranceIntent("turn fan on") == UTT_DEVICE, "intent: reordered device command stays local");
  printResult(c, detectUtteranceIntent("turn the light off") == UTT_DEVICE, "intent: natural device command stays local");
  printResult(c, detectUtteranceIntent("what happens when you turn a fan on") == UTT_QUESTION, "intent: device wording inside a question is not an action");
  printResult(c, detectUtteranceIntent("set timer for 10 seconds") == UTT_CLOCK, "intent: timer stays local clock");
  printResult(c, detectUtteranceIntent("remember that i like electronics") == UTT_PROFILE, "intent: explicit memory stays profile/local");
  printResult(c, detectUtteranceIntent("add test kira tomorrow at 7 pm") == UTT_TASK, "intent: scheduled natural add-command becomes task");

  KiraV1::QueryFrame frame;
  bool parsed = KiraV1::analyzeQuery("which is the best lip gloss", frame);
  printResult(c, parsed, "query frame: ranking parses");
  if(parsed){
    printResult(c, frame.op == KiraV1::OP_BEST, "query frame: BEST operator");
    printResult(c, containsNormalized(frame.subject, "lip gloss") || containsNormalized(frame.entity, "lip gloss"), "query frame: compound subject 'lip gloss' preserved");
    printResult(c, !frame.metric.length(), "query frame: 'lip' is not misread as metric");
  }

  parsed = KiraV1::analyzeQuery("what is neuromorphic computing", frame);
  printResult(c, parsed && containsNormalized(frame.subject + " " + frame.entity, "neuromorphic computing"), "query frame: multi-word knowledge subject preserved");
}

void runMemoryTests(TestCounters& c){
  int persistent = memoryCount();
  int session = sessionMemoryCount();
  printResult(c, persistent >= 0 && persistent <= MAX_BRAIN_MEMORIES, "memory: persistent cache count valid");
  printResult(c, session >= 0 && session <= 10, "memory: session count valid");

  Serial.print("[INFO] ");
  Serial.println(memoryV2Status());
  Serial.print("[INFO] ");
  Serial.println(memoryV3Status());

  printResult(c, elliInferMemoryCategory("my project is called kira") == ELLI_MEM_PROJECT, "memory v3: project category inference");
  printResult(c, elliInferMemoryCategory("i like electronics") == ELLI_MEM_PREFERENCE, "memory v3: preference category inference");
}

void runVerificationTests(TestCounters& c){
  KiraVerificationReport stable = kiraVerifyAnswer(
    "who invented bluetooth",
    "Bluetooth was developed by engineers working at Ericsson.",
    86,
    2
  );

  printResult(c, stable.level == KIRA_VERIFY_CROSSCHECKED, "verify: two evidence families -> crosschecked");
  printResult(c, stable.cacheable, "verify: stable crosschecked answer may enter cache");

  KiraVerificationReport current = kiraVerifyAnswer(
    "what is the latest wifi standard in 2026",
    "Example current-information answer.",
    90,
    2
  );

  printResult(c, current.level == KIRA_VERIFY_CURRENT_LIMITED, "verify: current question is freshness-sensitive");
  printResult(c, !current.cacheable, "verify: freshness-sensitive answer is not cached as verified truth");

  KiraVerificationReport aiOnly = kiraVerifyAnswer(
    "explain a stable concept",
    "A useful explanatory answer with no retrieved evidence.",
    90,
    0
  );
  printResult(c, aiOnly.level == KIRA_VERIFY_AI_ONLY, "verify: no evidence -> AI-only classification");
  printResult(c, aiOnly.adjustedConfidence <= 65, "verify: AI-only confidence is capped");
}

void runToolEngineTests(TestCounters& c){
  printResult(c, detectUtteranceIntent("save a note that test display tomorrow") == UTT_PROFILE,
    "tools v1.7: note command stays local in legacy intent layer");
  printResult(c, detectUtteranceIntent("add finish physics homework to tasks") == UTT_TASK,
    "tools v1.7: natural task grammar stays local in legacy intent layer");

  printResult(c, kiraToolRegistryHealthy(), "tools v1.7: registry has unique valid entries");
  printResult(c, kiraToolRegistryCount() >= 20, "tools v1.7: broad registry loaded");

  printResult(c, kiraToolClassifyCommand("save a note that test display tomorrow") == "notes.create",
    "tools v1.7: notes create classification");
  printResult(c, kiraToolClassifyCommand("search notes for display") == "notes.search",
    "tools v1.7: notes search classification");
  printResult(c, kiraToolClassifyCommand("add finish physics homework to tasks") == "tasks.create",
    "tools v1.7: natural task grammar classification");
  printResult(c, kiraToolClassifyCommand("add test kira tomorrow at 7 pm") == "tasks.create",
    "tools v1.7: scheduled natural add-command classification");
  printResult(c, kiraToolClassifyCommand("mark task 2 done") == "tasks.complete",
    "tools v1.7: task completion grammar classification");
  printResult(c, kiraToolClassifyCommand("what is 18 percent of 2500") == "calculator.solve",
    "tools v1.7: percentage routes to local calculator");
  printResult(c, kiraToolClassifyCommand("5 gb in mb") == "unit.convert",
    "tools v1.7: data-size conversion routes locally");
  printResult(c, kiraToolClassifyCommand("translate good morning to hindi") == "translation.translate",
    "tools v1.7: translation tool classification");
  printResult(c, kiraToolClassifyCommand("synonyms of intelligent") == "dictionary.lookup",
    "tools v1.7: dictionary tool classification");
  printResult(c, kiraToolClassifyCommand("show last 5 routes") == "history.read",
    "tools v1.7: history tool classification");

  printResult(c, kiraToolPermissionFor("notes.clear") == KIRA_TOOL_DESTRUCTIVE,
    "tools v1.7: destructive permission is explicit");
  printResult(c, kiraToolPermissionFor("device.control") == KIRA_TOOL_DEVICE_ACTION,
    "tools v1.7: device permission is isolated");
  printResult(c, kiraToolRecognizesChain("calculate 18 percent of 2500 and save the answer as a note"),
    "tools v1.7: safe two-tool chain recognized");
  printResult(c, KiraV1::aiWebEnvelopeParserSelfTest(),
    "tools v1.7: multiline AI answer envelope is preserved");

  printResult(c, kiraNotesReady(), "notes v1: persistent NVS storage ready");
  printResult(c, kiraNotesCount() >= 0 && kiraNotesCount() <= KIRA_MAX_NOTES,
    "notes v1: persistent note count valid");
  printResult(c, kiraHistoryReady(), "history v1: session history initialized");
  printResult(c, kiraHistoryCapacity() == 10, "history v1: ten-turn capacity configured");
}


void runV18Tests(TestCounters& c){
  printResult(c, kiraNetworkHasPrimaryConfigured(), "v1.8 network: primary Wi-Fi configured");
  printResult(c, kiraNetworkDualConfigValid(), "v1.8 network: dual-Wi-Fi configuration is non-conflicting");
  KiraConnectivityMode mode=kiraNetworkMode();
  printResult(c, mode==KIRA_NET_OFFLINE || mode==KIRA_NET_ONLINE_DEGRADED || mode==KIRA_NET_ONLINE_FULL, "v1.8 network: connectivity state machine returns valid mode");

  printResult(c, kiraToolRegistryCount() >= 32, "v1.8 tools: expanded registry loaded");
  printResult(c, kiraToolClassifyCommand("network mode") == "network.status", "v1.8 tools: network status classification");
  printResult(c, kiraToolClassifyCommand("offline mode on") == "network.control", "v1.8 tools: offline control classification");
  printResult(c, kiraToolClassifyCommand("timetable monday") == "school.timetable", "v1.8 tools: timetable classification");
  printResult(c, kiraToolClassifyCommand("fill my bag for wednesday") == "school.bag", "v1.8 tools: bag assistant classification");
  printResult(c, kiraToolClassifyCommand("bag reminder status") == "school.reminder", "v1.8 tools: bag reminder classification");
  printResult(c, kiraToolClassifyCommand("knowledge status") == "offline.knowledge", "v1.8 tools: offline knowledge classification");
  printResult(c, kiraToolClassifyCommand("show pending queries") == "offline.pending", "v1.8 tools: pending queue classification");
  printResult(c, kiraToolClassifyCommand("teach me electricity") == "study.teach", "v1.8 tools: teaching bridge classification");
  printResult(c, kiraToolClassifyCommand("quiz me on electricity") == "study.quiz", "v1.8 tools: quiz bridge classification");

  printResult(c, kiraSchoolTimetableSelfTest(), "v1.8 school: timetable transcription/mappings valid");
  printResult(c, kiraSchoolUniqueBagCountForDay(0) == 8, "v1.8 school: Monday bag has 8 unique subjects");
  printResult(c, kiraSchoolUniqueBagCountForDay(5) == 4, "v1.8 school: Saturday bag excludes VES/off period");
  printResult(c, kiraSchoolSubjectFullName("WS") == "Water Security", "v1.8 school: WS = Water Security");
  printResult(c, kiraSchoolSubjectFullName("DS") == "Defence Studies", "v1.8 school: DS = Defence Studies");
  printResult(c, kiraSchoolSubjectFullName("HPE") == "Health and Physical Education", "v1.8 school: HPE mapping valid");
  printResult(c, kiraSchoolSubjectFullName("Gem") == "Geometry", "v1.8 school: Gem = Geometry");
  printResult(c, kiraSchoolSubjectFullName("VES") == "Off period", "v1.8 school: VES is an off period");

  printResult(c, kiraOfflineSelfTest(), "v1.8 offline: built-in knowledge and quiz pack valid");
  printResult(c, kiraOfflineFreshnessSensitive("what is the latest wifi standard in 2026"), "v1.8 offline: live/fresh question detected");
  printResult(c, !kiraOfflineFreshnessSensitive("what is ohms law"), "v1.8 offline: stable fact is not freshness-sensitive");
  String title,answer; int score=0;
  printResult(c, kiraOfflineFindLesson("explain neuromorphic computers",title,answer,score) && answer.length()>40, "v1.8 offline: fuzzy knowledge lookup works");
  KiraOfflineQuizQuestion oq;
  printResult(c, kiraOfflineGetQuiz("electricity",0,oq) && oq.found && oq.correct.length()==1, "v1.8 offline: electricity quiz fallback available");

  printResult(c, kiraToolPermissionFor("network.control") == KIRA_TOOL_NETWORK, "v1.8 permissions: network control isolated");
  printResult(c, kiraToolPermissionFor("school.bag") == KIRA_TOOL_SAFE_WRITE, "v1.8 permissions: bag session is safe-write");
  printResult(c, kiraToolPermissionFor("offline.pending") == KIRA_TOOL_SAFE_WRITE, "v1.8 permissions: pending queue is safe-write");
}

void runResourceTests(TestCounters& c){
  uint32_t heap = ESP.getFreeHeap();
  uint32_t psram = ESP.getFreePsram();

  printResult(c, heap > 50000UL, "system: free heap above 50 KB");
  printResult(c, psram > 1024UL * 1024UL, "system: free PSRAM above 1 MB");

  Serial.print("[INFO] Free heap: "); Serial.println(heap);
  Serial.print("[INFO] Free PSRAM: "); Serial.println(psram);
}

void runOnlineChecks(TestCounters& c){
  if(kiraNetworkInternetAvailable()){
    printResult(c, true, "network: usable internet available");
    Serial.print("[INFO] SSID: "); Serial.println(kiraNetworkCurrentSsid());
    Serial.print("[INFO] RSSI: "); Serial.print(kiraNetworkCurrentRssi()); Serial.println(" dBm");
    Serial.print("[INFO] Speed bars: "); Serial.print(kiraNetworkSpeedBars()); Serial.println("/4");
  }
  else if(WiFi.status() == WL_CONNECTED){
    printWarning(c, "network: Wi-Fi linked but internet unavailable (local brain can still run)");
  }
  else{
    printWarning(c, "network: Wi-Fi not connected (local brain can still run)");
  }

  if(KiraV1::aiWebConfigured()){
    printResult(c, true, "AI: at least one configured provider path available");
  }else{
    printWarning(c, "AI: provider configuration unavailable");
  }
}

void printSummary(const TestCounters& c){
  Serial.println("========================================");
  Serial.print("Passed   : "); Serial.println(c.passed);
  Serial.print("Failed   : "); Serial.println(c.failed);
  Serial.print("Warnings : "); Serial.println(c.warnings);
  Serial.println("========================================");
}

} // namespace

void kiraSelfTestBegin(){
  Serial.println("[SELFTEST] Non-destructive regression diagnostics ready.");
}

bool kiraHandleSelfTestCommand(String q){
  q = normalizeInput(q);

  bool all = q == "test all" || q == "self test" || q == "run self test" || q == "test brain";
  bool parserOnly = q == "test parsers" || q == "test parser" || q == "test routing";
  bool memoryOnly = q == "test memory" || q == "memory self test";
  bool systemOnly = q == "test system" || q == "test resources";
  bool onlineOnly = q == "test online" || q == "test wifi and ai";
  bool verificationOnly = q == "test verification" || q == "test verify";
  bool toolsOnly = q == "test tools" || q == "tool self test" || q == "test productivity";
  bool v18Only = q == "test v1.8" || q == "test v18" || q == "test offline" || q == "test school" || q == "test resilience";

  if(!all && !parserOnly && !memoryOnly && !systemOnly && !onlineOnly && !verificationOnly && !toolsOnly && !v18Only) return false;

  TestCounters c;
  Serial.println();
  Serial.println("========== KIRA SELF-TEST ==========");

  if(all || parserOnly) runParserTests(c);
  if(all || memoryOnly) runMemoryTests(c);
  if(all || systemOnly) runResourceTests(c);
  if(all || onlineOnly) runOnlineChecks(c);
  if(all || verificationOnly) runVerificationTests(c);
  if(all || toolsOnly) runToolEngineTests(c);
  if(all || v18Only) runV18Tests(c);

  printSummary(c);

  if(c.failed == 0){
    String reply = "Self-test complete: " + String(c.passed) + " passed";
    if(c.warnings) reply += ", " + String(c.warnings) + " warnings";
    reply += ", and no regression failures.";
    elliSay(reply);
  }else{
    elliSay("Self-test found " + String(c.failed) + " failure(s). Check the [FAIL] lines in Serial Monitor before changing more code.");
  }

  return true;
}
