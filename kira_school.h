#pragma once
#include <Arduino.h>

void kiraSchoolBegin();
void kiraSchoolTick();
bool kiraSchoolHandleCommand(String q);
bool kiraSchoolLooksLikeCommand(const String& q);
String kiraSchoolStatus();

bool kiraSchoolBagActive();
uint8_t kiraSchoolUniqueBagCountForDay(uint8_t dayIndexMon0);
String kiraSchoolSubjectFullName(const String& code);
bool kiraSchoolTimetableSelfTest();
