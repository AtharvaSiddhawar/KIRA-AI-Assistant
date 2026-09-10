#pragma once
#include <Arduino.h>

struct KiraOfflineQuizQuestion {
  bool found=false;
  String topic;
  String question;
  String a,b,c,d;
  String correct;
  String why;
};

void kiraOfflineBegin();
void kiraOfflineTick();
bool kiraOfflineHandleCommand(String q);
bool kiraOfflineHandleKnowledge(String q);

bool kiraOfflineFindLesson(const String& query,String& title,String& answer,int& score);
bool kiraOfflineGetQuiz(const String& topic,uint16_t sequence,KiraOfflineQuizQuestion& out);
void kiraOfflineStoreVerified(const String& question,const String& answer,int confidence);

String kiraOfflineStatus();
uint8_t kiraOfflineLearnedCount();
uint8_t kiraOfflinePendingCount();
bool kiraOfflineFreshnessSensitive(const String& q);
bool kiraOfflineSelfTest();
