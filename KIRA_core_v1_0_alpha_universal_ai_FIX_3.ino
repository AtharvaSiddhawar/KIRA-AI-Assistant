#include "kira_brain.h"
#include "elli_visual.h"

#include "kira_device_io.h"
#include "kira_voice.h"

// ============================================================
// KIRA NEXT FOUNDATION
// ============================================================

#include "kira_next_config.h"
#include "kira_events.h"
#include "kira_runtime.h"
#include "kira_metrics.h"
#include "kira_audio_engine.h"
#include "kira_state_bus.h"
#include "kira_input_router.h"
#include "kira_gateway.h"
#include "kira_session.h"
#include "kira_interrupt.h"
#include "kira_recovery.h"
#include "kira_diagnostics.h"


void setup() {

  // ==========================================================
  // PHYSICAL OUTPUT SAFETY FIRST
  // ==========================================================
  //
  // Relay is ACTIVE LOW.
  //
  // Initialize it before the long KIRA startup sequence so it
  // cannot float while Serial / Wi-Fi / memory are starting.
  // ==========================================================

  kiraDeviceIOBegin();


  // ==========================================================
  // EXISTING UNIVERSAL BRAIN
  // ==========================================================
  //
  // kiraBrainSetup() starts Serial and all existing stable KIRA
  // intelligence/storage/network layers. Keep this order for now;
  // later KIRA Next network/session work will move connectivity
  // lifecycle ownership without rewriting the brain.
  // ==========================================================

  kiraBrainSetup();


#if KIRA_NEXT_ENABLED
  // ==========================================================
  // KIRA NEXT CENTRAL INFRASTRUCTURE
  // ==========================================================
  //
  // Foundation 1 introduces the event bus, runtime state machine
  // and metrics without yet stealing audio/network ownership from
  // the proven modules. Subsequent KIRA Next files will emit their
  // events into this authority.
  // ==========================================================

  kiraMetricsBegin();

  if(
    !kiraRuntimeBegin()
  ) {
    Serial.println(
      "[KIRA NEXT] Runtime foundation failed to start"
    );
  }
#endif


  // Elli display / independent animation task.

  elliVisualBegin();


#if KIRA_NEXT_ENABLED
  kiraStateBusBegin();
  kiraInputRouterBegin();
  kiraGatewayBegin();
  kiraSessionBegin();
  kiraInterruptBegin();
  kiraRecoveryBegin();
  kiraDiagnosticsBegin();
#endif


  // Offline wake word + commands + audio.
  //
  // Failure is non-fatal:
  // KIRA continues through Serial/display/network.

  bool voiceReady =
    kiraVoiceBegin();


#if KIRA_NEXT_ENABLED
  // The existing system is now initialized. Do not mark voice failure as
  // a fatal runtime error because Serial/display/local brain must remain
  // usable exactly as before.

  kiraRuntimePost(
    KIRA_EVENT_BOOT_COMPLETE,
    KIRA_EVENT_SOURCE_SYSTEM,
    voiceReady ? 1 : 0
  );

  kiraRuntimeService();

  Serial.print(
    "[KIRA NEXT] Foundation ready | voice="
  );

  Serial.println(
    voiceReady ? "READY" : "UNAVAILABLE"
  );
#endif
}


void loop() {

#if KIRA_NEXT_ENABLED
  // Group 2: keep network/session/transport lifecycle moving without
  // replacing KIRA's existing direct provider logic.
  kiraGatewayService();
  kiraSessionService();
  kiraInterruptService();
  kiraRecoveryService();
  kiraDiagnosticsService();

  // Service pending typed events before the legacy brain loop.
  kiraRuntimeService();
  kiraStateBusService();
#endif


  kiraBrainLoop();

  elliVisualUpdate();


#if KIRA_NEXT_ENABLED
  // Process any events produced while the current KIRA brain iteration ran.
  kiraRuntimeService();
  kiraStateBusService();
  kiraMetricsService();
  kiraRecoveryService();
#endif
}
