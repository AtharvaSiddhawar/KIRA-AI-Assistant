#pragma once

#include <Arduino.h>


enum KiraToolPermission : uint8_t {
  KIRA_TOOL_READ_ONLY=0,
  KIRA_TOOL_SAFE_WRITE,
  KIRA_TOOL_NETWORK,
  KIRA_TOOL_DEVICE_ACTION,
  KIRA_TOOL_DESTRUCTIVE
};


struct KiraToolSpec {
  const char* name;
  const char* description;
  KiraToolPermission permission;
  bool engineOwned;
};


// ============================================================
// KIRA NEXT GROUP 3 — STRUCTURED TOOL CALLS
// ============================================================

enum KiraToolCallStatus : uint8_t {
  KIRA_TOOL_CALL_UNPLANNED=0,
  KIRA_TOOL_CALL_READY,
  KIRA_TOOL_CALL_EXECUTING,
  KIRA_TOOL_CALL_OK,
  KIRA_TOOL_CALL_FAILED,
  KIRA_TOOL_CALL_REJECTED,
  KIRA_TOOL_CALL_LEGACY_BRIDGE
};


struct KiraToolCall {
  uint32_t id;

  String tool;
  String action;
  String input;

  KiraToolPermission permission;

  bool engineOwned;
  bool requiresNetwork;
  bool requiresConfirmation;
  bool confirmationPresent;

  KiraToolCallStatus status;
};


struct KiraToolResult {
  uint32_t callId;

  String tool;
  String action;

  KiraToolCallStatus status;

  bool handled;
  bool success;

  uint32_t latencyMs;

  String detail;
};


void kiraToolEngineBegin();
bool kiraToolHandleCommand(String q);


// Structured API used by future planner/gateway layers.
bool kiraToolPlanCommand(
  const String& q,
  KiraToolCall& outCall
);

bool kiraToolAuthorizeCall(
  const KiraToolCall& call,
  String& reason
);

bool kiraToolExecuteCall(
  const KiraToolCall& call,
  KiraToolResult& outResult
);

void kiraToolPrintCall(
  const KiraToolCall& call
);

void kiraToolPrintResult(
  const KiraToolResult& result
);

const char* kiraToolCallStatusName(
  KiraToolCallStatus status
);


// Existing public API preserved.
String kiraToolLastTool();
String kiraToolLastAction();
String kiraToolStatus();

uint8_t kiraToolRegistryCount();
bool kiraToolRegistryHealthy();

const char* kiraToolPermissionName(
  KiraToolPermission permission
);

KiraToolPermission kiraToolPermissionFor(
  const String& toolName
);

String kiraToolClassifyCommand(
  const String& q
);

bool kiraToolRecognizesChain(
  const String& q
);

void kiraToolPrintRegistry();
void kiraToolPrintLast();

uint32_t kiraToolExecutionCount();
uint32_t kiraToolRejectedCount();
