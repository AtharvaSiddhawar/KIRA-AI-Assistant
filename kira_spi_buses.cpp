#include "kira_spi_buses.h"

#include "kira_hardware_pins.h"


namespace {


// ESP32-S3 Arduino core:
//   global SPI = FSPI -> SPI2
//
// Keep the global SPI object exclusively for TFT.
SPIClass& tftBus =
  SPI;


// Separate physical hardware controller for SD.
SPIClass sdBus(
  HSPI
);


bool busesReady =
  false;

bool tftReady =
  false;

bool sdReady =
  false;


} // namespace


bool kiraSpiBusesBegin() {

  if(
    busesReady
  ) {
    return true;
  }


  // ----------------------------------------------------------
  // TFT — dedicated FSPI/SPI2
  // ----------------------------------------------------------

  tftBus.begin(
    KiraPins::TFT_SCK,
    KiraPins::TFT_MISO,
    KiraPins::TFT_MOSI,
    KiraPins::TFT_CS
  );

  tftReady =
    true;


  // ----------------------------------------------------------
  // SD — dedicated HSPI/SPI3
  // ----------------------------------------------------------

  sdBus.begin(
    KiraPins::SD_SCK,
    KiraPins::SD_MISO,
    KiraPins::SD_MOSI,
    KiraPins::SD_CS
  );

  sdReady =
    true;


  busesReady =
    tftReady &&
    sdReady;


  Serial.println();
  Serial.println(
    "[KIRA SPI] SEPARATE BUS BACKBONE READY"
  );

  Serial.print(
    "[KIRA SPI] TFT  FSPI/SPI2 | SCK="
  );

  Serial.print(
    KiraPins::TFT_SCK
  );

  Serial.print(
    " MOSI="
  );

  Serial.print(
    KiraPins::TFT_MOSI
  );

  Serial.print(
    " MISO="
  );

  Serial.print(
    KiraPins::TFT_MISO
  );

  Serial.print(
    " CS="
  );

  Serial.println(
    KiraPins::TFT_CS
  );


  Serial.print(
    "[KIRA SPI] SD   HSPI/SPI3 | SCK="
  );

  Serial.print(
    KiraPins::SD_SCK
  );

  Serial.print(
    " MOSI="
  );

  Serial.print(
    KiraPins::SD_MOSI
  );

  Serial.print(
    " MISO="
  );

  Serial.print(
    KiraPins::SD_MISO
  );

  Serial.print(
    " CS="
  );

  Serial.println(
    KiraPins::SD_CS
  );


  Serial.print(
    "[KIRA SPI] TOUCH software bus RESERVED | CLK="
  );

  Serial.print(
    KiraPins::TOUCH_CLK
  );

  Serial.print(
    " DIN="
  );

  Serial.print(
    KiraPins::TOUCH_DIN
  );

  Serial.print(
    " DO="
  );

  Serial.print(
    KiraPins::TOUCH_DO
  );

  Serial.print(
    " CS="
  );

  Serial.print(
    KiraPins::TOUCH_CS
  );

  Serial.print(
    " IRQ="
  );

  Serial.println(
    KiraPins::TOUCH_IRQ
  );


  Serial.println(
    "[KIRA SPI] TFT LED/BL = hardwired 3.3V (no GPIO)"
  );


  return
    busesReady;
}


SPIClass& kiraTftSPI() {

  return
    tftBus;
}


SPIClass& kiraSdSPI() {

  return
    sdBus;
}


bool kiraTftSPIReady() {

  return
    tftReady;
}


bool kiraSdSPIReady() {

  return
    sdReady;
}
