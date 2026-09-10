#pragma once
#include <Arduino.h>

// =====================================================
//               KIRA CALENDAR ENGINE
// =====================================================
//
// Handles:
//
//   what is special on 5 june
//   what is celebrated on 28 february
//   observances on 15 august in india
//
//   when is environment day
//   when is national science day
//   when is teachers day in india
//
//   what happened on 15 august 1947
//
//   what day is 29 august 2027
//
// It separates:
//
// WEEKDAY
// OBSERVANCE
// HISTORICAL EVENT
// EVENT -> DATE
//
// =====================================================

bool handleCalendarBrain(const String& input);