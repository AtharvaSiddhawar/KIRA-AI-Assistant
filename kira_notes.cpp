#include <Arduino.h>
#include <stdio.h>
#include <Preferences.h>
#include <time.h>
#include "kira_notes.h"

String normalizeInput(String s);
void elliSay(const String& s);

namespace {

static const uint32_t NOTES_MAGIC = 0x4B4E5431UL; // KNT1
static const uint16_t NOTES_VERSION = 1;
static const uint32_t CLEAR_CONFIRM_TTL_MS = 30000UL;

struct KiraNoteRecord {
  uint8_t active;
  uint8_t important;
  uint16_t id;
  uint32_t createdDay;
  uint64_t createdEpoch;
  char text[160];
};

struct KiraNoteStore {
  uint32_t magic;
  uint16_t version;
  uint16_t nextId;
  KiraNoteRecord notes[KIRA_MAX_NOTES];
};

Preferences notePrefs;
KiraNoteStore noteStore;
bool notePrefsReady=false;
bool pendingClear=false;
uint32_t pendingClearSince=0;

static void copyText(char* destination,size_t size,const String& value){
  if(!destination || size==0) return;
  snprintf(destination,size,"%s",value.c_str());
}

static uint32_t currentDay(){
  struct tm t;
  if(!getLocalTime(&t,100)) return 0;
  return (uint32_t)(t.tm_year+1900)*400UL+(uint32_t)t.tm_yday;
}

static uint64_t currentEpoch(){
  time_t now=time(nullptr);
  if(now<100000) return 0;
  return (uint64_t)now;
}

static void saveStore(){
  if(!notePrefsReady) return;
  notePrefs.putBytes("store",&noteStore,sizeof(noteStore));
}

static void initializeStore(){
  memset(&noteStore,0,sizeof(noteStore));
  noteStore.magic=NOTES_MAGIC;
  noteStore.version=NOTES_VERSION;
  noteStore.nextId=1;
  saveStore();
}

static bool sensitiveCredentialText(String text){
  text=normalizeInput(text);
  const char* blocked[]={
    "password","passcode","pin number"," otp","otp ","api key",
    "secret key","private key","cvv","credit card","debit card"
  };
  for(size_t i=0;i<sizeof(blocked)/sizeof(blocked[0]);i++){
    if(text.indexOf(blocked[i])>=0) return true;
  }
  return false;
}

static int freeSlot(){
  for(uint8_t i=0;i<KIRA_MAX_NOTES;i++){
    if(!noteStore.notes[i].active) return i;
  }
  return -1;
}

static int slotForVisibleNumber(int number){
  if(number<1) return -1;
  int visible=0;
  for(uint8_t i=0;i<KIRA_MAX_NOTES;i++){
    if(!noteStore.notes[i].active) continue;
    visible++;
    if(visible==number) return i;
  }
  return -1;
}

static bool parseTrailingNumber(String q,int& number){
  q=normalizeInput(q);
  int end=q.length()-1;
  while(end>=0 && q[end]==' ') end--;
  int start=end;
  while(start>=0 && isdigit((unsigned char)q[start])) start--;
  if(start==end) return false;
  String digits=q.substring(start+1,end+1);
  if(!digits.length()) return false;
  number=digits.toInt();
  return number>0;
}

static String stripNoteCreatePrefix(String q,bool& important){
  q=normalizeInput(q);
  important=false;

  const char* importantPrefixes[]={
    "save important note that ","save an important note that ",
    "add important note ","important note "
  };
  for(size_t i=0;i<sizeof(importantPrefixes)/sizeof(importantPrefixes[0]);i++){
    String p=importantPrefixes[i];
    if(q.startsWith(p)){
      important=true;
      q.remove(0,p.length());
      q.trim();
      return q;
    }
  }

  const char* prefixes[]={
    "save a note that ","save note that ","add a note that ",
    "add note that ","save a note ","save note ",
    "add a note ","add note ","note that ","note "
  };
  for(size_t i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);i++){
    String p=prefixes[i];
    if(q.startsWith(p)){
      q.remove(0,p.length());
      q.trim();
      return q;
    }
  }

  return "";
}

static void printNotes(const String& filter=""){
  Serial.println();
  Serial.println("========== KIRA NOTES ==========");
  int visible=0;
  int matched=0;
  String f=normalizeInput(filter);

  for(uint8_t i=0;i<KIRA_MAX_NOTES;i++){
    const KiraNoteRecord& note=noteStore.notes[i];
    if(!note.active) continue;
    visible++;

    String text=note.text;
    if(f.length() && normalizeInput(text).indexOf(f)<0) continue;
    matched++;

    Serial.print(visible);
    Serial.print(". ");
    Serial.print(text);
    if(note.important) Serial.print(" [important]");
    Serial.print("  {id=");
    Serial.print(note.id);
    Serial.println("}");
  }

  if(!matched){
    if(f.length()) Serial.println("No notes matched that search.");
    else Serial.println("No saved notes.");
  }
  Serial.println("===============================");
}

static bool deleteVisible(int number,String& deleted){
  int slot=slotForVisibleNumber(number);
  if(slot<0) return false;
  deleted=noteStore.notes[slot].text;
  memset(&noteStore.notes[slot],0,sizeof(KiraNoteRecord));
  saveStore();
  return true;
}

static void clearAll(){
  for(uint8_t i=0;i<KIRA_MAX_NOTES;i++){
    memset(&noteStore.notes[i],0,sizeof(KiraNoteRecord));
  }
  saveStore();
}

} // namespace

