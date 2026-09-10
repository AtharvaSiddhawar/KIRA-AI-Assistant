#pragma once
#include <Arduino.h>

struct KiraCacheRecord {
  bool found=false;
  bool stale=false;
  String question;
  String answer;
  String source;
  uint32_t storedEpoch=0;
  uint32_t ttlSeconds=0;
  int confidence=0;
};

void kiraKnowledgeCacheBegin();
bool kiraCacheLookup(const String& question,KiraCacheRecord& record,bool allowStale=true);
void kiraCacheStore(const String& question,const String& answer,const String& source,int confidence,uint32_t ttlSeconds);
uint32_t kiraDefaultCacheTtl(const String& question);
String kiraCacheAgeText(uint32_t storedEpoch);
