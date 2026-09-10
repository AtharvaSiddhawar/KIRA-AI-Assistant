#pragma once
#include <Arduino.h>

void kiraDevToolsBegin();
bool kiraDevHandleCommand(String q);
void kiraDevObserveInput(const String& raw,const String& normalized);
void kiraDevObserveRoute(
  const String& route,
  uint32_t elapsedMs,
  uint32_t heapBefore,
  uint32_t heapAfter,
  uint32_t psramBefore,
  uint32_t psramAfter
);
bool kiraDevDebugEnabled();
String kiraDevLastRoute();
String kiraDevLastQuery();
