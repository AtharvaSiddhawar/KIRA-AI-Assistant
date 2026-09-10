#include <Arduino.h>
#include <stdio.h>
#include <esp_heap_caps.h>
#include "kira_history.h"

String normalizeInput(String s);
void elliSay(const String& s);

namespace {

static const uint8_t HISTORY_CAPACITY=10;

struct UserHistoryEntry {
  char raw[128];
  char normalized[128];
};

struct ElliHistoryEntry {
  char text[280];
};

struct RouteHistoryEntry {
  char query[128];
  char route[48];
};

struct ToolHistoryEntry {
  char tool[48];
  char action[48];
  uint32_t latencyMs;
  uint8_t success;
};

UserHistoryEntry* userEntries=nullptr;
ElliHistoryEntry* elliEntries=nullptr;
RouteHistoryEntry* routeEntries=nullptr;
ToolHistoryEntry* toolEntries=nullptr;

uint8_t userHead=0,userCount=0;
uint8_t elliHead=0,elliCount=0;
uint8_t routeHead=0,routeCount=0;
uint8_t toolHead=0,toolCount=0;
bool ready=false;
bool usingPsram=false;

static void copyText(char* destination,size_t size,const String& value){
  if(!destination || size==0) return;
  snprintf(destination,size,"%s",value.c_str());
}

static int ringIndex(uint8_t head,uint8_t count,uint8_t back){
  if(count==0 || back>=count) return -1;
  int idx=(int)head-1-(int)back;
  while(idx<0) idx+=HISTORY_CAPACITY;
  return idx%HISTORY_CAPACITY;
}

static void printHeader(const char* title){
  Serial.println();
  Serial.print("========== ");
  Serial.print(title);
  Serial.println(" ==========");
}

} // namespace

