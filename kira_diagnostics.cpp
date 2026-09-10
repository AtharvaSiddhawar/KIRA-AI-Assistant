#include "kira_diagnostics.h"

#include "kira_events.h"
#include "kira_gateway.h"
#include "kira_input_router.h"
#include "kira_metrics.h"
#include "kira_network_v2.h"
#include "kira_wifi_max.h"
#include "kira_next_config.h"
#include "kira_recovery.h"
#include "kira_runtime.h"
#include "kira_session.h"
#include "kira_tool_engine.h"
#include "kira_transport.h"

namespace {

bool ready=false;

}


bool kiraDiagnosticsBegin(){
#if !KIRA_DIAGNOSTICS_ENABLED
  return true;
#else
  ready=true;

  Serial.println(
    "[DIAGNOSTICS] READY | Group 3 health console"
  );

  return true;
#endif
}


void kiraDiagnosticsService(){
#if KIRA_DIAGNOSTICS_ENABLED
  // Intentionally quiet. Recovery owns exception logging.
#endif
}


String kiraDiagnosticsOneLine(){
  KiraHealthSnapshot h=
    kiraMetricsSnapshot();

  String s=
    "health=";

  s+=
    kiraHealthLevelName(
      h.level
    );

  s+=
    " state=";

  s+=
    kiraRuntimeStateName();

  s+=
    " heap=";

  s+=
    String(
      h.heapFree
    );

  s+=
    " psram=";

  s+=
    String(
      h.psramFree
    );

  s+=
    " drops=";

  s+=
    String(
      h.eventDropped
    );

  s+=
    " wifi=";

  s+=
    String(
      kiraNetworkHealthScore()
    );

  s+=
    "/100";

  s+=
    " tools=";

  s+=
    String(
      kiraToolRegistryCount()
    );

  return s;
}


void kiraDiagnosticsPrintSummary(){
#if KIRA_DIAGNOSTICS_ENABLED
  Serial.print(
    "[DIAGNOSTICS] "
  );

  Serial.println(
    kiraDiagnosticsOneLine()
  );
#endif
}


void kiraDiagnosticsPrintFull(){
#if KIRA_DIAGNOSTICS_ENABLED
  if(!ready){
    kiraDiagnosticsBegin();
  }


  KiraHealthSnapshot h=
    kiraMetricsSnapshot();


  Serial.println();
  Serial.println(
    "================ KIRA NEXT DIAGNOSTICS ================"
  );

  Serial.print("Health                : ");
  Serial.println(kiraHealthLevelName(h.level));

  Serial.print("Runtime state         : ");
  Serial.println(kiraRuntimeStateName());

  Serial.print("State age             : ");
  Serial.print(h.stateAgeMs);
  Serial.println(" ms");

  Serial.print("Illegal transitions   : ");
  Serial.println(h.illegalTransitions);


  Serial.println("---------------- MEMORY ----------------");

  Serial.print("Heap free             : ");
  Serial.println(h.heapFree);

  Serial.print("Heap largest block    : ");
  Serial.println(h.heapLargest);

  Serial.print("PSRAM free            : ");
  Serial.println(h.psramFree);

  Serial.print("PSRAM largest block   : ");
  Serial.println(h.psramLargest);


  Serial.println("---------------- EVENTS ----------------");

  Serial.print("Posted                : ");
  Serial.println(h.eventPosted);

  Serial.print("Handled               : ");
  Serial.println(h.eventHandled);

  Serial.print("Dropped               : ");
  Serial.println(h.eventDropped);


  Serial.println("---------------- ROUTING ----------------");

  Serial.print("Router route          : ");
  Serial.println(kiraInputRouterRouteName());

  Serial.print("Router turn id        : ");
  Serial.println(kiraInputRouterTurnId());


  Serial.println("---------------- NETWORK / SESSION ----------------");

  Serial.print("Network connected     : ");
  Serial.println(kiraNetworkConnected() ? "YES" : "NO");

  Serial.print("Transport             : ");
  Serial.println(kiraTransportModeName());

  Serial.print("Gateway               : ");
  Serial.println(kiraGatewayModeName());

  Serial.print("Session               : ");
  Serial.println(kiraSessionStateName());

  Serial.print("Session recoveries    : ");
  Serial.println(kiraSessionRecoveryCount());

  kiraWifiMaxPrintDiagnostics();


  Serial.println("---------------- TOOLS ----------------");

  Serial.print("Registry count        : ");
  Serial.println(kiraToolRegistryCount());

  Serial.print("Registry healthy      : ");
  Serial.println(kiraToolRegistryHealthy() ? "YES" : "NO");

  Serial.print("Tool executions       : ");
  Serial.println(kiraToolExecutionCount());

  Serial.print("Tool rejections       : ");
  Serial.println(kiraToolRejectedCount());

  Serial.print("Last tool             : ");
  Serial.println(kiraToolLastTool());

  Serial.print("Last action           : ");
  Serial.println(kiraToolLastAction());


  Serial.println("---------------- RECOVERY ----------------");

  Serial.print("Attempts              : ");
  Serial.println(kiraRecoveryAttemptCount());

  Serial.print("Successes             : ");
  Serial.println(kiraRecoverySuccessCount());

  Serial.print("Last reason           : ");
  Serial.println(
    kiraRecoveryReasonName(
      kiraRecoveryLastReason()
    )
  );

  Serial.print("Last detail           : ");
  Serial.println(kiraRecoveryLastDetail());


  Serial.println("---------------- COUNTERS ----------------");

  kiraMetricsPrintCounters();

  Serial.println(
    "======================================================="
  );

  Serial.println();
#endif
}
