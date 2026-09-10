#pragma once

#include <Arduino.h>

#include "kira_events.h"

enum KiraInputRoute : uint8_t {
  KIRA_INPUT_ROUTE_NONE=0,
  KIRA_INPUT_ROUTE_OFFLINE,
  KIRA_INPUT_ROUTE_STT,
  KIRA_INPUT_ROUTE_UNIVERSAL
};

bool kiraInputRouterBegin();

void kiraInputRouterObserveEvent(
  const KiraEvent& event
);

KiraInputRoute kiraInputRouterRoute();
const char* kiraInputRouterRouteName();

uint32_t kiraInputRouterTurnId();
