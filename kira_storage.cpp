#include <Arduino.h>
#include <time.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include "kira_storage.h"
#include "kira_hardware_pins.h"
#include "kira_spi_buses.h"

// normalizeInput still lives in kira_brain.ino during migration.
String normalizeInput(String s);

static Preferences brainPrefs;
static bool brainPrefsReady=false;

BrainStore brainStore;
bool brainSdReady=false;

static String recentUserUtterances[8];
static uint8_t recentUserHead=0;
static uint8_t recentUserCount=0;

// SD now owns its own hardware SPI controller (HSPI/SPI3).
// Permanent wiring:
//   SCK  -> GPIO4
//   MOSI -> GPIO5
//   MISO -> GPIO6
//   CS   -> GPIO21


void copyBrainText(char* destination,size_t size,const String& value){
  if(!destination || size==0) return;
  snprintf(destination,size,"%s",value.c_str());
}


uint32_t currentBrainDay(){
  struct tm t;
  if(!getLocalTime(&t,100)) return 0;
  return (uint32_t)(t.tm_year+1900)*400UL+(uint32_t)t.tm_yday;
}


void saveBrainStore(){
  if(!brainPrefsReady) return;
  brainPrefs.putBytes("store",&brainStore,sizeof(brainStore));
}


static void initializeFreshBrainStore(){
  memset(&brainStore,0,sizeof(brainStore));
  brainStore.magic=BRAIN_STORE_MAGIC;
  brainStore.version=BRAIN_STORE_VERSION;
  brainStore.autoRemember=1;
  brainStore.respectfulMode=1;

  // Keep the current owner default during migration. It can be changed
  // locally with "my name is ..." at any time.
  copyBrainText(brainStore.ownerName,sizeof(brainStore.ownerName),"Atharva");
  saveBrainStore();
}


void beginBrainStorage(){
  brainPrefsReady=brainPrefs.begin("kira_brain",false);

  if(!brainPrefsReady){
    Serial.println("[BRAIN/NVS] Could not open personal brain storage.");
    return;
  }

  size_t len=brainPrefs.getBytesLength("store");

  if(len==sizeof(brainStore)){
    brainPrefs.getBytes("store",&brainStore,sizeof(brainStore));
  }

  if(
    brainStore.magic!=BRAIN_STORE_MAGIC ||
    brainStore.version!=BRAIN_STORE_VERSION
  ){
    initializeFreshBrainStore();
    Serial.println("[BRAIN/NVS] New personal brain store created.");
  }else{
    Serial.println("[BRAIN/NVS] Personal cache/tasks/routines loaded.");
  }
}


static void ensureBrainDirectories(){
  if(!SD.exists("/elli_brain")) SD.mkdir("/elli_brain");
  if(!SD.exists("/elli_brain/history")) SD.mkdir("/elli_brain/history");
  if(!SD.exists("/elli_brain/vocabulary")) SD.mkdir("/elli_brain/vocabulary");
}


bool beginBrainSD(){

  if(
    !kiraSpiBusesBegin()
  ){
    brainSdReady=false;

    Serial.println(
      "[BRAIN/SD] Dedicated SPI backbone failed. NVS cache only."
    );

    return false;
  }


  if(
    !SD.begin(
      KiraPins::SD_CS,
      kiraSdSPI(),
      10000000
    )
  ){
    brainSdReady=false;

    Serial.println(
      "[BRAIN/SD] SD not mounted on dedicated HSPI/SPI3. Searchable memory will use NVS cache only."
    );

    return false;
  }


  brainSdReady=true;

  ensureBrainDirectories();

  Serial.println(
    "[BRAIN/SD] Elli long-term memory card mounted on dedicated HSPI/SPI3."
  );

  return true;
}


void appendBrainSD(const String& path,const String& line){
  if(!brainSdReady) return;
  File f=SD.open(path.c_str(),FILE_APPEND);
  if(!f) return;
  f.println(line);
  f.close();
}


String brainTimestamp(){
  struct tm t;
  if(!getLocalTime(&t,100)) return String(millis());
  char b[40];
  strftime(b,sizeof(b),"%Y-%m-%d %H:%M:%S",&t);
  return String(b);
}


static void rememberRecentUtterance(const String& text){
  recentUserUtterances[recentUserHead]=text;
  recentUserHead=(recentUserHead+1)%8;
  if(recentUserCount<8) recentUserCount++;
}


bool safeToPersistConversationLine(String raw){
  String q=normalizeInput(raw);

  const char* blocked[]={
    "password","passcode","pin number","otp","one time password",
    "api key","secret key","credit card","debit card","cvv",
    "bank account","home address","house address","private address",
    "medical","diagnosis","therapy","therapist","mental health",
    "i am depressed","i feel depressed","i am anxious","i feel anxious"
  };

  for(size_t i=0;i<sizeof(blocked)/sizeof(blocked[0]);i++){
    if(q.indexOf(blocked[i])>=0) return false;
  }
  return true;
}


void logUserUtterance(const String& raw,const String& normalized){
  rememberRecentUtterance(normalized);

  if(brainSdReady && safeToPersistConversationLine(raw)){
    appendBrainSD(
      "/elli_brain/history/conversation.log",
      brainTimestamp()+" | USER | "+raw
    );
  }
}


void printRecentConversation(){
  Serial.println();
  Serial.println("========== RECENT USER CONTEXT ==========");

  if(recentUserCount==0){
    Serial.println("No recent utterances in RAM.");
  }else{
    int start=(recentUserHead+8-recentUserCount)%8;
    for(uint8_t i=0;i<recentUserCount;i++){
      int idx=(start+i)%8;
      Serial.print(i+1);
      Serial.print(". ");
      Serial.println(recentUserUtterances[idx]);
    }
  }

  Serial.println("=========================================");
}
