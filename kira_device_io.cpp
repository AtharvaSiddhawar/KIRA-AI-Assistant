#include <Arduino.h>
#include "kira_device_io.h"


namespace {


// ============================================================
// KIRA PHYSICAL RELAY — LOW-VOLTAGE BENCH MODE
// ============================================================
//
// Relay module:
//   SRD-05VDC-SL-C single-channel module
//
// Control:
//   IN -> GPIO14
//   active LOW
//
// IMPORTANT:
//   This file only controls the LOW-VOLTAGE relay input.
//   Relay COM / NO / NC must remain disconnected during bench testing.
// ============================================================

constexpr uint8_t MAIN_LIGHT_RELAY_PIN =
  14;


// Active-LOW relay logic.
constexpr uint8_t RELAY_ON =
  LOW;

constexpr uint8_t RELAY_OFF =
  HIGH;


// Physical relay control is intentionally limited to the
// main-light logical state. Fan / charger / second-light remain
// logical-only until separate hardware channels exist.
constexpr bool PHYSICAL_RELAY_BENCH_ENABLED =
  true;


bool deviceIOReady =
  false;


bool lastMainLightOn =
  false;

bool lastMainLightValid =
  false;


// ------------------------------------------------------------
// Apply one physical relay state.
// ------------------------------------------------------------

void writeMainRelay(
  bool on
) {

  if(
    !PHYSICAL_RELAY_BENCH_ENABLED
  ) {

    digitalWrite(
      MAIN_LIGHT_RELAY_PIN,
      RELAY_OFF
    );

    return;
  }


  digitalWrite(
    MAIN_LIGHT_RELAY_PIN,
    on
      ? RELAY_ON
      : RELAY_OFF
  );
}


} // namespace



void kiraDeviceIOBegin() {

  // ----------------------------------------------------------
  // SAFE STARTUP
  // ----------------------------------------------------------
  //
  // Preload HIGH before turning GPIO14 into an output.
  // For this active-LOW module that means relay OFF.
  //
  // This minimizes an unwanted LOW pulse during normal firmware
  // initialization.
  // ----------------------------------------------------------

  digitalWrite(
    MAIN_LIGHT_RELAY_PIN,
    RELAY_OFF
  );


  pinMode(
    MAIN_LIGHT_RELAY_PIN,
    OUTPUT
  );


  digitalWrite(
    MAIN_LIGHT_RELAY_PIN,
    RELAY_OFF
  );


  deviceIOReady =
    true;


  lastMainLightOn =
    false;

  lastMainLightValid =
    true;


  Serial.println();
  Serial.println(
    "[DEVICE IO RELAY V1] GPIO14 PHYSICAL BENCH RELAY READY"
  );

  Serial.println(
    "[DEVICE IO RELAY V1] active LOW | startup state = OFF"
  );

  Serial.println(
    "[DEVICE IO RELAY V1] only main-light state drives the physical relay"
  );
}



void kiraDeviceIOApply(
  bool mainLightOn,
  bool fanOn,
  bool chargerOn,
  bool secondLightOn
) {

  // Future channels remain logical-only.
  (void)fanOn;
  (void)chargerOn;
  (void)secondLightOn;


  if(
    !deviceIOReady
  ) {

    kiraDeviceIOBegin();
  }


  // ----------------------------------------------------------
  // ANTI-CHATTER / STATE CACHE
  // ----------------------------------------------------------
  //
  // kiraDeviceIOApply() is called after every KIRA command.
  // Do not rewrite GPIO14 unless the logical light state actually
  // changed.
  // ----------------------------------------------------------

  if(
    lastMainLightValid &&
    mainLightOn ==
      lastMainLightOn
  ) {

    return;
  }


  writeMainRelay(
    mainLightOn
  );


  lastMainLightOn =
    mainLightOn;

  lastMainLightValid =
    true;


  Serial.print(
    "[DEVICE IO RELAY V1] main light -> "
  );

  Serial.print(
    mainLightOn
      ? "ON"
      : "OFF"
  );

  Serial.print(
    " | GPIO14="
  );

  Serial.println(
    mainLightOn
      ? "LOW"
      : "HIGH"
  );
}
