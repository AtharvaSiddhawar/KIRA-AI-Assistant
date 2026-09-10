#include <Arduino.h>
#include <time.h>
#include <FS.h>
#include <SD.h>
#include "kira_web_cache.h"
#include "kira_storage.h"

static const uint8_t RAM_CACHE_SIZE=6;
static KiraCacheRecord ramCache[RAM_CACHE_SIZE];
static uint8_t ramHead=0;

static String norm(String s){s.toLowerCase();String o="";bool sp=false;for(size_t i=0;i<s.length();i++){char c=s[i];if(isalnum((unsigned char)c)){o+=c;sp=false;}else if(o.length()&&!sp){o+=' ';sp=true;}}o.trim();while(o.indexOf("  ")>=0)o.replace("  "," ");return o;}
static uint32_t fnv1a(const String& s){uint32_t h=2166136261UL;for(size_t i=0;i<s.length();i++){h^=(uint8_t)s[i];h*=16777619UL;}return h;}
static String hex8(uint32_t v){char b[12];snprintf(b,sizeof(b),"%08lX",(unsigned long)v);return String(b);}
static String pathFor(const String& q){return "/elli_brain/knowledge/"+hex8(fnv1a(norm(q)))+".kira";}
static String esc(String s){s.replace("\\","\\\\");s.replace("\n","\\n");s.replace("\r","");s.replace("\t","\\t");return s;}
static String unesc(String s){String o="";bool slash=false;for(size_t i=0;i<s.length();i++){char c=s[i];if(slash){if(c=='n')o+='\n';else if(c=='t')o+='\t';else o+=c;slash=false;}else if(c=='\\')slash=true;else o+=c;}if(slash)o+='\\';return o;}
static uint32_t nowEpoch(){time_t n=time(nullptr);return n>1600000000?(uint32_t)n:0;}
static String field(const String& body,const String& key){String p=key+"\t";int at=body.indexOf(p);if(at<0)return "";int start=at+p.length();int end=body.indexOf('\n',start);if(end<0)end=body.length();return unesc(body.substring(start,end));}

void kiraKnowledgeCacheBegin(){
  if(brainSdReady){if(!SD.exists("/elli_brain"))SD.mkdir("/elli_brain");if(!SD.exists("/elli_brain/knowledge"))SD.mkdir("/elli_brain/knowledge");Serial.println("[CACHE] SD knowledge cache ready.");}
  else Serial.println("[CACHE] SD unavailable; temporary RAM cache only.");
}

uint32_t kiraDefaultCacheTtl(const String& question){
  String q=norm(question);
  if(q.indexOf("weather")>=0||q.indexOf("temperature")>=0||q.indexOf("forecast")>=0) return 20UL*60UL;
  if(q.indexOf("today")>=0||q.indexOf("latest")>=0||q.indexOf("current")>=0||q.indexOf("news")>=0) return 6UL*60UL*60UL;
  if(q.indexOf("most ")>=0||q.indexOf("highest")>=0||q.indexOf("lowest")>=0||q.indexOf("best ")>=0||q.indexOf("ranking")>=0) return 7UL*24UL*60UL*60UL;
  return 90UL*24UL*60UL*60UL;
}

void kiraCacheStore(const String& question,const String& answer,const String& source,int confidence,uint32_t ttl){
  if(!question.length()||!answer.length())return;
  KiraCacheRecord r;r.found=true;r.question=norm(question);r.answer=answer;r.source=source;r.storedEpoch=nowEpoch();r.ttlSeconds=ttl;r.confidence=confidence;
  ramCache[ramHead]=r;ramHead=(ramHead+1)%RAM_CACHE_SIZE;
  if(!brainSdReady)return;
  String path=pathFor(question);File f=SD.open(path.c_str(),FILE_WRITE);if(!f)return;
  f.println("KIRA_CACHE_V1");f.println("question\t"+esc(r.question));f.println("answer\t"+esc(r.answer));f.println("source\t"+esc(r.source));f.println("stored\t"+String(r.storedEpoch));f.println("ttl\t"+String(r.ttlSeconds));f.println("confidence\t"+String(r.confidence));f.close();
}

bool kiraCacheLookup(const String& question,KiraCacheRecord& record,bool allowStale){
  record=KiraCacheRecord();String nq=norm(question);uint32_t now=nowEpoch();
  for(uint8_t i=0;i<RAM_CACHE_SIZE;i++){
    if(ramCache[i].found&&ramCache[i].question==nq){record=ramCache[i];record.stale=record.storedEpoch&&now&&record.ttlSeconds&&(now-record.storedEpoch>record.ttlSeconds);if(!record.stale||allowStale)return true;}
  }
  if(!brainSdReady)return false;
  File f=SD.open(pathFor(question).c_str(),FILE_READ);if(!f)return false;String body=f.readString();f.close();if(!body.startsWith("KIRA_CACHE_V1"))return false;
  record.found=true;record.question=field(body,"question");record.answer=field(body,"answer");record.source=field(body,"source");record.storedEpoch=(uint32_t)field(body,"stored").toInt();record.ttlSeconds=(uint32_t)field(body,"ttl").toInt();record.confidence=field(body,"confidence").toInt();
  if(record.question!=nq){record=KiraCacheRecord();return false;}
  record.stale=record.storedEpoch&&now&&record.ttlSeconds&&(now-record.storedEpoch>record.ttlSeconds);
  if(record.stale&&!allowStale){record=KiraCacheRecord();return false;}
  ramCache[ramHead]=record;ramHead=(ramHead+1)%RAM_CACHE_SIZE;return true;
}

String kiraCacheAgeText(uint32_t storedEpoch){
  if(!storedEpoch)return "an earlier session";uint32_t now=nowEpoch();if(!now||now<storedEpoch)return "an earlier session";uint32_t d=now-storedEpoch;
  if(d<60)return String(d)+" seconds ago";if(d<3600)return String(d/60)+" minutes ago";if(d<86400)return String(d/3600)+" hours ago";return String(d/86400)+" days ago";
}
