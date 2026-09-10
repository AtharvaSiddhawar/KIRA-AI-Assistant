#pragma once
#include <Arduino.h>

void kiraHistoryBegin();
bool kiraHistoryReady();
uint8_t kiraHistoryCapacity();

void kiraHistoryObserveUser(const String& raw,const String& normalized);
void kiraHistoryObserveElli(const String& response);
void kiraHistoryObserveRoute(const String& normalized,const String& route);
void kiraHistoryObserveTool(const String& tool,const String& action,bool success,uint32_t latencyMs);

String kiraHistoryLastUserQuery();
String kiraHistoryLastElliReply();
String kiraHistoryStatus();

bool kiraHistoryHandleCommand(String q);
void kiraHistoryPrintQueries(uint8_t limit=5);
void kiraHistoryPrintRoutes(uint8_t limit=5);
void kiraHistoryPrintTools(uint8_t limit=5);