void kiraNotesBegin(){
  notePrefsReady=notePrefs.begin("kira_notes",false);
  if(!notePrefsReady){
    Serial.println("[NOTES] NVS namespace could not be opened. Notes disabled.");
    return;
  }

  size_t len=notePrefs.getBytesLength("store");
  if(len==sizeof(noteStore)){
    notePrefs.getBytes("store",&noteStore,sizeof(noteStore));
  }

  if(noteStore.magic!=NOTES_MAGIC || noteStore.version!=NOTES_VERSION){
    initializeStore();
    Serial.println("[NOTES] Fresh persistent note store created.");
  }else{
    Serial.print("[NOTES] Persistent notes loaded: ");
    Serial.println(kiraNotesCount());
  }
}

bool kiraNotesReady(){ return notePrefsReady; }

int kiraNotesCount(){
  int count=0;
  for(uint8_t i=0;i<KIRA_MAX_NOTES;i++) if(noteStore.notes[i].active) count++;
  return count;
}

String kiraNotesStatus(){
  return String("Notes: ")+String(kiraNotesCount())+"/"+String(KIRA_MAX_NOTES)+
    (notePrefsReady ? " persistent NVS slots ready." : "; storage unavailable.");
}

bool kiraNotesCreate(const String& rawText,bool important,uint16_t& noteId,String& error){
  String text=rawText;
  text.trim();
  noteId=0;
  error="";

  if(!notePrefsReady){ error="Persistent note storage is unavailable."; return false; }
  if(text.length()<1){ error="The note is empty."; return false; }
  if(sensitiveCredentialText(text)){
    error="I won't store passwords, PINs, API keys, card security codes, or similar credentials in notes.";
    return false;
  }

  int slot=freeSlot();
  if(slot<0){ error="My local note list is full. Delete an old note first."; return false; }

  if(text.length()>159) text=text.substring(0,159);

  KiraNoteRecord& note=noteStore.notes[slot];
  memset(&note,0,sizeof(note));
  note.active=1;
  note.important=important ? 1 : 0;
  note.id=noteStore.nextId++;
  if(noteStore.nextId==0) noteStore.nextId=1;
  note.createdDay=currentDay();
  note.createdEpoch=currentEpoch();
  copyText(note.text,sizeof(note.text),text);
  saveStore();
  noteId=note.id;
  return true;
}

bool kiraNotesHandleCommand(String q){
  q=normalizeInput(q);

  if(pendingClear && millis()-pendingClearSince>CLEAR_CONFIRM_TTL_MS){
    pendingClear=false;
  }

  if(q=="save a note" || q=="save note" || q=="add a note" || q=="add note" || q=="note"){
    elliSay("Tell me what you want the note to say.");
    return true;
  }

  if(q=="show my notes" || q=="show notes" || q=="list notes" || q=="notes"){
    printNotes();
    elliSay(kiraNotesCount() ? "I printed your saved notes in Serial Monitor." : "You don't have any saved notes yet.");
    return true;
  }

  if(q=="notes status" || q=="note status"){
    elliSay(kiraNotesStatus());
    return true;
  }

  if(q.startsWith("search notes for ") || q.startsWith("find notes for ") || q.startsWith("find note about ")){
    String term;
    if(q.startsWith("search notes for ")) term=q.substring(17);
    else if(q.startsWith("find notes for ")) term=q.substring(15);
    else term=q.substring(16);
    term.trim();
    if(!term.length()) elliSay("Tell me what to search for in your notes.");
    else{
      printNotes(term);
      elliSay("I printed the matching notes in Serial Monitor.");
    }
    return true;
  }

  if(q.startsWith("show note ")){
    int number=0;
    if(!parseTrailingNumber(q,number)){
      elliSay("Tell me the note number, for example 'show note 2'.");
      return true;
    }
    int slot=slotForVisibleNumber(number);
    if(slot<0){ elliSay("I couldn't find that note number."); return true; }
    elliSay(String(noteStore.notes[slot].text));
    return true;
  }

  if(q.startsWith("delete note ") || q.startsWith("remove note ")){
    int number=0;
    if(!parseTrailingNumber(q,number)){
      elliSay("Tell me the note number, for example 'delete note 2'.");
      return true;
    }
    String deleted;
    if(deleteVisible(number,deleted)) elliSay("Deleted note "+String(number)+".");
    else elliSay("I couldn't find that note number.");
    return true;
  }

  if(q=="clear notes" || q=="clear all notes" || q=="delete all notes"){
    pendingClear=true;
    pendingClearSince=millis();
    elliSay("That would delete every saved note. Say 'confirm clear notes' within 30 seconds if you really want that.");
    return true;
  }

  if(q=="confirm clear notes"){
    if(!pendingClear || millis()-pendingClearSince>CLEAR_CONFIRM_TTL_MS){
      pendingClear=false;
      elliSay("There is no active clear-notes confirmation. Say 'clear notes' first.");
      return true;
    }
    pendingClear=false;
    clearAll();
    elliSay("All saved notes have been cleared.");
    return true;
  }

  bool important=false;
  String text=stripNoteCreatePrefix(q,important);
  if(text.length()){
    uint16_t id=0;
    String error;
    if(kiraNotesCreate(text,important,id,error)){
      elliSay(String(important ? "Important note saved" : "Note saved")+". Note id "+String(id)+".");
    }else{
      elliSay(error);
    }
    return true;
  }

  return false;
}