void kiraHistoryBegin(){
  userEntries=(UserHistoryEntry*)heap_caps_malloc(sizeof(UserHistoryEntry)*HISTORY_CAPACITY,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  elliEntries=(ElliHistoryEntry*)heap_caps_malloc(sizeof(ElliHistoryEntry)*HISTORY_CAPACITY,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  routeEntries=(RouteHistoryEntry*)heap_caps_malloc(sizeof(RouteHistoryEntry)*HISTORY_CAPACITY,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  toolEntries=(ToolHistoryEntry*)heap_caps_malloc(sizeof(ToolHistoryEntry)*HISTORY_CAPACITY,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);

  usingPsram=userEntries && elliEntries && routeEntries && toolEntries;

  if(!usingPsram){
    if(userEntries) free(userEntries);
    if(elliEntries) free(elliEntries);
    if(routeEntries) free(routeEntries);
    if(toolEntries) free(toolEntries);

    userEntries=(UserHistoryEntry*)malloc(sizeof(UserHistoryEntry)*HISTORY_CAPACITY);
    elliEntries=(ElliHistoryEntry*)malloc(sizeof(ElliHistoryEntry)*HISTORY_CAPACITY);
    routeEntries=(RouteHistoryEntry*)malloc(sizeof(RouteHistoryEntry)*HISTORY_CAPACITY);
    toolEntries=(ToolHistoryEntry*)malloc(sizeof(ToolHistoryEntry)*HISTORY_CAPACITY);

    if(!userEntries || !elliEntries || !routeEntries || !toolEntries){
      if(userEntries) free(userEntries);
      if(elliEntries) free(elliEntries);
      if(routeEntries) free(routeEntries);
      if(toolEntries) free(toolEntries);
      userEntries=nullptr; elliEntries=nullptr; routeEntries=nullptr; toolEntries=nullptr;
      ready=false;
      Serial.println("[HISTORY] Could not allocate session history buffers.");
      return;
    }
  }

  memset(userEntries,0,sizeof(UserHistoryEntry)*HISTORY_CAPACITY);
  memset(elliEntries,0,sizeof(ElliHistoryEntry)*HISTORY_CAPACITY);
  memset(routeEntries,0,sizeof(RouteHistoryEntry)*HISTORY_CAPACITY);
  memset(toolEntries,0,sizeof(ToolHistoryEntry)*HISTORY_CAPACITY);
  userHead=elliHead=routeHead=toolHead=0;
  userCount=elliCount=routeCount=toolCount=0;
  ready=true;

  Serial.print("[HISTORY] 10-turn history ready in ");
  Serial.println(usingPsram ? "PSRAM." : "internal RAM fallback.");
}

bool kiraHistoryReady(){ return ready; }
uint8_t kiraHistoryCapacity(){ return HISTORY_CAPACITY; }

void kiraHistoryObserveUser(const String& raw,const String& normalized){
  if(!ready) return;
  copyText(userEntries[userHead].raw,sizeof(userEntries[userHead].raw),raw);
  copyText(userEntries[userHead].normalized,sizeof(userEntries[userHead].normalized),normalized);
  userHead=(userHead+1)%HISTORY_CAPACITY;
  if(userCount<HISTORY_CAPACITY) userCount++;
}

void kiraHistoryObserveElli(const String& response){
  if(!ready || !response.length()) return;
  copyText(elliEntries[elliHead].text,sizeof(elliEntries[elliHead].text),response);
  elliHead=(elliHead+1)%HISTORY_CAPACITY;
  if(elliCount<HISTORY_CAPACITY) elliCount++;
}

void kiraHistoryObserveRoute(const String& normalized,const String& route){
  if(!ready) return;
  copyText(routeEntries[routeHead].query,sizeof(routeEntries[routeHead].query),normalized);
  copyText(routeEntries[routeHead].route,sizeof(routeEntries[routeHead].route),route);
  routeHead=(routeHead+1)%HISTORY_CAPACITY;
  if(routeCount<HISTORY_CAPACITY) routeCount++;
}

void kiraHistoryObserveTool(const String& tool,const String& action,bool success,uint32_t latencyMs){
  if(!ready) return;
  copyText(toolEntries[toolHead].tool,sizeof(toolEntries[toolHead].tool),tool);
  copyText(toolEntries[toolHead].action,sizeof(toolEntries[toolHead].action),action);
  toolEntries[toolHead].success=success ? 1 : 0;
  toolEntries[toolHead].latencyMs=latencyMs;
  toolHead=(toolHead+1)%HISTORY_CAPACITY;
  if(toolCount<HISTORY_CAPACITY) toolCount++;
}

String kiraHistoryLastUserQuery(){
  int idx=ringIndex(userHead,userCount,0);
  return idx<0 ? String("") : String(userEntries[idx].raw);
}

String kiraHistoryLastElliReply(){
  int idx=ringIndex(elliHead,elliCount,0);
  return idx<0 ? String("") : String(elliEntries[idx].text);
}

String kiraHistoryStatus(){
  return String("History: ")+String(userCount)+" user, "+String(elliCount)+" Elli, "+
    String(routeCount)+" routes, "+String(toolCount)+" tools in "+
    (usingPsram ? "PSRAM." : "RAM.");
}

void kiraHistoryPrintQueries(uint8_t limit){
  printHeader("QUERY HISTORY");
  if(!userCount) Serial.println("No user queries recorded yet.");
  uint8_t n=(limit<userCount)?limit:userCount;
  for(uint8_t back=0;back<n;back++){
    int idx=ringIndex(userHead,userCount,n-1-back);
    Serial.print(back+1); Serial.print(". "); Serial.println(userEntries[idx].raw);
  }
  Serial.println("===================================");
}

void kiraHistoryPrintRoutes(uint8_t limit){
  printHeader("ROUTE HISTORY");
  if(!routeCount) Serial.println("No routes recorded yet.");
  uint8_t n=(limit<routeCount)?limit:routeCount;
  for(uint8_t back=0;back<n;back++){
    int idx=ringIndex(routeHead,routeCount,n-1-back);
    Serial.print(back+1); Serial.print(". ");
    Serial.print(routeEntries[idx].route); Serial.print(" <- ");
    Serial.println(routeEntries[idx].query);
  }
  Serial.println("===================================");
}

void kiraHistoryPrintTools(uint8_t limit){
  printHeader("TOOL HISTORY");
  if(!toolCount) Serial.println("No tool calls recorded yet.");
  uint8_t n=(limit<toolCount)?limit:toolCount;
  for(uint8_t back=0;back<n;back++){
    int idx=ringIndex(toolHead,toolCount,n-1-back);
    Serial.print(back+1); Serial.print(". ");
    Serial.print(toolEntries[idx].tool); Serial.print(".");
    Serial.print(toolEntries[idx].action); Serial.print(" | ");
    Serial.print(toolEntries[idx].success ? "OK" : "FAIL"); Serial.print(" | ");
    Serial.print(toolEntries[idx].latencyMs); Serial.println(" ms");
  }
  Serial.println("===================================");
}

bool kiraHistoryHandleCommand(String q){
  q=normalizeInput(q);

  if(q=="history status" || q=="conversation history status"){
    elliSay(kiraHistoryStatus());
    return true;
  }

  if(q=="what did i ask before" || q=="what was my last question" || q=="repeat my last question"){
    String previous=kiraHistoryLastUserQuery();
    if(previous.length()) elliSay("Your last question was: "+previous);
    else elliSay("I don't have an earlier question in this session yet.");
    return true;
  }

  if(q=="repeat your last answer" || q=="what was your last answer"){
    String previous=kiraHistoryLastElliReply();
    if(previous.length()) elliSay(previous);
    else elliSay("I don't have an earlier answer in this session yet.");
    return true;
  }

  if(q=="show last 5 queries" || q=="show query history" || q=="query history"){
    kiraHistoryPrintQueries(5);
    elliSay("I printed the recent query history in Serial Monitor.");
    return true;
  }

  if(q=="show last 5 routes" || q=="show route history" || q=="route history"){
    kiraHistoryPrintRoutes(5);
    elliSay("I printed the recent route history in Serial Monitor.");
    return true;
  }

  if(q=="show last 5 tools" || q=="show tool history" || q=="tool history"){
    kiraHistoryPrintTools(5);
    elliSay("I printed the recent tool history in Serial Monitor.");
    return true;
  }

  if(q=="show conversation history" || q=="conversation history"){
    kiraHistoryPrintQueries(10);
    elliSay("I printed the recent in-session conversation queries in Serial Monitor.");
    return true;
  }

  return false;
}
