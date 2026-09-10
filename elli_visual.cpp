#include "elli_visual.h"

#if KIRA_ELLI_DISPLAY_ENABLED


#include <SPI.h>
#include <WiFi.h>
#include <time.h>

#include <esp_heap_caps.h>
#include <pgmspace.h>

#include "elli_assets.h"


namespace {


// ============================================================
//                 PHASE 4X EXHIBITION BUILD
// ============================================================
//
// 4C
// - Independent FreeRTOS animation task
// - TFT animation continues while AI/web is blocking
//
// 4D
// - Full approved Elli animation engine preserved
// - Automatic visual reaction architecture
//
// 5
// - Live 32px status strip
// - Real internet-speed bars
// - Network/internet state
// - Clock
// - AI/SPEAKING activity
//
// IMPORTANT:
// No additional full-screen framebuffer is used.
//
// Existing:
//   sourceBuffer = 288 x 240 RGB565
//   renderBuffer = 288 x 240 RGB565
//
// Both are allocated in PSRAM.
//
// ============================================================


// ============================================================
// TFT HARDWARE
// ============================================================

constexpr int TFT_CS =
  10;

constexpr int TFT_DC =
  9;

constexpr int TFT_RST =
  8;


constexpr int SCREEN_W =
  320;

constexpr int SCREEN_H =
  240;


constexpr int ELLI_W =
  288;

constexpr int ELLI_H =
  240;


constexpr int STATUS_X =
  288;

constexpr int STATUS_W =
  32;


// ============================================================
// PROVEN TFT SETTINGS
// ============================================================
//
// 40 MHz stopped the integrated instability.
//
// MADCTL 0x20 is the known-good orientation.
//
// RGB666 0x66 is the known-good transfer format.
//
// DO NOT change these while Phase 4X is being validated.
// ============================================================

constexpr uint32_t SPI_HZ =
  40000000UL;


constexpr uint8_t TFT_MADCTL =
  0x20;


constexpr uint8_t TFT_PIXEL_FORMAT =
  0x66;


// ============================================================
// TRANSMIT BUFFER
// ============================================================

constexpr int TX_PIXELS =
  512;


uint8_t txBuffer[
  TX_PIXELS * 3
];


// ============================================================
// PSRAM FRAMEBUFFERS
// ============================================================

uint16_t* sourceBuffer =
  nullptr;


uint16_t* renderBuffer =
  nullptr;


// ============================================================
// FREERTOS DISPLAY TASK
// ============================================================
//
// 8 KB keeps enough headroom for the display engine without
// wasting large amounts of internal RAM.
//
// Only this task should continuously draw TFT animation after
// startup.
// ============================================================

constexpr uint32_t VISUAL_TASK_STACK =
  8192;


constexpr UBaseType_t VISUAL_TASK_PRIORITY =
  2;


constexpr BaseType_t VISUAL_TASK_CORE =
  1;


TaskHandle_t visualTaskHandle =
  nullptr;


bool visualTaskRunning =
  false;


// ============================================================
// VISUAL ENGINE STATE
// ============================================================

bool displayReady =
  false;


ElliVisualState currentVisualState =
  ELLI_VISUAL_IDLE;


uint32_t stateStartMs =
  0;


uint32_t lastActivityMs =
  0;


// ============================================================
// CROSS-TASK STATE REQUESTS
// ============================================================
//
// KIRA brain requests states.
//
// The visual task receives the request and actually updates
// the TFT.
//
// This prevents the KIRA networking/AI task from directly
// manipulating animation memory while Elli is rendering.
// ============================================================

portMUX_TYPE visualMux =
  portMUX_INITIALIZER_UNLOCKED;


volatile bool pendingStateRequest =
  false;


volatile ElliVisualState pendingState =
  ELLI_VISUAL_IDLE;


volatile bool pendingSpeakRequest =
  false;


volatile ElliVisualState pendingReaction =
  ELLI_VISUAL_IDLE;


volatile bool commandSpeechIssued =
  false;


// Phase 5G:
// A scene such as GOOD NIGHT can ask Elli to finish her
// response and then remain in the approved SLEEPY animation.
volatile bool sleepAfterSpeechRequested =
  false;


// ============================================================
// PHASE 5M - REAL AUDIO-SYNCED SPEECH STATE
// ============================================================
//
// TTS updates these values from the audio playback task.
// The independent visual task consumes them safely.
// ============================================================

volatile bool audioSpeechActive =
  false;

volatile uint8_t audioSpeechLevel =
  0;


// ============================================================
// GENERAL ANIMATION CLOCKS
// ============================================================

uint32_t nextFrameMs =
  0;


uint32_t nextBlinkMs =
  0;


uint32_t nextMouthMs =
  0;


uint8_t motionStep =
  0;


uint8_t blinkStep =
  0;


uint8_t blinkRepeat =
  0;


uint8_t mouthStep =
  0;


bool blinkActive =
  false;


// ============================================================
// AUTOMATIC REACTION → SPEAK → IDLE
// ============================================================
//
// Stage 0 = no automatic sequence
// Stage 1 = emotion reaction
// Stage 2 = speaking
//
// ============================================================

uint8_t autoSpeakStage =
  0;


uint32_t autoSpeakDeadline =
  0;


uint32_t currentSpeakDurationMs =
  1800;


// Emotion is shown briefly before speaking.

constexpr uint32_t REACTION_HOLD_MS =
  700;


// ============================================================
// IDLE SLEEP TIMER
// ============================================================

constexpr uint32_t SLEEPY_AFTER_MS =
  5UL *
  60UL *
  1000UL;


// Phase 5G:
// Small calm/neutral idle variations make Elli feel alive
// without adding another framebuffer or new image assets.
uint32_t nextIdlePersonalityMs =
  0;

uint32_t idlePersonalityUntilMs =
  0;

uint8_t idlePersonalityAsset =
  0;


// ============================================================
// STATUS STRIP STATE
// ============================================================

uint32_t nextStatusCheckMs =
  0;


uint32_t lastStatusSignature =
  0xFFFFFFFFUL;


// ============================================================
// EXISTING ELLI ASSET NUMBERS
// ============================================================
//
// These are the current approved 21 assets.
//
// Do NOT rearrange elli_assets.h without updating these.
//
// ============================================================

constexpr uint8_t ASSET_IDLE_MASTER =
  0;

constexpr uint8_t ASSET_BLINK_OPEN =
  1;

constexpr uint8_t ASSET_BLINK_HALF =
  2;

constexpr uint8_t ASSET_BLINK_CLOSED =
  3;

constexpr uint8_t ASSET_BLINK_OPEN_AGAIN =
  4;

constexpr uint8_t ASSET_THINKING_A =
  5;

constexpr uint8_t ASSET_THINKING_B =
  6;

constexpr uint8_t ASSET_SPEAK_CLOSED =
  7;

constexpr uint8_t ASSET_SPEAK_SMALL =
  8;

constexpr uint8_t ASSET_SPEAK_MEDIUM =
  9;

constexpr uint8_t ASSET_SPEAK_WIDE =
  10;

constexpr uint8_t ASSET_HAPPY =
  11;

constexpr uint8_t ASSET_EXCITED =
  12;

constexpr uint8_t ASSET_SAD =
  13;

constexpr uint8_t ASSET_CONFUSED =
  14;

constexpr uint8_t ASSET_SLEEPY =
  15;

constexpr uint8_t ASSET_ANGRY =
  16;

constexpr uint8_t ASSET_NEUTRAL =
  17;

constexpr uint8_t ASSET_IDLE_CALM =
  18;

constexpr uint8_t ASSET_LISTENING_A =
  19;

constexpr uint8_t ASSET_LISTENING_B =
  20;


// ============================================================
// APPROVED ANGRY EYE REGIONS
// ============================================================
//
// Angry blink closes DOWNWARD from the upper eyelid.
//
// ============================================================

#define ANGRY_LX0 72
#define ANGRY_LX1 99

#define ANGRY_RX0 162
#define ANGRY_RX1 190

#define ANGRY_EYE_Y0 139
#define ANGRY_EYE_Y1 173


// ============================================================
// GENERIC EYE REGIONS
// ============================================================
//
// Used to transfer only blink information.
//
// Head, bow, face and body stay from the emotion/state master.
//
// ============================================================

constexpr int EYE_LEFT_X0 =
  75;

constexpr int EYE_LEFT_Y0 =
  118;

constexpr int EYE_LEFT_X1 =
  112;

constexpr int EYE_LEFT_Y1 =
  164;


constexpr int EYE_RIGHT_X0 =
  178;

constexpr int EYE_RIGHT_Y0 =
  118;

constexpr int EYE_RIGHT_X1 =
  214;

constexpr int EYE_RIGHT_Y1 =
  164;


// ============================================================
// SPEAKING MOUTH REGION
// ============================================================
//
// Only this area changes between speaking frames.
//
// ============================================================

constexpr int MOUTH_X0 =
  118;

constexpr int MOUTH_Y0 =
  166;

constexpr int MOUTH_X1 =
  173;

constexpr int MOUTH_Y1 =
  203;


// ============================================================
// THINKING PAW REGION
// ============================================================
//
// Prevents bow/head/right side of face from moving.
//
// ============================================================

constexpr int THINK_PAW_X0 =
  70;

constexpr int THINK_PAW_Y0 =
  170;

constexpr int THINK_PAW_X1 =
  218;

constexpr int THINK_PAW_Y1 =
  240;


// ============================================================
// CONFUSED QUESTION MARK AREA
// ============================================================
//
// Only pixels DIFFERENT from neutral inside this zone are
// considered candidates.
//
// Pink accent filtering additionally prevents the surrounding
// head/bow from being erased.
//
// ============================================================

constexpr int CONF_Q_X0 =
  210;

constexpr int CONF_Q_Y0 =
  30;

constexpr int CONF_Q_X1 =
  282;

constexpr int CONF_Q_Y1 =
  125;


// ============================================================
// SLEEPY Z REGION
// ============================================================

constexpr int SLEEP_Z_X0 =
  200;

constexpr int SLEEP_Z_Y0 =
  20;

constexpr int SLEEP_Z_X1 =
  282;

constexpr int SLEEP_Z_Y1 =
  135;


// ============================================================
// ANGRY SYMBOL REGION
// ============================================================
//
// Symbol is above / right of the head.
//
// Difference against neutral lets us isolate the symbol while
// leaving the actual angry body completely fixed.
//
// ============================================================

constexpr int ANGRY_SYMBOL_X0 =
  205;

constexpr int ANGRY_SYMBOL_Y0 =
  5;

constexpr int ANGRY_SYMBOL_X1 =
  287;

constexpr int ANGRY_SYMBOL_Y1 =
  112;


// ============================================================
// SIMPLE BOB TABLE
// ============================================================

const int8_t IDLE_BOB[8] = {

  0,
  -1,
  -2,
  -3,
  -2,
  -1,
  0,
  0
};


// ============================================================
// SPI TRANSACTION
// ============================================================

inline void beginTftTransaction() {

  SPI.beginTransaction(
    SPISettings(
      SPI_HZ,
      MSBFIRST,
      SPI_MODE0
    )
  );
}


inline void endTftTransaction() {

  SPI.endTransaction();
}


// ============================================================
// LOCKED TFT COMMAND
// ============================================================
//
// These helpers assume beginTftTransaction() was already called.
//
// Keeping the complete:
//
//   SET COLUMN
//   SET ROW
//   MEMORY WRITE
//   PIXELS
//
// under one SPI transaction prevents another shared SPI user
// from interrupting a TFT frame halfway through.
//
// ============================================================

inline void commandLocked(
  uint8_t command
) {

  digitalWrite(
    TFT_DC,
    LOW
  );


  digitalWrite(
    TFT_CS,
    LOW
  );


  SPI.transfer(
    command
  );


  digitalWrite(
    TFT_CS,
    HIGH
  );
}


inline void data8Locked(
  uint8_t data
) {

  digitalWrite(
    TFT_DC,
    HIGH
  );


  digitalWrite(
    TFT_CS,
    LOW
  );


  SPI.transfer(
    data
  );


  digitalWrite(
    TFT_CS,
    HIGH
  );
}


// ============================================================
// SINGLE COMMAND
// ============================================================

void writeCommand(
  uint8_t command
) {

  beginTftTransaction();


  commandLocked(
    command
  );


  endTftTransaction();
}


// ============================================================
// COMMAND + ONE DATA BYTE
// ============================================================

void writeCommandData8(
  uint8_t command,
  uint8_t data
) {

  beginTftTransaction();


  commandLocked(
    command
  );


  data8Locked(
    data
  );


  endTftTransaction();
}


// ============================================================
// RESET TFT
// ============================================================

void resetTft() {

  digitalWrite(
    TFT_RST,
    HIGH
  );


  delay(
    20
  );


  digitalWrite(
    TFT_RST,
    LOW
  );


  delay(
    120
  );


  digitalWrite(
    TFT_RST,
    HIGH
  );


  delay(
    150
  );
}


// ============================================================
// INITIALIZE TFT
// ============================================================

void initTft() {

  resetTft();


  // Software reset.

  writeCommand(
    0x01
  );


  delay(
    150
  );


  // Sleep out.

  writeCommand(
    0x11
  );


  delay(
    150
  );


  // Proven orientation.

  writeCommandData8(
    0x36,
    TFT_MADCTL
  );


  // RGB666.

  writeCommandData8(
    0x3A,
    TFT_PIXEL_FORMAT
  );


  // Display on.

  writeCommand(
    0x29
  );


  delay(
    100
  );
}


// ============================================================
// TFT WINDOW - TRANSACTION ALREADY LOCKED
// ============================================================

void setWindowLocked(
  uint16_t x0,
  uint16_t y0,
  uint16_t x1,
  uint16_t y1
) {

  // Column address.

  commandLocked(
    0x2A
  );


  digitalWrite(
    TFT_DC,
    HIGH
  );


  digitalWrite(
    TFT_CS,
    LOW
  );


  SPI.transfer(
    x0 >> 8
  );

  SPI.transfer(
    x0 & 0xFF
  );


  SPI.transfer(
    x1 >> 8
  );

  SPI.transfer(
    x1 & 0xFF
  );


  digitalWrite(
    TFT_CS,
    HIGH
  );


  // Row address.

  commandLocked(
    0x2B
  );


  digitalWrite(
    TFT_DC,
    HIGH
  );


  digitalWrite(
    TFT_CS,
    LOW
  );


  SPI.transfer(
    y0 >> 8
  );

  SPI.transfer(
    y0 & 0xFF
  );


  SPI.transfer(
    y1 >> 8
  );

  SPI.transfer(
    y1 & 0xFF
  );


  digitalWrite(
    TFT_CS,
    HIGH
  );


  // Memory write.

  commandLocked(
    0x2C
  );
}


// ============================================================
// RGB565 → RGB666
// ============================================================

inline void rgb565To666(
  uint16_t color,
  uint8_t& r,
  uint8_t& g,
  uint8_t& b
) {

  uint8_t r5 =
    (
      color >>
      11
    ) &
    0x1F;


  uint8_t g6 =
    (
      color >>
      5
    ) &
    0x3F;


  uint8_t b5 =
    color &
    0x1F;


  r =
    (
      (
        r5 <<
        3
      ) |
      (
        r5 >>
        2
      )
    ) &
    0xFC;


  g =
    (
      g6 <<
      2
    ) &
    0xFC;


  b =
    (
      (
        b5 <<
        3
      ) |
      (
        b5 >>
        2
      )
    ) &
    0xFC;
}


// ============================================================
// PUSH RGB565 REGION
// ============================================================
//
// IMPORTANT:
//
// The panel's working orientation requires software horizontal
// reversal.
//
// physicalX:
//
//   SCREEN_W - logicalX - width
//
// and every row is sent RIGHT → LEFT.
//
// DO NOT remove this.
// ============================================================

void pushRGB565Region(
  int logicalX,
  int y,
  int width,
  int height,
  const uint16_t* pixels
) {

  int physicalX =
    SCREEN_W -
    logicalX -
    width;


  beginTftTransaction();


  setWindowLocked(
    physicalX,
    y,
    physicalX +
    width -
    1,
    y +
    height -
    1
  );


  digitalWrite(
    TFT_DC,
    HIGH
  );


  digitalWrite(
    TFT_CS,
    LOW
  );


  for(
    int row = 0;
    row < height;
    row++
  ) {

    int x =
      width -
      1;


    while(
      x >= 0
    ) {

      int chunk =
        min(
          TX_PIXELS,
          x + 1
        );


      for(
        int i = 0;
        i < chunk;
        i++
      ) {

        uint16_t color =
          pixels[
            (
              (uint32_t)row *
              width
            ) +
            (
              x -
              i
            )
          ];


        uint8_t r;
        uint8_t g;
        uint8_t b;


        rgb565To666(
          color,
          r,
          g,
          b
        );


        int p =
          i *
          3;


        txBuffer[
          p
        ] =
          r;


        txBuffer[
          p + 1
        ] =
          g;


        txBuffer[
          p + 2
        ] =
          b;
      }


      SPI.writeBytes(
        txBuffer,
        chunk *
        3
      );


      x -=
        chunk;
    }
  }


  digitalWrite(
    TFT_CS,
    HIGH
  );


  endTftTransaction();
}


// ============================================================
// DECODE RLE ASSET
// ============================================================

bool decodeFrame(
  uint8_t index,
  uint16_t* destination
) {

  if(
    index >=
    ELLI_ASSET_COUNT
  ) {

    return false;
  }


  ElliFrameAsset asset =
    ELLI_ASSETS[
      index
    ];


  uint32_t position =
    0;


  const uint32_t total =
    (
      (uint32_t)ELLI_W *
      ELLI_H
    );


  for(
    uint32_t i = 0;
    i < asset.runCount;
    i++
  ) {

    ElliRleRun run;


    memcpy_P(
      &run,
      &asset.runs[
        i
      ],
      sizeof(
        run
      )
    );


    if(
      position +
      run.count >
      total
    ) {

      return false;
    }


    for(
      uint16_t n = 0;
      n < run.count;
      n++
    ) {

      destination[
        position++
      ] =
        run.color;
    }
  }


  return
    position ==
    total;
}


// ============================================================
// BUFFER RECTANGLE COPY
// ============================================================

void copyRect(
  uint16_t* destination,
  const uint16_t* source,
  int x0,
  int y0,
  int x1,
  int y1
) {

  if(
    x0 < 0
  ) {

    x0 =
      0;
  }


  if(
    y0 < 0
  ) {

    y0 =
      0;
  }


  if(
    x1 >
    ELLI_W
  ) {

    x1 =
      ELLI_W;
  }


  if(
    y1 >
    ELLI_H
  ) {

    y1 =
      ELLI_H;
  }


  int width =
    x1 -
    x0;


  if(
    width <= 0
  ) {

    return;
  }


  for(
    int y = y0;
    y < y1;
    y++
  ) {

    memcpy(
      destination +
      (
        (uint32_t)y *
        ELLI_W
      ) +
      x0,

      source +
      (
        (uint32_t)y *
        ELLI_W
      ) +
      x0,

      width *
      sizeof(
        uint16_t
      )
    );
  }
}


// ============================================================
// BUFFER FILL RECT
// ============================================================

void fillRectBuffer(
  uint16_t* buffer,
  int x,
  int y,
  int width,
  int height,
  uint16_t color
) {

  for(
    int yy = y;
    yy <
    y + height;
    yy++
  ) {

    if(
      yy < 0 ||
      yy >=
      ELLI_H
    ) {

      continue;
    }


    for(
      int xx = x;
      xx <
      x + width;
      xx++
    ) {

      if(
        xx < 0 ||
        xx >=
        ELLI_W
      ) {

        continue;
      }


      buffer[
        (
          (uint32_t)yy *
          ELLI_W
        ) +
        xx
      ] =
        color;
    }
  }
}


// ============================================================
// BUFFER LINE
// ============================================================

void drawLineBuffer(
  uint16_t* buffer,
  int x0,
  int y0,
  int x1,
  int y1,
  uint16_t color,
  int thickness = 2
) {

  int dx =
    abs(
      x1 -
      x0
    );


  int sx =
    x0 <
    x1
    ?
    1
    :
    -1;


  int dy =
    -abs(
      y1 -
      y0
    );


  int sy =
    y0 <
    y1
    ?
    1
    :
    -1;


  int error =
    dx +
    dy;


  while(
    true
  ) {

    fillRectBuffer(
      buffer,

      x0 -
      thickness /
      2,

      y0 -
      thickness /
      2,

      thickness,

      thickness,

      color
    );


    if(
      x0 ==
      x1 &&
      y0 ==
      y1
    ) {

      break;
    }


    int e2 =
      error *
      2;


    if(
      e2 >=
      dy
    ) {

      error +=
        dy;

      x0 +=
        sx;
    }


    if(
      e2 <=
      dx
    ) {

      error +=
        dx;

      y0 +=
        sy;
    }
  }
}


// ============================================================
// SET BUFFER PIXEL
// ============================================================

inline void setBufferPixel(
  uint16_t* buffer,
  int x,
  int y,
  uint16_t color
) {

  if(
    x < 0 ||
    y < 0 ||
    x >=
    ELLI_W ||
    y >=
    ELLI_H
  ) {

    return;
  }


  buffer[
    (
      (uint32_t)y *
      ELLI_W
    ) +
    x
  ] =
    color;
}


// ============================================================
// SHIFT FRAME
// ============================================================

void shiftFrameXY(
  const uint16_t* source,
  uint16_t* destination,
  int dx,
  int dy
) {

  uint16_t background =
    source[
      0
    ];


  const uint32_t total =
    (
      (uint32_t)ELLI_W *
      ELLI_H
    );


  for(
    uint32_t i = 0;
    i < total;
    i++
  ) {

    destination[
      i
    ] =
      background;
  }


  for(
    int y = 0;
    y <
    ELLI_H;
    y++
  ) {

    int newY =
      y +
      dy;


    if(
      newY < 0 ||
      newY >=
      ELLI_H
    ) {

      continue;
    }


    for(
      int x = 0;
      x <
      ELLI_W;
      x++
    ) {

      int newX =
        x +
        dx;


      if(
        newX < 0 ||
        newX >=
        ELLI_W
      ) {

        continue;
      }


      destination[
        (
          (uint32_t)newY *
          ELLI_W
        ) +
        newX
      ] =

        source[
          (
            (uint32_t)y *
            ELLI_W
          ) +
          x
        ];
    }
  }
}


// ============================================================
// GENERIC BLINK ASSET
// ============================================================

uint8_t blinkAssetForStep(
  uint8_t step
) {

  switch(
    step
  ) {

    case 0:

      return
        ASSET_BLINK_HALF;


    case 1:

      return
        ASSET_BLINK_CLOSED;


    default:

      return
        ASSET_BLINK_OPEN_AGAIN;
  }
}


// ============================================================
// APPLY GENERIC BLINK TO CURRENT MASTER FRAME
// ============================================================
//
// Only eye rectangles are transferred.
//
// This prevents:
//
// - bow movement
// - nose movement
// - paw movement
// - emotion/body replacement
//
// ============================================================

void applyGenericBlink(
  uint16_t* master,
  uint8_t step
) {

  uint8_t blinkAsset =
    blinkAssetForStep(
      step
    );


  decodeFrame(
    blinkAsset,
    renderBuffer
  );


  copyRect(
    master,
    renderBuffer,

    EYE_LEFT_X0,
    EYE_LEFT_Y0,

    EYE_LEFT_X1,
    EYE_LEFT_Y1
  );


  copyRect(
    master,
    renderBuffer,

    EYE_RIGHT_X0,
    EYE_RIGHT_Y0,

    EYE_RIGHT_X1,
    EYE_RIGHT_Y1
  );
}


// ============================================================
// APPLY ANGRY TOP-DOWN BLINK
// ============================================================
//
// IMPORTANT:
//
// Angry eyes close from ABOVE downward.
//
// We never wiggle/shift the angry face.
//
// ============================================================

void applyAngryBlink(
  uint16_t* buffer,
  uint8_t phase
) {

  if(
    phase > 2
  ) {

    return;
  }


  // Sample face and eye colors directly from the approved
  // angry asset instead of hardcoding palette values.

  uint16_t faceColor =
    buffer[
      (
        (uint32_t)145 *
        ELLI_W
      ) +
      135
    ];


  uint16_t eyelidColor =
    buffer[
      (
        (uint32_t)155 *
        ELLI_W
      ) +
      84
    ];


  int eyeHeight =
    ANGRY_EYE_Y1 -
    ANGRY_EYE_Y0;


  // Half-close.

  if(
    phase == 0 ||
    phase == 2
  ) {

    int cover =
      (
        eyeHeight *
        48
      ) /
      100;


    int lidY =
      ANGRY_EYE_Y0 +
      cover;


    fillRectBuffer(
      buffer,

      ANGRY_LX0,
      ANGRY_EYE_Y0,

      ANGRY_LX1 -
      ANGRY_LX0,

      cover,

      faceColor
    );


    fillRectBuffer(
      buffer,

      ANGRY_RX0,
      ANGRY_EYE_Y0,

      ANGRY_RX1 -
      ANGRY_RX0,

      cover,

      faceColor
    );


    drawLineBuffer(
      buffer,

      ANGRY_LX0 +
      3,

      lidY,

      ANGRY_LX1 -
      3,

      lidY,

      eyelidColor,

      2
    );


    drawLineBuffer(
      buffer,

      ANGRY_RX0 +
      3,

      lidY,

      ANGRY_RX1 -
      3,

      lidY,

      eyelidColor,

      2
    );


    return;
  }


  // Fully closed.
  //
  // Cover from the top all the way down, then place the
  // eyelid line near the bottom of the original eye.

  fillRectBuffer(
    buffer,

    ANGRY_LX0,
    ANGRY_EYE_Y0,

    ANGRY_LX1 -
    ANGRY_LX0,

    eyeHeight,

    faceColor
  );


  fillRectBuffer(
    buffer,

    ANGRY_RX0,
    ANGRY_EYE_Y0,

    ANGRY_RX1 -
    ANGRY_RX0,

    eyeHeight,

    faceColor
  );


  int closedY =
    ANGRY_EYE_Y1 -
    6;


  drawLineBuffer(
    buffer,

    ANGRY_LX0 +
    3,

    closedY,

    ANGRY_LX1 -
    3,

    closedY,

    eyelidColor,

    3
  );


  drawLineBuffer(
    buffer,

    ANGRY_RX0 +
    3,

    closedY,

    ANGRY_RX1 -
    3,

    closedY,

    eyelidColor,

    3
  );
}


// ============================================================
// RGB565 COLOR HELPERS
// ============================================================

bool isBlueZPixel(
  uint16_t color
) {

  int r =
    (
      color >>
      11
    ) &
    0x1F;


  int g =
    (
      color >>
      5
    ) &
    0x3F;


  int b =
    color &
    0x1F;


  // Deliberately selective.
  //
  // We only manipulate obvious blue Z pixels.
  //
  // Nearby decorative lines that are not blue are untouched.

  return
    b >= 16 &&
    g >= 12 &&
    b >
    r + 6;
}


bool isPinkAccentPixel(
  uint16_t color
) {

  int r =
    (
      color >>
      11
    ) &
    0x1F;


  int g =
    (
      color >>
      5
    ) &
    0x3F;


  int b =
    color &
    0x1F;


  return
    r >= 20 &&
    b >= 10 &&
    r >
    g / 2;
}


// ============================================================
// RESET ANIMATION
// ============================================================

void resetAnimation(
  uint32_t now
) {

  stateStartMs =
    now;


  motionStep =
    0;


  blinkStep =
    0;


  blinkRepeat =
    0;


  mouthStep =
    0;


  blinkActive =
    false;


  nextFrameMs =
    now;


  nextBlinkMs =
    now +
    3200;


  nextMouthMs =
    now +
    300;
}


// ============================================================
// PUSH COMPLETE ELLI FRAME
// ============================================================

void pushElliFrame(
  const uint16_t* frame
) {

  pushRGB565Region(
    0,
    0,
    ELLI_W,
    ELLI_H,
    frame
  );
}


// ============================================================
// IDLE
// ============================================================
//
// Approved motion:
//
// 0,-1,-2,-3,-2,-1,0,0
//
// plus normal blink.
//
// ============================================================

void updateIdle(
  uint32_t now
) {

  int bobY =
    IDLE_BOB[
      motionStep %
      8
    ];


  // Start blink when due.

  if(
    !blinkActive &&
    (
      (int32_t)(
        now -
        nextBlinkMs
      ) >=
      0
    )
  ) {

    blinkActive =
      true;


    blinkStep =
      0;
  }


  if(
    blinkActive
  ) {

    uint8_t blinkAsset =
      blinkAssetForStep(
        blinkStep
      );


    decodeFrame(
      blinkAsset,
      sourceBuffer
    );


    shiftFrameXY(
      sourceBuffer,
      renderBuffer,
      0,
      bobY
    );


    pushElliFrame(
      renderBuffer
    );


    blinkStep++;


    if(
      blinkStep >=
      3
    ) {

      blinkActive =
        false;


      blinkStep =
        0;


      nextBlinkMs =
        now +
        3500;
    }


    nextFrameMs =
      now +
      90;


    // Hold body at same bob position during blink.

    return;
  }


  // ========================================================
  // PHASE 5G IDLE PERSONALITY
  // ========================================================
  //
  // Every ~18-38 seconds of uninterrupted idle time Elli
  // briefly uses the approved calm/neutral master frame.
  // Blinking and bobbing are still preserved.
  // ========================================================

  if(
    nextIdlePersonalityMs ==
    0
  ) {

    nextIdlePersonalityMs =
      now +
      12000UL +
      (uint32_t)random(
        8000
      );
  }


  if(
    (int32_t)(
      now -
      nextIdlePersonalityMs
    ) >=
    0
  ) {

    idlePersonalityAsset =
      random(
        2
      ) ==
      0
      ?
      ASSET_IDLE_CALM
      :
      ASSET_NEUTRAL;


    idlePersonalityUntilMs =
      now +
      1600UL +
      (uint32_t)random(
        1800
      );


    nextIdlePersonalityMs =
      now +
      18000UL +
      (uint32_t)random(
        20000
      );
  }


  uint8_t idleAsset =
    (
      idlePersonalityUntilMs !=
      0
      &&
      (int32_t)(
        idlePersonalityUntilMs -
        now
      ) >
      0
    )
    ?
    idlePersonalityAsset
    :
    ASSET_IDLE_MASTER;


  decodeFrame(
    idleAsset,
    sourceBuffer
  );


  shiftFrameXY(
    sourceBuffer,
    renderBuffer,
    0,
    bobY
  );


  pushElliFrame(
    renderBuffer
  );


  motionStep =
    (
      motionStep +
      1
    ) %
    8;


  nextFrameMs =
    now +
    170;
}


// ============================================================
// LISTENING
// ============================================================
//
// Approved:
//
// - Listening A / B
// - slow animation
// - gentle idle-like bob
// - deliberately slow blink
// - 220 ms each blink phase
// - body position remains fixed while blinking
//
// ============================================================

void updateListening(
  uint32_t now
) {

  int bobY =
    IDLE_BOB[
      motionStep %
      8
    ];


  uint8_t listenAsset =
    (
      (
        motionStep /
        4
      ) %
      2
    )
    ?
    ASSET_LISTENING_B
    :
    ASSET_LISTENING_A;


  decodeFrame(
    listenAsset,
    sourceBuffer
  );


  if(
    !blinkActive &&
    (
      (int32_t)(
        now -
        nextBlinkMs
      ) >=
      0
    )
  ) {

    blinkActive =
      true;


    blinkStep =
      0;
  }


  if(
    blinkActive
  ) {

    applyGenericBlink(
      sourceBuffer,
      blinkStep
    );


    shiftFrameXY(
      sourceBuffer,
      renderBuffer,
      0,
      bobY
    );


    pushElliFrame(
      renderBuffer
    );


    blinkStep++;


    if(
      blinkStep >=
      3
    ) {

      blinkActive =
        false;


      blinkStep =
        0;


      nextBlinkMs =
        now +
        4000;
    }


    // User-approved deliberately slow blink.

    nextFrameMs =
      now +
      220;


    // motionStep is intentionally NOT changed here.

    return;
  }


  shiftFrameXY(
    sourceBuffer,
    renderBuffer,
    0,
    bobY
  );


  pushElliFrame(
    renderBuffer
  );


  motionStep =
    (
      motionStep +
      1
    ) %
    32;


  nextFrameMs =
    now +
    175;
}


// ============================================================
// THINKING COMPOSER
// ============================================================
//
// Thinking A is the permanent master.
//
// Thinking B contributes ONLY the lower paw.
//
// Result:
//
// - head fixed
// - bow fixed
// - right side of face fixed
// - lower paw moves
//
// ============================================================

void composeThinkingBase(
  bool pawB
) {

  decodeFrame(
    ASSET_THINKING_A,
    sourceBuffer
  );


  if(
    !pawB
  ) {

    return;
  }


  decodeFrame(
    ASSET_THINKING_B,
    renderBuffer
  );


  copyRect(
    sourceBuffer,
    renderBuffer,

    THINK_PAW_X0,
    THINK_PAW_Y0,

    THINK_PAW_X1,
    THINK_PAW_Y1
  );
}


// ============================================================
// THINKING
// ============================================================
//
// Approved:
//
// - lower paw A/B only
// - face/head/bow stay fixed
// - blink
// - idle-like vertical bob
//
// Because Phase 4C places this engine in a FreeRTOS task,
// this continues moving while an HTTP/AI request is waiting.
//
// ============================================================

void updateThinking(
  uint32_t now
) {

  bool pawB =
    (
      (
        motionStep /
        2
      ) %
      2
    ) !=
    0;


  composeThinkingBase(
    pawB
  );


  int bobY =
    IDLE_BOB[
      motionStep %
      8
    ];


  if(
    !blinkActive &&
    (
      (int32_t)(
        now -
        nextBlinkMs
      ) >=
      0
    )
  ) {

    blinkActive =
      true;


    blinkStep =
      0;
  }


  if(
    blinkActive
  ) {

    applyGenericBlink(
      sourceBuffer,
      blinkStep
    );


    blinkStep++;


    if(
      blinkStep >=
      3
    ) {

      blinkActive =
        false;


      blinkStep =
        0;


      nextBlinkMs =
        now +
        3900;
    }


    nextFrameMs =
      now +
      160;
  }

  else {

    nextFrameMs =
      now +
      210;
  }


  shiftFrameXY(
    sourceBuffer,
    renderBuffer,
    0,
    bobY
  );


  pushElliFrame(
    renderBuffer
  );


  if(
    !blinkActive
  ) {

    motionStep =
      (
        motionStep +
        1
      ) %
      16;
  }
}


// ============================================================
// SPEAKING
// ============================================================
//
// Approved:
//
// - body fixed master
// - nose fixed
// - bow fixed
// - eyes fixed except blink
// - ONLY mouth region changes
// - mouth speed ~300 ms
// - whole character moves using idle bob
// - speaking blink ~180 ms phase
//
// ============================================================

void updateSpeaking(
  uint32_t now
) {

  static const uint8_t mouthAssets[4] = {

    ASSET_SPEAK_CLOSED,
    ASSET_SPEAK_SMALL,
    ASSET_SPEAK_MEDIUM,
    ASSET_SPEAK_WIDE
  };


  // Permanent speaking master.

  decodeFrame(
    ASSET_SPEAK_CLOSED,
    sourceBuffer
  );


  bool useRealAudio =
    false;

  uint8_t realAudioLevel =
    0;


  portENTER_CRITICAL(
    &visualMux
  );


  useRealAudio =
    audioSpeechActive;

  realAudioLevel =
    audioSpeechLevel;


  portEXIT_CRITICAL(
    &visualMux
  );


  uint8_t mouthAsset =
    ASSET_SPEAK_CLOSED;


  if(
    useRealAudio
  ) {

    // ========================================================
    // PHASE 5M - REAL PCM-DRIVEN MOUTH
    // ========================================================
    //
    // 0 -> approved asset 7  : closed
    // 1 -> approved asset 8  : small open
    // 2 -> approved asset 9  : medium open
    // 3 -> approved asset 10 : wide open
    // ========================================================

    if(
      realAudioLevel > 3
    ) {
      realAudioLevel = 3;
    }


    mouthAsset =
      mouthAssets[
        realAudioLevel
      ];
  }
  else {

    // Existing timer animation remains as a safe fallback for
    // any future non-audio SPEAKING state.

    static const uint8_t fallbackMouth[6] = {

      ASSET_SPEAK_CLOSED,
      ASSET_SPEAK_SMALL,
      ASSET_SPEAK_MEDIUM,
      ASSET_SPEAK_SMALL,
      ASSET_SPEAK_WIDE,
      ASSET_SPEAK_SMALL
    };


    if(
      (
        (int32_t)(
          now -
          nextMouthMs
        ) >=
        0
      )
    ) {

      mouthStep =
        (
          mouthStep +
          1
        ) %
        6;


      nextMouthMs =
        now +
        300;
    }


    mouthAsset =
      fallbackMouth[
        mouthStep
      ];
  }


  // Decode the selected approved mouth source.

  decodeFrame(
    mouthAsset,
    renderBuffer
  );


  // Copy mouth ONLY. Body / bow / nose / eyes stay unchanged.

  copyRect(
    sourceBuffer,
    renderBuffer,

    MOUTH_X0,
    MOUTH_Y0,

    MOUTH_X1,
    MOUTH_Y1
  );


  // Start blink.

  if(
    !blinkActive &&
    (
      (int32_t)(
        now -
        nextBlinkMs
      ) >=
      0
    )
  ) {

    blinkActive =
      true;


    blinkStep =
      0;
  }


  if(
    blinkActive
  ) {

    applyGenericBlink(
      sourceBuffer,
      blinkStep
    );


    blinkStep++;


    if(
      blinkStep >=
      3
    ) {

      blinkActive =
        false;


      blinkStep =
        0;


      nextBlinkMs =
        now +
        4000;
    }
  }


  int bobY =
    IDLE_BOB[
      motionStep %
      8
    ];


  shiftFrameXY(
    sourceBuffer,
    renderBuffer,
    0,
    bobY
  );


  pushElliFrame(
    renderBuffer
  );


  // Whole character retains the same gentle motion as IDLE.

  motionStep =
    (
      motionStep +
      1
    ) %
    24;


  nextFrameMs =
    now +
    (
      blinkActive
      ?
      180
      :
      300
    );
}


// ============================================================
// HAPPY
// ============================================================
//
// Approved:
//
// - happy master
// - gentle bob
// - slower / calmer blink
// - ~5400 ms blink separation
// - ~135 ms blink frames
//
// ============================================================

void updateHappy(
  uint32_t now
) {

  decodeFrame(
    ASSET_HAPPY,
    sourceBuffer
  );


  if(
    !blinkActive &&
    (
      (int32_t)(
        now -
        nextBlinkMs
      ) >=
      0
    )
  ) {

    blinkActive =
      true;


    blinkStep =
      0;
  }


  if(
    blinkActive
  ) {

    applyGenericBlink(
      sourceBuffer,
      blinkStep
    );


    blinkStep++;


    if(
      blinkStep >=
      3
    ) {

      blinkActive =
        false;


      blinkStep =
        0;


      nextBlinkMs =
        now +
        5400;
    }


    nextFrameMs =
      now +
      135;
  }

  else {

    nextFrameMs =
      now +
      180;
  }


  shiftFrameXY(
    sourceBuffer,
    renderBuffer,
    0,
    IDLE_BOB[
      motionStep %
      8
    ]
  );


  pushElliFrame(
    renderBuffer
  );


  if(
    !blinkActive
  ) {

    motionStep =
      (
        motionStep +
        1
      ) %
      8;
  }
}


// ============================================================
// EXCITED SIDE-LINE REGIONS
// ============================================================
//
// Lines live outside the central head.
//
// To blink one side at a time we restore the inactive side
// from IDLE_MASTER.
//
// We deliberately keep these regions narrow so the face,
// bow and body are not replaced.
//
// ============================================================

constexpr int EXCITED_LEFT_X0 =
  20;

constexpr int EXCITED_LEFT_Y0 =
  65;

constexpr int EXCITED_LEFT_X1 =
  66;

constexpr int EXCITED_LEFT_Y1 =
  165;


constexpr int EXCITED_RIGHT_X0 =
  226;

constexpr int EXCITED_RIGHT_Y0 =
  65;

constexpr int EXCITED_RIGHT_X1 =
  280;

constexpr int EXCITED_RIGHT_Y1 =
  165;


// ============================================================
// EXCITED
// ============================================================
//
// Approved:
//
// - side lines flash faster
// - ONLY one side active at once
// - ~320 ms side phase
// - blink
// - gentle bob
//
// ============================================================

void updateExcited(
  uint32_t now
) {

  // Start from full excited frame.

  decodeFrame(
    ASSET_EXCITED,
    sourceBuffer
  );


  // Idle master used only as a clean reference for whichever
  // side is supposed to be hidden.

  decodeFrame(
    ASSET_IDLE_MASTER,
    renderBuffer
  );


  bool showLeft =
    (
      motionStep %
      2
    ) ==
    0;


  if(
    showLeft
  ) {

    // Hide RIGHT lines.

    copyRect(
      sourceBuffer,
      renderBuffer,

      EXCITED_RIGHT_X0,
      EXCITED_RIGHT_Y0,

      EXCITED_RIGHT_X1,
      EXCITED_RIGHT_Y1
    );
  }

  else {

    // Hide LEFT lines.

    copyRect(
      sourceBuffer,
      renderBuffer,

      EXCITED_LEFT_X0,
      EXCITED_LEFT_Y0,

      EXCITED_LEFT_X1,
      EXCITED_LEFT_Y1
    );
  }


  // Blink timing.

  if(
    !blinkActive &&
    (
      (int32_t)(
        now -
        nextBlinkMs
      ) >=
      0
    )
  ) {

    blinkActive =
      true;


    blinkStep =
      0;
  }


  if(
    blinkActive
  ) {

    applyGenericBlink(
      sourceBuffer,
      blinkStep
    );


    blinkStep++;


    if(
      blinkStep >=
      3
    ) {

      blinkActive =
        false;


      blinkStep =
        0;


      nextBlinkMs =
        now +
        3600;
    }


    nextFrameMs =
      now +
      145;
  }

  else {

    nextFrameMs =
      now +
      320;
  }


  shiftFrameXY(
    sourceBuffer,
    renderBuffer,
    0,
    IDLE_BOB[
      motionStep %
      8
    ]
  );


  pushElliFrame(
    renderBuffer
  );


  if(
    !blinkActive
  ) {

    motionStep =
      (
        motionStep +
        1
      ) %
      8;
  }
}


// ============================================================
// SAD
// ============================================================
//
// Approved:
//
// - sad master
// - gentle bob
// - DOUBLE blink
//
// Blink sequence:
//
// half
// closed
// open
// short gap
// half
// closed
// open
//
// ============================================================

void updateSad(
  uint32_t now
) {

  decodeFrame(
    ASSET_SAD,
    sourceBuffer
  );


  if(
    !blinkActive &&
    (
      (int32_t)(
        now -
        nextBlinkMs
      ) >=
      0
    )
  ) {

    blinkActive =
      true;


    blinkStep =
      0;


    blinkRepeat =
      0;
  }


  if(
    blinkActive
  ) {

    // step 3 is the open-eye pause between the two blinks.

    if(
      blinkStep <=
      2
    ) {

      applyGenericBlink(
        sourceBuffer,
        blinkStep
      );
    }


    if(
      blinkStep ==
      0 ||
      blinkStep ==
      1
    ) {

      blinkStep++;


      nextFrameMs =
        now +
        135;
    }

    else if(
      blinkStep ==
      2
    ) {

      if(
        blinkRepeat ==
        0
      ) {

        blinkRepeat =
          1;


        blinkStep =
          3;


        nextFrameMs =
          now +
          180;
      }

      else {

        blinkActive =
          false;


        blinkStep =
          0;


        blinkRepeat =
          0;


        nextBlinkMs =
          now +
          4200;


        nextFrameMs =
          now +
          170;
      }
    }

    else {

      // End of the gap:
      // begin second blink.

      blinkStep =
        0;


      nextFrameMs =
        now +
        135;
    }
  }

  else {

    nextFrameMs =
      now +
      180;
  }


  shiftFrameXY(
    sourceBuffer,
    renderBuffer,
    0,
    IDLE_BOB[
      motionStep %
      8
    ]
  );


  pushElliFrame(
    renderBuffer
  );


  if(
    !blinkActive
  ) {

    motionStep =
      (
        motionStep +
        1
      ) %
      8;
  }
}


// ============================================================
// HIDE CONFUSED QUESTION MARK
// ============================================================
//
// We compare confused against neutral.
//
// A pixel is removed only if:
//
// 1. it lies inside the question-mark area
// 2. it differs from the neutral reference
// 3. it looks like the pink accent
//
// This prevents us from blanking the bow/face accidentally.
//
// ============================================================

void hideConfusedQuestionMark(
  uint16_t* confusedFrame,
  const uint16_t* neutralFrame
) {

  for(
    int y =
      CONF_Q_Y0;
    y <
      CONF_Q_Y1;
    y++
  ) {

    for(
      int x =
        CONF_Q_X0;
      x <
        CONF_Q_X1;
      x++
    ) {

      uint32_t p =
        (
          (uint32_t)y *
          ELLI_W
        ) +
        x;


      uint16_t a =
        confusedFrame[
          p
        ];


      uint16_t b =
        neutralFrame[
          p
        ];


      if(
        a != b &&
        isPinkAccentPixel(
          a
        )
      ) {

        confusedFrame[
          p
        ] =
          b;
      }
    }
  }
}


// ============================================================
// CONFUSED
// ============================================================
//
// Approved question-mark pattern:
//
// Normal
//
// OFF : 2200–2450 ms
//
// ON
//
// OFF : 2700–2950 ms
//
// Then repeat.
//
// This produces the requested DOUBLE question-mark blink.
//
// ============================================================

void updateConfused(
  uint32_t now
) {

  decodeFrame(
    ASSET_CONFUSED,
    sourceBuffer
  );


  uint32_t elapsed =
    (
      now -
      stateStartMs
    ) %
    4500UL;


  bool questionOff =
    (
      elapsed >=
      2200 &&
      elapsed <
      2450
    )
    ||
    (
      elapsed >=
      2700 &&
      elapsed <
      2950
    );


  if(
    questionOff
  ) {

    decodeFrame(
      ASSET_NEUTRAL,
      renderBuffer
    );


    hideConfusedQuestionMark(
      sourceBuffer,
      renderBuffer
    );
  }


  // Confused can still blink normally.

  if(
    !blinkActive &&
    (
      (int32_t)(
        now -
        nextBlinkMs
      ) >=
      0
    )
  ) {

    blinkActive =
      true;


    blinkStep =
      0;
  }


  if(
    blinkActive
  ) {

    applyGenericBlink(
      sourceBuffer,
      blinkStep
    );


    blinkStep++;


    if(
      blinkStep >=
      3
    ) {

      blinkActive =
        false;


      blinkStep =
        0;


      nextBlinkMs =
        now +
        4200;
    }
  }


  shiftFrameXY(
    sourceBuffer,
    renderBuffer,
    0,
    IDLE_BOB[
      motionStep %
      8
    ]
  );


  pushElliFrame(
    renderBuffer
  );


  motionStep =
    (
      motionStep +
      1
    ) %
    8;


  nextFrameMs =
    now +
    (
      blinkActive
      ?
      150
      :
      170
    );
}


// ============================================================
// BUILD SLEEPY Z MOTION
// ============================================================
//
// CRITICAL:
//
// ONLY blue Z pixels are modified.
//
// The decorative lines next to the Z are intentionally left
// untouched.
//
// ============================================================

void buildSleepyFrame(
  uint8_t zStep
) {

  // Original sleepy frame becomes the working source.

  decodeFrame(
    ASSET_SLEEPY,
    sourceBuffer
  );


  // Background sample.

  uint16_t background =
    sourceBuffer[
      0
    ];


  // Remove ONLY original blue Z pixels from sourceBuffer.

  for(
    int y =
      SLEEP_Z_Y0;
    y <
      SLEEP_Z_Y1;
    y++
  ) {

    for(
      int x =
        SLEEP_Z_X0;
      x <
        SLEEP_Z_X1;
      x++
    ) {

      uint32_t p =
        (
          (uint32_t)y *
          ELLI_W
        ) +
        x;


      if(
        isBlueZPixel(
          sourceBuffer[
            p
          ]
        )
      ) {

        sourceBuffer[
          p
        ] =
          background;
      }
    }
  }


  // Decode pristine sleepy frame into renderBuffer so we still
  // know the original Z locations and colors.

  decodeFrame(
    ASSET_SLEEPY,
    renderBuffer
  );


  static const int scalePercent[4] = {

    94,
    100,
    108,
    100
  };


  static const int yOffsets[4] = {

    2,
    0,
    -3,
    -1
  };


  int scale =
    scalePercent[
      zStep %
      4
    ];


  int offsetY =
    yOffsets[
      zStep %
      4
    ];


  // Approximate center of Z cluster.

  constexpr int CX =
    242;

  constexpr int CY =
    73;


  for(
    int y =
      SLEEP_Z_Y0;
    y <
      SLEEP_Z_Y1;
    y++
  ) {

    for(
      int x =
        SLEEP_Z_X0;
      x <
        SLEEP_Z_X1;
      x++
    ) {

      uint32_t p =
        (
          (uint32_t)y *
          ELLI_W
        ) +
        x;


      uint16_t color =
        renderBuffer[
          p
        ];


      if(
        !isBlueZPixel(
          color
        )
      ) {

        continue;
      }


      int newX =
        CX +
        (
          (
            x -
            CX
          ) *
          scale
        ) /
        100;


      int newY =
        CY +
        (
          (
            y -
            CY
          ) *
          scale
        ) /
        100 +
        offsetY;


      setBufferPixel(
        sourceBuffer,
        newX,
        newY,
        color
      );
    }
  }
}


// ============================================================
// SLEEPY
// ============================================================
//
// Approved:
//
// - eyes never become fully open
// - Z gently grows/moves
// - nearby Z lines remain untouched
// - gentle nod
//
// ============================================================

void updateSleepy(
  uint32_t now
) {

  uint32_t elapsed =
    now -
    stateStartMs;


  uint8_t zStep =
    (
      elapsed /
      450UL
    ) %
    4;


  buildSleepyFrame(
    zStep
  );


  static const int8_t sleepyNod[8] = {

    0,
    0,
    -1,
    -1,
    0,
    0,
    1,
    0
  };


  int dy =
    sleepyNod[
      motionStep %
      8
    ];


  shiftFrameXY(
    sourceBuffer,
    renderBuffer,
    0,
    dy
  );


  pushElliFrame(
    renderBuffer
  );


  motionStep =
    (
      motionStep +
      1
    ) %
    8;


  nextFrameMs =
    now +
    300;
}


// ============================================================
// BUILD ANGRY SYMBOL PULSE
// ============================================================
//
// The angry character remains completely fixed.
//
// We compare ANGRY against NEUTRAL only in the symbol zone.
//
// Differences are treated as symbol pixels and scaled.
//
// This avoids moving the head, face or body.
//
// ============================================================

void buildAngrySymbolFrame(
  int scalePercent
) {

  // Angry master.

  decodeFrame(
    ASSET_ANGRY,
    sourceBuffer
  );


  // Neutral reference.

  decodeFrame(
    ASSET_NEUTRAL,
    renderBuffer
  );


  // First remove symbol-difference pixels from source by
  // restoring the corresponding neutral reference pixel.

  for(
    int y =
      ANGRY_SYMBOL_Y0;
    y <
      ANGRY_SYMBOL_Y1;
    y++
  ) {

    for(
      int x =
        ANGRY_SYMBOL_X0;
      x <
        ANGRY_SYMBOL_X1;
      x++
    ) {

      uint32_t p =
        (
          (uint32_t)y *
          ELLI_W
        ) +
        x;


      if(
        sourceBuffer[
          p
        ] !=
        renderBuffer[
          p
        ]
      ) {

        sourceBuffer[
          p
        ] =
          renderBuffer[
            p
          ];
      }
    }
  }


  // Reload pristine angry asset into renderBuffer.
  //
  // It now acts as the source of symbol pixels.

  decodeFrame(
    ASSET_ANGRY,
    renderBuffer
  );


  // Approximate center of the anger mark.

  constexpr int SYMBOL_CX =
    248;

  constexpr int SYMBOL_CY =
    54;


  // Neutral comparison is no longer available in renderBuffer,
  // so build a symbol mask using sourceBuffer:
  //
  // sourceBuffer currently contains the neutral-restored region.
  //
  // Any pristine angry pixel that differs from that restored
  // pixel belongs to the angry-symbol difference mask.

  for(
    int y =
      ANGRY_SYMBOL_Y0;
    y <
      ANGRY_SYMBOL_Y1;
    y++
  ) {

    for(
      int x =
        ANGRY_SYMBOL_X0;
      x <
        ANGRY_SYMBOL_X1;
      x++
    ) {

      uint32_t p =
        (
          (uint32_t)y *
          ELLI_W
        ) +
        x;


      uint16_t angryPixel =
        renderBuffer[
          p
        ];


      uint16_t restoredPixel =
        sourceBuffer[
          p
        ];


      if(
        angryPixel ==
        restoredPixel
      ) {

        continue;
      }


      int newX =
        SYMBOL_CX +
        (
          (
            x -
            SYMBOL_CX
          ) *
          scalePercent
        ) /
        100;


      int newY =
        SYMBOL_CY +
        (
          (
            y -
            SYMBOL_CY
          ) *
          scalePercent
        ) /
        100;


      setBufferPixel(
        sourceBuffer,
        newX,
        newY,
        angryPixel
      );
    }
  }
}


// ============================================================
// ANGRY
// ============================================================
//
// Approved:
//
// BODY:
// completely fixed
//
// FACE:
// completely fixed except eyelid blink
//
// SYMBOL:
// 94 → 100 → 106 → 100 %
//
// PERIOD:
// 450 ms per symbol step
//
// BLINK:
// closes downward from upper eyelids
//
// ============================================================

void updateAngry(
  uint32_t now
) {

  static const int symbolScale[4] = {

    94,
    100,
    106,
    100
  };


  uint32_t elapsed =
    now -
    stateStartMs;


  uint8_t symbolStep =
    (
      elapsed /
      450UL
    ) %
    4;


  buildAngrySymbolFrame(
    symbolScale[
      symbolStep
    ]
  );


  // Start occasional angry blink.

  if(
    !blinkActive &&
    (
      (int32_t)(
        now -
        nextBlinkMs
      ) >=
      0
    )
  ) {

    blinkActive =
      true;


    blinkStep =
      0;
  }


  if(
    blinkActive
  ) {

    applyAngryBlink(
      sourceBuffer,
      blinkStep
    );


    blinkStep++;


    if(
      blinkStep >=
      3
    ) {

      blinkActive =
        false;


      blinkStep =
        0;


      nextBlinkMs =
        now +
        4300;
    }
  }


  // NO shiftFrameXY here.
  //
  // Angry face/body must NOT wiggle.

  pushElliFrame(
    sourceBuffer
  );


  nextFrameMs =
    now +
    (
      blinkActive
      ?
      170
      :
      225
    );
}


// ============================================================
// IMMEDIATE FIRST STATE FRAME
// ============================================================

void showImmediateStateFrame(
  ElliVisualState state
) {

  uint8_t asset =
    ASSET_IDLE_MASTER;


  switch(
    state
  ) {

    case ELLI_VISUAL_IDLE:

      asset =
        ASSET_IDLE_MASTER;

      break;


    case ELLI_VISUAL_LISTENING:

      asset =
        ASSET_LISTENING_A;

      break;


    case ELLI_VISUAL_THINKING:

      asset =
        ASSET_THINKING_A;

      break;


    case ELLI_VISUAL_SPEAKING:

      asset =
        ASSET_SPEAK_CLOSED;

      break;


    case ELLI_VISUAL_HAPPY:

      asset =
        ASSET_HAPPY;

      break;


    case ELLI_VISUAL_EXCITED:

      asset =
        ASSET_EXCITED;

      break;


    case ELLI_VISUAL_SAD:

      asset =
        ASSET_SAD;

      break;


    case ELLI_VISUAL_CONFUSED:

      asset =
        ASSET_CONFUSED;

      break;


    case ELLI_VISUAL_SLEEPY:

      asset =
        ASSET_SLEEPY;

      break;


    case ELLI_VISUAL_ANGRY:

      asset =
        ASSET_ANGRY;

      break;
  }


  decodeFrame(
    asset,
    renderBuffer
  );


  pushElliFrame(
    renderBuffer
  );
}


// ============================================================
// UPDATE CURRENT ANIMATION
// ============================================================

void updateCurrentAnimation(
  uint32_t now
) {

  if(
    (
      (int32_t)(
        now -
        nextFrameMs
      )
    ) <
    0
  ) {

    return;
  }


  switch(
    currentVisualState
  ) {

    case ELLI_VISUAL_IDLE:

      updateIdle(
        now
      );

      break;


    case ELLI_VISUAL_LISTENING:

      updateListening(
        now
      );

      break;


    case ELLI_VISUAL_THINKING:

      updateThinking(
        now
      );

      break;


    case ELLI_VISUAL_SPEAKING:

      updateSpeaking(
        now
      );

      break;


    case ELLI_VISUAL_HAPPY:

      updateHappy(
        now
      );

      break;


    case ELLI_VISUAL_EXCITED:

      updateExcited(
        now
      );

      break;


    case ELLI_VISUAL_SAD:

      updateSad(
        now
      );

      break;


    case ELLI_VISUAL_CONFUSED:

      updateConfused(
        now
      );

      break;


    case ELLI_VISUAL_SLEEPY:

      updateSleepy(
        now
      );

      break;


    case ELLI_VISUAL_ANGRY:

      updateAngry(
        now
      );

      break;
  }
}


// ============================================================
//          END OF PHASE 4X PART 1
//
// DO NOT add:
//
// }
//
// here.
//
// The anonymous namespace is intentionally still OPEN.
//
// PART 2 continues directly below this line.
// ============================================================// ============================================================
// PART 2 STARTS HERE
// ============================================================


// Close the first anonymous-namespace section temporarily.
//
// We need to include KIRA Network V2 at global scope so its
// declarations retain their correct linkage.

} // namespace


#include "kira_network_v2.h"


// Re-opening an unnamed namespace in the same translation unit
// continues the SAME anonymous namespace used in Part 1.

namespace {


// ============================================================
// STATUS STRIP COLORS - RGB565
// ============================================================

constexpr uint16_t STATUS_BACKGROUND =
  0x1082;


constexpr uint16_t STATUS_DIM =
  0x4208;


constexpr uint16_t STATUS_WHITE =
  0xFFFF;


constexpr uint16_t STATUS_CYAN =
  0x07FF;


constexpr uint16_t STATUS_GREEN =
  0x07E0;


constexpr uint16_t STATUS_YELLOW =
  0xFFE0;


constexpr uint16_t STATUS_RED =
  0xF800;


constexpr uint16_t STATUS_PINK =
  0xF81F;


// ============================================================
// TINY 3 x 5 DIGIT FONT
// ============================================================
//
// The strip is only 32 pixels wide.
//
// We intentionally use a tiny procedural font instead of
// storing another font/bitmap asset in flash.
//
// ============================================================

const uint8_t MINI_DIGITS[10][5] PROGMEM = {

  {
    0b111,
    0b101,
    0b101,
    0b101,
    0b111
  },

  {
    0b010,
    0b110,
    0b010,
    0b010,
    0b111
  },

  {
    0b111,
    0b001,
    0b111,
    0b100,
    0b111
  },

  {
    0b111,
    0b001,
    0b111,
    0b001,
    0b111
  },

  {
    0b101,
    0b101,
    0b111,
    0b001,
    0b001
  },

  {
    0b111,
    0b100,
    0b111,
    0b001,
    0b111
  },

  {
    0b111,
    0b100,
    0b111,
    0b101,
    0b111
  },

  {
    0b111,
    0b001,
    0b010,
    0b010,
    0b010
  },

  {
    0b111,
    0b101,
    0b111,
    0b101,
    0b111
  },

  {
    0b111,
    0b101,
    0b111,
    0b001,
    0b111
  }
};


// ============================================================
// STATUS BUFFER HELPERS
// ============================================================
//
// renderBuffer is much larger than the status strip.
//
// For status drawing we simply use its first:
//
//   32 x 240 = 7680 pixels
//
// The next Elli animation decode overwrites it again.
//
// No third framebuffer.
//
// ============================================================

void clearStatusBuffer(
  uint16_t color
) {

  const uint32_t total =
    (
      (uint32_t)STATUS_W *
      SCREEN_H
    );


  for(
    uint32_t i = 0;
    i < total;
    i++
  ) {

    renderBuffer[
      i
    ] =
      color;
  }
}


// ============================================================
// STATUS PIXEL
// ============================================================

inline void statusPixel(
  int x,
  int y,
  uint16_t color
) {

  if(
    x < 0 ||
    x >= STATUS_W ||
    y < 0 ||
    y >= SCREEN_H
  ) {

    return;
  }


  renderBuffer[
    (
      (uint32_t)y *
      STATUS_W
    ) +
    x
  ] =
    color;
}


// ============================================================
// STATUS RECTANGLE
// ============================================================

void statusRect(
  int x,
  int y,
  int width,
  int height,
  uint16_t color
) {

  for(
    int yy = y;
    yy <
    y + height;
    yy++
  ) {

    for(
      int xx = x;
      xx <
      x + width;
      xx++
    ) {

      statusPixel(
        xx,
        yy,
        color
      );
    }
  }
}


// ============================================================
// STATUS HORIZONTAL LINE
// ============================================================

void statusHLine(
  int x0,
  int x1,
  int y,
  uint16_t color
) {

  if(
    x1 <
    x0
  ) {

    int temp =
      x0;

    x0 =
      x1;

    x1 =
      temp;
  }


  for(
    int x = x0;
    x <= x1;
    x++
  ) {

    statusPixel(
      x,
      y,
      color
    );
  }
}


// ============================================================
// MINI DIGIT
// ============================================================

void drawMiniDigit(
  int digit,
  int x,
  int y,
  int scale,
  uint16_t color
) {

  if(
    digit < 0 ||
    digit > 9
  ) {

    return;
  }


  for(
    int row = 0;
    row < 5;
    row++
  ) {

    uint8_t bits =
      pgm_read_byte(
        &MINI_DIGITS[
          digit
        ][
          row
        ]
      );


    for(
      int column = 0;
      column < 3;
      column++
    ) {

      if(
        bits &
        (
          1 <<
          (
            2 -
            column
          )
        )
      ) {

        statusRect(
          x +
          column *
          scale,

          y +
          row *
          scale,

          scale,

          scale,

          color
        );
      }
    }
  }
}


// ============================================================
// DRAW TWO DIGITS
// ============================================================
//
// Example:
//
// 19
// 24
//
// fits comfortably inside the 32px strip.
//
// ============================================================

void drawTwoDigits(
  int value,
  int y,
  uint16_t color
) {

  value =
    constrain(
      value,
      0,
      99
    );


  int tens =
    value /
    10;


  int ones =
    value %
    10;


  constexpr int SCALE =
    2;


  // Digit width:
  // 3 * 2 = 6 pixels.
  //
  // Two digits + gap:
  // 6 + 4 + 6 = 16 pixels.
  //
  // Centered:
  // (32 - 16) / 2 = 8

  constexpr int START_X =
    8;


  drawMiniDigit(
    tens,
    START_X,
    y,
    SCALE,
    color
  );


  drawMiniDigit(
    ones,
    START_X + 10,
    y,
    SCALE,
    color
  );
}


// ============================================================
// REAL INTERNET SPEED BARS
// ============================================================
//
// IMPORTANT:
// These four lines used to represent Wi-Fi RSSI.
// They now represent KIRA Network V2's latest measured
// INTERNET THROUGHPUT result.
//
// The parameters remain in this helper only so the surrounding
// status-render code does not need to change shape.
// ============================================================

int statusWifiBars(
  bool connected,
  int rssi
) {

  (void)connected;
  (void)rssi;

  return
    kiraNetworkSpeedBars();
}


// ============================================================
// DRAW INTERNET SPEED BARS
// ============================================================

void drawWifiBars(
  bool connected,
  int rssi
) {

  int bars =
    statusWifiBars(
      connected,
      rssi
    );


  for(
    int i = 0;
    i < 4;
    i++
  ) {

    int height =
      5 +
      i *
      5;


    int x =
      4 +
      i *
      6;


    int y =
      31 -
      height;


    uint16_t color =
      (
        i <
        bars
      )
      ?
      STATUS_GREEN
      :
      STATUS_DIM;


    statusRect(
      x,
      y,
      4,
      height,
      color
    );
  }
}


// ============================================================
// NETWORK MODE COLOR
// ============================================================
//
// KIRA Network V2:
//
// OFFLINE
// DEGRADED
// FULL
//
// ============================================================

uint16_t networkModeColor(
  KiraConnectivityMode mode
) {

  switch(
    mode
  ) {

    case KIRA_NET_ONLINE_FULL:

      return
        STATUS_GREEN;


    case KIRA_NET_ONLINE_DEGRADED:

      return
        STATUS_YELLOW;


    default:

      return
        STATUS_RED;
  }
}


// ============================================================
// DRAW NETWORK MODE INDICATOR
// ============================================================

void drawNetworkModeIndicator(
  KiraConnectivityMode mode
) {

  uint16_t color =
    networkModeColor(
      mode
    );


  // 8 x 8 mode lamp.

  statusRect(
    12,
    39,
    8,
    8,
    color
  );


  // Bright center.

  statusRect(
    14,
    41,
    4,
    4,
    STATUS_WHITE
  );
}


// ============================================================
// DRAW CLOCK
// ============================================================

void drawClockStatus(
  int hour,
  int minute,
  bool clockReady
) {

  constexpr int HOUR_Y = 58;
  constexpr int MINUTE_Y = 75;
  constexpr int SEP_Y = 71;


  if(
    !clockReady
  ) {

    // Same-size placeholder as the real HH / MM clock.

    statusHLine(
      9,
      13,
      HOUR_Y + 4,
      STATUS_DIM
    );

    statusHLine(
      18,
      22,
      HOUR_Y + 4,
      STATUS_DIM
    );

    statusHLine(
      9,
      13,
      MINUTE_Y + 4,
      STATUS_DIM
    );

    statusHLine(
      18,
      22,
      MINUTE_Y + 4,
      STATUS_DIM
    );

    return;
  }


  drawTwoDigits(
    hour,
    HOUR_Y,
    STATUS_WHITE
  );


  drawTwoDigits(
    minute,
    MINUTE_Y,
    STATUS_WHITE
  );


  // Separator between HH and MM.

  statusRect(
    14,
    SEP_Y,
    4,
    2,
    STATUS_CYAN
  );
}


// ============================================================
// DATE - SAME SIZE AS TIME
// ============================================================
// DD and MM use the exact same scale-2 renderer as HH and MM.
// Stacking keeps the full-size digits readable inside the 32px strip.
// ============================================================

void drawCompactDate(
  int day,
  int month,
  bool clockReady
) {

  constexpr int DAY_Y = 98;
  constexpr int MONTH_Y = 115;
  constexpr int SEP_Y = 111;


  if(
    !clockReady ||
    day < 1 ||
    day > 31 ||
    month < 1 ||
    month > 12
  ) {

    // Same-size date placeholder as DD / MM.

    statusHLine(
      9,
      13,
      DAY_Y + 4,
      STATUS_DIM
    );

    statusHLine(
      18,
      22,
      DAY_Y + 4,
      STATUS_DIM
    );

    statusHLine(
      9,
      13,
      MONTH_Y + 4,
      STATUS_DIM
    );

    statusHLine(
      18,
      22,
      MONTH_Y + 4,
      STATUS_DIM
    );

    return;
  }


  // IMPORTANT: date now uses drawTwoDigits(), exactly the same
  // scale-2 digit renderer used by the clock.

  drawTwoDigits(
    day,
    DAY_Y,
    STATUS_WHITE
  );


  drawTwoDigits(
    month,
    MONTH_Y,
    STATUS_WHITE
  );


  // Slash-like date separator between DD and MM.

  statusPixel(
    17,
    SEP_Y - 1,
    STATUS_CYAN
  );

  statusPixel(
    16,
    SEP_Y,
    STATUS_CYAN
  );

  statusPixel(
    15,
    SEP_Y + 1,
    STATUS_CYAN
  );

  statusPixel(
    14,
    SEP_Y + 2,
    STATUS_CYAN
  );
}


// ============================================================
// ACTIVITY ICON
// ============================================================
//
// IDLE:
// tiny calm dot
//
// THINKING:
// three animated dots
//
// SPEAKING:
// animated audio bars
//
// LISTENING:
// microphone-style bars
//
// EMOTIONS:
// color indicator
//
// ============================================================

void drawActivityIcon(
  ElliVisualState state,
  uint32_t now
) {

  bool pulse =
    (
      (
        now /
        400UL
      ) %
      2UL
    ) !=
    0;


  if(
    state ==
    ELLI_VISUAL_THINKING
  ) {

    int activeDot =
      (
        now /
        260UL
      ) %
      3;


    for(
      int i = 0;
      i < 3;
      i++
    ) {

      int size =
        (
          i ==
          activeDot
        )
        ?
        5
        :
        3;


      int centerX =
        6 +
        i *
        10;


      statusRect(
        centerX -
        size /
        2,

        157 -
        size /
        2,

        size,

        size,

        STATUS_YELLOW
      );
    }


    return;
  }


  if(
    state ==
    ELLI_VISUAL_SPEAKING
  ) {

    static const uint8_t waveA[4] = {

      7,
      15,
      10,
      5
    };


    static const uint8_t waveB[4] = {

      13,
      6,
      16,
      9
    };


    const uint8_t* heights =
      pulse
      ?
      waveA
      :
      waveB;


    for(
      int i = 0;
      i < 4;
      i++
    ) {

      int height =
        heights[
          i
        ];


      statusRect(
        4 +
        i *
        6,

        166 -
        height,

        3,

        height,

        STATUS_PINK
      );
    }


    return;
  }


  if(
    state ==
    ELLI_VISUAL_LISTENING
  ) {

    static const uint8_t listenA[3] = {

      6,
      14,
      9
    };


    static const uint8_t listenB[3] = {

      11,
      7,
      15
    };


    const uint8_t* heights =
      pulse
      ?
      listenA
      :
      listenB;


    for(
      int i = 0;
      i < 3;
      i++
    ) {

      statusRect(
        7 +
        i *
        7,

        166 -
        heights[
          i
        ],

        3,

        heights[
          i
        ],

        STATUS_CYAN
      );
    }


    return;
  }


  uint16_t emotionColor =
    STATUS_GREEN;


  switch(
    state
  ) {

    case ELLI_VISUAL_HAPPY:

      emotionColor =
        STATUS_PINK;

      break;


    case ELLI_VISUAL_EXCITED:

      emotionColor =
        STATUS_YELLOW;

      break;


    case ELLI_VISUAL_SAD:

      emotionColor =
        STATUS_CYAN;

      break;


    case ELLI_VISUAL_CONFUSED:

      emotionColor =
        STATUS_YELLOW;

      break;


    case ELLI_VISUAL_SLEEPY:

      emotionColor =
        STATUS_CYAN;

      break;


    case ELLI_VISUAL_ANGRY:

      emotionColor =
        STATUS_RED;

      break;


    default:

      emotionColor =
        STATUS_GREEN;

      break;
  }


  int size =
    pulse
    ?
    8
    :
    6;


  statusRect(
    16 -
    size /
    2,

    157 -
    size /
    2,

    size,

    size,

    emotionColor
  );
}


// ============================================================
// NETWORK ICON / LOWER STATUS
// ============================================================
//
// A tiny three-part indicator near the bottom:
//
// left  = Wi-Fi link
// mid   = KIRA provider mode
// right = exhibition mode
//
// ============================================================

void drawLowerIndicators(
  bool connected,
  KiraConnectivityMode mode,
  bool exhibition
) {

  statusRect(
    5,
    228,
    5,
    5,

    connected
    ?
    STATUS_GREEN
    :
    STATUS_RED
  );


  statusRect(
    14,
    228,
    5,
    5,

    networkModeColor(
      mode
    )
  );


  statusRect(
    23,
    228,
    5,
    5,

    exhibition
    ?
    STATUS_PINK
    :
    STATUS_DIM
  );
}


// ============================================================
// DRAW COMPLETE STATUS STRIP
// ============================================================

void drawStatusStrip(
  uint32_t now,
  bool connected,
  int rssi,
  KiraConnectivityMode mode,
  bool exhibition,
  bool clockReady,
  int hour,
  int minute,
  int day,
  int month
) {

  clearStatusBuffer(
    STATUS_BACKGROUND
  );


  // Divider separating Elli from status strip.

  for(
    int y = 0;
    y < SCREEN_H;
    y++
  ) {

    statusPixel(
      0,
      y,
      STATUS_CYAN
    );
  }


  // Wi-Fi.

  drawWifiBars(
    connected,
    rssi
  );


  // Real KIRA network state.

  drawNetworkModeIndicator(
    mode
  );


  // Clock.

  drawClockStatus(
    hour,
    minute,
    clockReady
  );


  drawCompactDate(
    day,
    month,
    clockReady
  );


  // Current Elli/KIRA activity.

  drawActivityIcon(
    currentVisualState,
    now
  );


  // Small separators.

  statusHLine(
    5,
    27,
    52,
    STATUS_DIM
  );


  statusHLine(
    5,
    27,
    134,
    STATUS_DIM
  );


  statusHLine(
    5,
    27,
    176,
    STATUS_DIM
  );


  // Current visual-state marker.
  //
  // Different vertical location makes the display feel more
  // alive without storing icons.

  int markerY =
    185 +
    (
      (
        (int)currentVisualState
      ) %
      8
    ) *
    4;


  statusRect(
    12,
    markerY,
    8,
    3,
    STATUS_PINK
  );


  drawLowerIndicators(
    connected,
    mode,
    exhibition
  );


  pushRGB565Region(
    STATUS_X,
    0,
    STATUS_W,
    SCREEN_H,
    renderBuffer
  );
}


// ============================================================
// STATUS SIGNATURE
// ============================================================
//
// We redraw the strip only when something visible changes.
//
// This saves:
//
// - SPI bandwidth
// - CPU
// - TFT bus contention
//
// THINKING/SPEAKING/LISTENING still pulse approximately every
// 400 ms.
//
// ============================================================

uint32_t makeStatusSignature(
  bool connected,
  int bars,
  KiraConnectivityMode mode,
  bool exhibition,
  bool clockReady,
  int hour,
  int minute,
  int day,
  int month,
  ElliVisualState state,
  bool activityPulse
) {

  uint32_t signature =
    0;


  signature |=
    connected
    ?
    1UL
    :
    0UL;


  signature |=
    (
      (
        uint32_t
      )bars &
      0x07UL
    )
    <<
    1;


  signature |=
    (
      (
        uint32_t
      )mode &
      0x03UL
    )
    <<
    4;


  signature |=
    (
      exhibition
      ?
      1UL
      :
      0UL
    )
    <<
    6;


  signature |=
    (
      clockReady
      ?
      1UL
      :
      0UL
    )
    <<
    7;


  signature |=
    (
      (
        uint32_t
      )(
        hour +
        1
      ) &
      0x1FUL
    )
    <<
    8;


  signature |=
    (
      (
        uint32_t
      )(
        minute +
        1
      ) &
      0x3FUL
    )
    <<
    13;


  signature |=
    (
      (
        uint32_t
      )state &
      0x0FUL
    )
    <<
    19;


  signature |=
    (
      activityPulse
      ?
      1UL
      :
      0UL
    )
    <<
    23;


  // Date is mixed rather than bit-packed because the existing compact
  // 32-bit status signature already uses its lower 24 bits.
  signature ^=
    ((uint32_t)(day + 1) * 2654435761UL);

  signature ^=
    ((uint32_t)(month + 1) * 40503UL);


  return
    signature;
}


// ============================================================
// UPDATE LIVE STATUS
// ============================================================

void updateStatusStrip(
  uint32_t now
) {

  if(
    (
      int32_t
    )(
      now -
      nextStatusCheckMs
    ) <
    0
  ) {

    return;
  }


  nextStatusCheckMs =
    now +
    400;


  // ==========================================================
  // REAL KIRA NETWORK STATE
  // ==========================================================

  bool connected =
    kiraNetworkConnected();


  int rssi =
    kiraNetworkCurrentRssi();


  KiraConnectivityMode mode =
    kiraNetworkMode();


  bool exhibition =
    kiraNetworkExhibitionMode();


  int bars =
    statusWifiBars(
      connected,
      rssi
    );


  // ==========================================================
  // CLOCK
  // ==========================================================

  bool clockReady =
    false;


  int hour =
    -1;


  int minute =
    -1;


  int day =
    -1;


  int month =
    -1;


  struct tm currentTime;


  if(
    getLocalTime(
      &currentTime,
      10
    )
  ) {

    clockReady =
      true;


    hour =
      currentTime.tm_hour;


    minute =
      currentTime.tm_min;


    day =
      currentTime.tm_mday;


    month =
      currentTime.tm_mon + 1;
  }


  // Activity animation only needs a changing bit for active
  // modes.

  bool active =
    currentVisualState ==
      ELLI_VISUAL_THINKING
    ||
    currentVisualState ==
      ELLI_VISUAL_SPEAKING
    ||
    currentVisualState ==
      ELLI_VISUAL_LISTENING;


  bool activityPulse =
    active
    &&
    (
      (
        now /
        400UL
      ) %
      2UL
    );


  uint32_t signature =
    makeStatusSignature(
      connected,
      bars,
      mode,
      exhibition,
      clockReady,
      hour,
      minute,
      day,
      month,
      currentVisualState,
      activityPulse
    );


  if(
    signature ==
    lastStatusSignature
  ) {

    return;
  }


  lastStatusSignature =
    signature;


  drawStatusStrip(
    now,
    connected,
    rssi,
    mode,
    exhibition,
    clockReady,
    hour,
    minute,
    day,
    month
  );
}


// ============================================================
// RESPONSE-LENGTH SPEAKING DURATION
// ============================================================
//
// Temporary only until Phase 8 provides REAL audio timing.
//
// A long answer should visibly speak longer than a one-word
// reply.
//
// ============================================================

uint32_t speakingDurationForText(
  const String& text
) {

  uint32_t duration =
    1200UL +
    (
      (uint32_t)text.length() *
      45UL
    );


  if(
    duration <
    1800UL
  ) {

    duration =
      1800UL;
  }


  if(
    duration >
    8500UL
  ) {

    duration =
      8500UL;
  }


  return
    duration;
}


// ============================================================
// TEXT NORMALIZATION FOR VISUAL REACTION
// ============================================================

String reactionText(
  const String& input
) {

  String text =
    input;


  text.toLowerCase();


  text.trim();


  return
    text;
}


// ============================================================
// PHRASE MATCH
// ============================================================

bool containsReactionPhrase(
  const String& text,
  const char* const phrases[],
  size_t count
) {

  for(
    size_t i = 0;
    i < count;
    i++
  ) {

    if(
      text.indexOf(
        phrases[
          i
        ]
      ) >=
      0
    ) {

      return true;
    }
  }


  return false;
}


// ============================================================
// CLASSIFY AUTOMATIC REACTION
// ============================================================
//
// IMPORTANT EXHIBITION POLICY:
//
// We do NOT trigger emotions from one loose word.
//
// Example:
//
//   "Anger is an emotion..."
//
// must NOT make Elli angry.
//
// We therefore use conservative phrase groups representing
// actual response outcomes.
//
// ANGRY remains fully implemented but is not automatically
// inferred unless a future explicit KIRA emotion signal asks
// for it.
//
// ============================================================

ElliVisualState classifyReplyReaction(
  const String& reply
) {

  String text =
    reactionText(
      reply
    );


  // ==========================================================
  // CONFUSED / CLARIFICATION
  // ==========================================================

  const char* const confusedPhrases[] = {

    "which one do you mean",
    "which one did you mean",
    "what do you mean by",
    "i need one more detail",
    "tell me which",
    "could you clarify",
    "i couldn't read",
    "i still couldn't read",
    "that could be am or pm",
    "i need a place name",
    "i need more context",
    "i don't have enough context",
    "i'm not sure which",
    "i can go deeper — tell me which",
    "i can go deeper - tell me which"
  };


  if(
    containsReactionPhrase(
      text,
      confusedPhrases,
      sizeof(
        confusedPhrases
      ) /
      sizeof(
        confusedPhrases[
          0
        ]
      )
    )
  ) {

    return
      ELLI_VISUAL_CONFUSED;
  }


  // ==========================================================
  // SAD / SYSTEM PROBLEM
  // ==========================================================
  //
  // Phase 5E: system/network failures get a visible reaction.
  // The phrases are specific so normal educational answers do
  // not randomly trigger a system-error emotion.
  // ==========================================================

  const char* const systemProblemPhrases[] = {

    "no usable internet",
    "internet is unavailable",
    "internet is dead",
    "currently has no usable internet",
    "neither tested network currently has usable internet",
    "not connected to a configured wi-fi",
    "couldn't reach an online",
    "i couldn't reach an online",
    "connection failed",
    "microphone failed",
    "speaker failed",
    "storage failed",
    "i can't reach the internet"
  };


  if(
    containsReactionPhrase(
      text,
      systemProblemPhrases,
      sizeof(
        systemProblemPhrases
      ) /
      sizeof(
        systemProblemPhrases[
          0
        ]
      )
    )
  ) {

    return
      ELLI_VISUAL_SAD;
  }


  // ==========================================================
  // SAD / SUPPORTIVE
  // ==========================================================

  const char* const supportivePhrases[] = {

    "i'm here with you",
    "i am here with you",
    "that sounds difficult",
    "that sounds hard",
    "that sounds stressful",
    "that sounds upsetting",
    "i'm sorry you're dealing with",
    "i am sorry you're dealing with",
    "i'm sorry you are dealing with",
    "i can stay with you",
    "we can work through this",
    "let's work through this"
  };


  if(
    containsReactionPhrase(
      text,
      supportivePhrases,
      sizeof(
        supportivePhrases
      ) /
      sizeof(
        supportivePhrases[
          0
        ]
      )
    )
  ) {

    return
      ELLI_VISUAL_SAD;
  }


  // ==========================================================
  // EXCITED
  // ==========================================================

  const char* const excitedPhrases[] = {

    "that's awesome",
    "that is awesome",
    "that's amazing",
    "that is amazing",
    "excellent!",
    "congratulations",
    "we did it",
    "perfect!",
    "yess!",
    "yay!"
  };


  if(
    containsReactionPhrase(
      text,
      excitedPhrases,
      sizeof(
        excitedPhrases
      ) /
      sizeof(
        excitedPhrases[
          0
        ]
      )
    )
  ) {

    return
      ELLI_VISUAL_EXCITED;
  }


  // ==========================================================
  // HAPPY / SUCCESS
  // ==========================================================

  const char* const successPhrases[] = {

    "all set",
    "done.",
    "done!",
    "saved.",
    "saved!",
    "completed.",
    "completed!",
    "successfully",
    "ready.",
    "ready!",
    "connected again",
    "selected the better usable internet connection",
    "selected a working internet connection",
    "scene ready",
    "scene set",
    "all devices are off",
    "all four devices are off",
    "all four devices are on",
    "timer started",
    "alarm set",
    "note saved",
    "task added",
    "task completed",
    "turned on",
    "turned off",
    "got it",
    "sounds good"
  };


  if(
    containsReactionPhrase(
      text,
      successPhrases,
      sizeof(
        successPhrases
      ) /
      sizeof(
        successPhrases[
          0
        ]
      )
    )
  ) {

    return
      ELLI_VISUAL_HAPPY;
  }


  // Generic knowledge/conversation answer:
  //
  // no fake emotion.
  //
  // Go straight to SPEAKING.

  return
    ELLI_VISUAL_IDLE;
}


// ============================================================
// INTERNAL STATE CHANGE
// ============================================================

void setCurrentState(
  ElliVisualState state,
  uint32_t now
) {

  portENTER_CRITICAL(
    &visualMux
  );


  currentVisualState =
    state;


  portEXIT_CRITICAL(
    &visualMux
  );


  resetAnimation(
    now
  );


  showImmediateStateFrame(
    state
  );


  lastStatusSignature =
    0xFFFFFFFFUL;


  Serial.print(
    "[ELLI/VISUAL] State -> "
  );


  switch(
    state
  ) {

    case ELLI_VISUAL_IDLE:

      Serial.println(
        "IDLE"
      );

      break;


    case ELLI_VISUAL_LISTENING:

      Serial.println(
        "LISTENING"
      );

      break;


    case ELLI_VISUAL_THINKING:

      Serial.println(
        "THINKING"
      );

      break;


    case ELLI_VISUAL_SPEAKING:

      Serial.println(
        "SPEAKING"
      );

      break;


    case ELLI_VISUAL_HAPPY:

      Serial.println(
        "HAPPY"
      );

      break;


    case ELLI_VISUAL_EXCITED:

      Serial.println(
        "EXCITED"
      );

      break;


    case ELLI_VISUAL_SAD:

      Serial.println(
        "SAD"
      );

      break;


    case ELLI_VISUAL_CONFUSED:

      Serial.println(
        "CONFUSED"
      );

      break;


    case ELLI_VISUAL_SLEEPY:

      Serial.println(
        "SLEEPY"
      );

      break;


    case ELLI_VISUAL_ANGRY:

      Serial.println(
        "ANGRY"
      );

      break;
  }
}


// ============================================================
// BEGIN AUTOMATIC SPEAK SEQUENCE
// ============================================================

void beginAutomaticSpeak(
  ElliVisualState reaction,
  uint32_t speakDuration,
  uint32_t now
) {

  currentSpeakDurationMs =
    speakDuration;


  if(
    reaction !=
    ELLI_VISUAL_IDLE
  ) {

    autoSpeakStage =
      1;


    autoSpeakDeadline =
      now +
      REACTION_HOLD_MS;


    setCurrentState(
      reaction,
      now
    );


    return;
  }


  // No emotional reaction:
  // go directly to speaking.

  autoSpeakStage =
    2;


  autoSpeakDeadline =
    now +
    currentSpeakDurationMs;


  setCurrentState(
    ELLI_VISUAL_SPEAKING,
    now
  );
}


// ============================================================
// HANDLE BRAIN REQUESTS
// ============================================================

void handleVisualRequests(
  uint32_t now
) {

  bool haveSpeakRequest =
    false;


  bool haveStateRequest =
    false;


  ElliVisualState reaction =
    ELLI_VISUAL_IDLE;


  ElliVisualState state =
    ELLI_VISUAL_IDLE;


  uint32_t duration =
    1800;


  portENTER_CRITICAL(
    &visualMux
  );


  // A completed reply has priority over an older THINKING
  // state request.

  if(
    pendingSpeakRequest
  ) {

    haveSpeakRequest =
      true;


    reaction =
      pendingReaction;


    duration =
      currentSpeakDurationMs;


    pendingSpeakRequest =
      false;


    pendingStateRequest =
      false;
  }

  else if(
    pendingStateRequest
  ) {

    haveStateRequest =
      true;


    state =
      pendingState;


    pendingStateRequest =
      false;
  }


  portEXIT_CRITICAL(
    &visualMux
  );


  if(
    haveSpeakRequest
  ) {

    beginAutomaticSpeak(
      reaction,
      duration,
      now
    );


    return;
  }


  if(
    haveStateRequest
  ) {

    // A new explicit state cancels any old automatic
    // emotion/speaking sequence.

    autoSpeakStage =
      0;


    autoSpeakDeadline =
      0;


    setCurrentState(
      state,
      now
    );
  }
}


// ============================================================
// UPDATE AUTOMATIC REACTION/SPEAK SEQUENCE
// ============================================================

void updateAutomaticSpeak(
  uint32_t now
) {


  // Phase 5M: real TTS audio owns the SPEAKING lifetime.
  // Never let the old text-length timer end the animation while
  // PCM is still coming out of the speaker.
  bool realAudioActive =
    false;


  portENTER_CRITICAL(
    &visualMux
  );


  realAudioActive =
    audioSpeechActive;


  portEXIT_CRITICAL(
    &visualMux
  );


  if(
    realAudioActive
  ) {
    return;
  }

  if(
    autoSpeakStage ==
    0
  ) {

    return;
  }


  if(
    (
      int32_t
    )(
      now -
      autoSpeakDeadline
    ) <
    0
  ) {

    return;
  }


  // ==========================================================
  // REACTION -> SPEAKING
  // ==========================================================

  if(
    autoSpeakStage ==
    1
  ) {

    autoSpeakStage =
      2;


    autoSpeakDeadline =
      now +
      currentSpeakDurationMs;


    setCurrentState(
      ELLI_VISUAL_SPEAKING,
      now
    );


    return;
  }


  // ==========================================================
  // SPEAKING -> IDLE
  // ==========================================================

  autoSpeakStage =
    0;


  autoSpeakDeadline =
    0;


  // Speech sequence is NOW genuinely complete.
  //
  // It is safe to allow IDLE requests again.

  portENTER_CRITICAL(
    &visualMux
  );


  commandSpeechIssued =
    false;


  bool goSleep =
    sleepAfterSpeechRequested;


  sleepAfterSpeechRequested =
    false;


  portEXIT_CRITICAL(
    &visualMux
  );


  setCurrentState(
    goSleep
      ?
      ELLI_VISUAL_SLEEPY
      :
      ELLI_VISUAL_IDLE,
    now
  );
}


// ============================================================
// AUTO SLEEPY
// ============================================================
//
// Elli becomes sleepy only after five minutes of true idle.
//
// Any command or visual activity wakes her immediately.
//
// ============================================================

void updateAutomaticSleepy(
  uint32_t now
) {

  if(
    currentVisualState !=
    ELLI_VISUAL_IDLE
  ) {

    return;
  }


  if(
    autoSpeakStage !=
    0
  ) {

    return;
  }


  if(
    lastActivityMs ==
    0
  ) {

    return;
  }


  if(
    (
      now -
      lastActivityMs
    ) >=
    SLEEPY_AFTER_MS
  ) {

    setCurrentState(
      ELLI_VISUAL_SLEEPY,
      now
    );
  }
}


// ============================================================
// VISUAL ENGINE SERVICE
// ============================================================

void serviceVisualEngine() {

  if(
    !displayReady
  ) {

    return;
  }


  uint32_t now =
    millis();


  handleVisualRequests(
    now
  );


  updateAutomaticSpeak(
    now
  );


  updateAutomaticSleepy(
    now
  );


  updateCurrentAnimation(
    now
  );


  updateStatusStrip(
    now
  );
}


// ============================================================
// FREERTOS VISUAL TASK
// ============================================================
//
// This is Phase 4C.
//
// Even while the regular Arduino loop is waiting for an AI/web
// operation, FreeRTOS can schedule this task and Elli continues
// animating.
//
// ============================================================

void elliVisualTask(
  void* parameter
) {

  (void)parameter;


  Serial.print(
    "[ELLI/VISUAL] FreeRTOS visual task on Core "
  );


  Serial.println(
    xPortGetCoreID()
  );


  while(
    true
  ) {

    serviceVisualEngine();


    // Short sleep:
    //
    // animation functions themselves decide actual frame timing.

    vTaskDelay(
      pdMS_TO_TICKS(
        8
      )
    );
  }
}


// ============================================================
// END INTERNAL NAMESPACE
// ============================================================

} // namespace


// ============================================================
// PUBLIC BEGIN
// ============================================================

void elliVisualBegin() {

  if(
    displayReady
  ) {

    return;
  }


  Serial.println();


  Serial.println(
    "[ELLI/VISUAL] Phase 4X EXHIBITION starting..."
  );


  // ==========================================================
  // TFT CONTROL PINS
  // ==========================================================

  pinMode(
    TFT_CS,
    OUTPUT
  );


  pinMode(
    TFT_DC,
    OUTPUT
  );


  pinMode(
    TFT_RST,
    OUTPUT
  );


  digitalWrite(
    TFT_CS,
    HIGH
  );


  digitalWrite(
    TFT_DC,
    HIGH
  );


  digitalWrite(
    TFT_RST,
    HIGH
  );


  // ==========================================================
  // SHARED SPI
  // ==========================================================
  //
  // KIRA has already initialized the shared SPI bus.
  //
  // DO NOT call SPI.begin() here.
  //
  // ==========================================================


  // ==========================================================
  // TWO PSRAM BUFFERS ONLY
  // ==========================================================

  const size_t bufferBytes =
    (
      (size_t)ELLI_W *
      ELLI_H *
      sizeof(
        uint16_t
      )
    );


  sourceBuffer =
    (
      uint16_t*
    )
    heap_caps_malloc(
      bufferBytes,

      MALLOC_CAP_SPIRAM |
      MALLOC_CAP_8BIT
    );


  renderBuffer =
    (
      uint16_t*
    )
    heap_caps_malloc(
      bufferBytes,

      MALLOC_CAP_SPIRAM |
      MALLOC_CAP_8BIT
    );


  if(
    sourceBuffer ==
      nullptr ||
    renderBuffer ==
      nullptr
  ) {

    Serial.println(
      "[ELLI/VISUAL] ERROR: PSRAM framebuffer allocation failed."
    );


    Serial.println(
      "[ELLI/VISUAL] KIRA brain will continue without the display."
    );


    displayReady =
      false;


    return;
  }


  Serial.print(
    "[ELLI/VISUAL] Buffer bytes each: "
  );


  Serial.println(
    bufferBytes
  );


  Serial.print(
    "[ELLI/VISUAL] Two buffers total: "
  );


  Serial.println(
    bufferBytes *
    2
  );


  // ==========================================================
  // TFT INITIALIZATION
  // ==========================================================

  initTft();


  displayReady =
    true;


  uint32_t now =
    millis();


  lastActivityMs =
    now;


  currentVisualState =
    ELLI_VISUAL_IDLE;


  resetAnimation(
    now
  );


  showImmediateStateFrame(
    ELLI_VISUAL_IDLE
  );


  // Force immediate status render.

  nextStatusCheckMs =
    0;


  lastStatusSignature =
    0xFFFFFFFFUL;


  // ==========================================================
  // CREATE FREERTOS ANIMATION TASK
  // ==========================================================

  BaseType_t result =
    xTaskCreatePinnedToCore(
      elliVisualTask,

      "ElliVisual",

      VISUAL_TASK_STACK,

      nullptr,

      VISUAL_TASK_PRIORITY,

      &visualTaskHandle,

      VISUAL_TASK_CORE
    );


  if(
    result ==
    pdPASS
  ) {

    visualTaskRunning =
      true;


    Serial.println(
      "[ELLI/VISUAL] Independent animation task READY."
    );
  }

  else {

    visualTaskRunning =
      false;


    Serial.println(
      "[ELLI/VISUAL] WARNING: FreeRTOS task creation failed."
    );


    Serial.println(
      "[ELLI/VISUAL] Falling back to main-loop rendering."
    );
  }


  Serial.println(
    "[ELLI/VISUAL] Phase 4X EXHIBITION READY."
  );
}


// ============================================================
// PUBLIC UPDATE
// ============================================================
//
// Your main .ino can keep calling:
//
// elliVisualUpdate();
//
// When FreeRTOS task creation succeeded, this function is
// intentionally almost free.
//
// ============================================================

void elliVisualUpdate() {

  if(
    !displayReady
  ) {

    return;
  }


  if(
    visualTaskRunning
  ) {

    return;
  }


  // Emergency fallback if the FreeRTOS task could not start.

  serviceVisualEngine();
}


// ============================================================
// PUBLIC SET STATE
// ============================================================

void elliVisualSetState(
  ElliVisualState state
) {

  if(
    !displayReady
  ) {

    return;
  }


  elliVisualNotifyActivity();


  portENTER_CRITICAL(
    &visualMux
  );


  // ==========================================================
  // SPEECH PROTECTION
  // ==========================================================
  //
  // Once elliSay() has queued a response, a late IDLE request
  // must NOT cancel:
  //
  // reaction -> SPEAKING -> IDLE
  //
  // The visual task itself will return to IDLE when speaking
  // has genuinely finished.
  //
  // ==========================================================

  if(
    state ==
      ELLI_VISUAL_IDLE
    &&
    (
      commandSpeechIssued
      ||
      pendingSpeakRequest
    )
  ) {

    portEXIT_CRITICAL(
      &visualMux
    );


    return;
  }


  pendingState =
    state;


  pendingStateRequest =
    true;


  portEXIT_CRITICAL(
    &visualMux
  );
}


// ============================================================
// PUBLIC GET STATE
// ============================================================

ElliVisualState elliVisualGetState() {

  ElliVisualState state;


  portENTER_CRITICAL(
    &visualMux
  );


  state =
    currentVisualState;


  portEXIT_CRITICAL(
    &visualMux
  );


  return
    state;
}


// ============================================================
// DISPLAY READY
// ============================================================

bool elliVisualReady() {

  return
    displayReady;
}


// ============================================================
// ACTIVITY
// ============================================================

void elliVisualNotifyActivity() {

  portENTER_CRITICAL(
    &visualMux
  );


  lastActivityMs =
    millis();


  // Fresh interaction cancels an old scheduled sleep.
  sleepAfterSpeechRequested =
    false;


  portEXIT_CRITICAL(
    &visualMux
  );
}


// ============================================================
// COMMAND START
// ============================================================
//
// As soon as KIRA receives a valid command:
//
// THINKING begins.
//
// The actual THINKING animation then continues independently.
//
// ============================================================

void elliVisualCommandStart() {

  uint32_t now =
    millis();


  portENTER_CRITICAL(
    &visualMux
  );


  lastActivityMs =
    now;


  commandSpeechIssued =
    false;


  sleepAfterSpeechRequested =
    false;


  pendingState =
    ELLI_VISUAL_THINKING;


  pendingStateRequest =
    true;


  portEXIT_CRITICAL(
    &visualMux
  );
}


// ============================================================
// SPEAK TEXT
// ============================================================
//
// KIRA's brain does NOT change.
//
// This function observes the response for VISUAL PURPOSES only.
//
// It never rewrites:
// - response
// - route
// - memory
// - tool
// - provider
//
// ============================================================

void elliVisualSpeakText(
  const String& text
) {

  ElliVisualState reaction =
    classifyReplyReaction(
      text
    );


  uint32_t duration =
    speakingDurationForText(
      text
    );


  uint32_t now =
    millis();


  portENTER_CRITICAL(
    &visualMux
  );


  lastActivityMs =
    now;


  // Protect the reply sequence against any late IDLE request.

  commandSpeechIssued =
    true;


  pendingReaction =
    reaction;


  currentSpeakDurationMs =
    duration;


  // The reply has arrived.
  //
  // THINKING or any stale IDLE request is no longer relevant.

  pendingStateRequest =
    false;


  pendingSpeakRequest =
    true;


  portEXIT_CRITICAL(
    &visualMux
  );


  // Useful permanent diagnostic.
  //
  // When you ask "hello" we MUST see this.

  Serial.print(
    "[ELLI/VISUAL] Speech queued: "
  );


  Serial.print(
    duration
  );


  Serial.println(
    " ms"
  );
}


// ============================================================
// PHASE 5M - REAL AUDIO SPEECH BEGIN
// ============================================================

void elliVisualAudioSpeechBegin() {

  if(
    !displayReady
  ) {
    return;
  }


  uint32_t now =
    millis();


  portENTER_CRITICAL(
    &visualMux
  );


  lastActivityMs =
    now;

  commandSpeechIssued =
    true;

  audioSpeechActive =
    true;

  audioSpeechLevel =
    0;


  // Real audio supersedes the old estimated-duration request.
  pendingSpeakRequest =
    false;

  pendingState =
    ELLI_VISUAL_SPEAKING;

  pendingStateRequest =
    true;


  portEXIT_CRITICAL(
    &visualMux
  );


  Serial.println(
    "[ELLI/5M] Real audio mouth START"
  );
}


// ============================================================
// PHASE 5M - PCM LEVEL -> APPROVED MOUTH FRAME
// ============================================================

void elliVisualAudioSpeechLevel(
  uint8_t level
) {

  if(
    level > 3
  ) {
    level = 3;
  }


  portENTER_CRITICAL(
    &visualMux
  );


  audioSpeechLevel =
    level;


  portEXIT_CRITICAL(
    &visualMux
  );
}


// ============================================================
// PHASE 5M - REAL AUDIO SPEECH END
// ============================================================

void elliVisualAudioSpeechEnd() {

  if(
    !displayReady
  ) {
    return;
  }


  bool goSleep =
    false;


  portENTER_CRITICAL(
    &visualMux
  );


  audioSpeechLevel =
    0;

  audioSpeechActive =
    false;

  commandSpeechIssued =
    false;


  goSleep =
    sleepAfterSpeechRequested;

  sleepAfterSpeechRequested =
    false;


  pendingSpeakRequest =
    false;

  pendingState =
    goSleep
      ?
      ELLI_VISUAL_SLEEPY
      :
      ELLI_VISUAL_IDLE;

  pendingStateRequest =
    true;


  portEXIT_CRITICAL(
    &visualMux
  );


  Serial.println(
    "[ELLI/5M] Real audio mouth END"
  );
}


// ============================================================
// PHASE 5G - SLEEP AFTER CURRENT REPLY
// ============================================================

void elliVisualSleepAfterSpeech() {

  if(
    !displayReady
  ) {

    return;
  }


  portENTER_CRITICAL(
    &visualMux
  );


  sleepAfterSpeechRequested =
    true;


  portEXIT_CRITICAL(
    &visualMux
  );
}


// ============================================================
// COMMAND END
// ============================================================
//
// If KIRA printed only diagnostic/self-test Serial output and
// never called elliSay(), THINKING must not remain forever.
//
// If KIRA DID speak, its automatic reaction/speaking sequence
// is allowed to finish naturally.
//
// ============================================================

void elliVisualCommandEnd() {

  bool speechIssued;


  portENTER_CRITICAL(
    &visualMux
  );


  speechIssued =
    commandSpeechIssued;


  portEXIT_CRITICAL(
    &visualMux
  );


  if(
    !speechIssued
  ) {

    elliVisualSetState(
      ELLI_VISUAL_IDLE
    );
  }
}


// ============================================================
// DISPLAY DISABLED BUILD
// ============================================================

#else


void elliVisualBegin() {
}


void elliVisualUpdate() {
}


bool elliVisualReady() {

  return
    false;
}


void elliVisualSetState(
  ElliVisualState state
) {

  (void)state;
}


ElliVisualState elliVisualGetState() {

  return
    ELLI_VISUAL_IDLE;
}


void elliVisualCommandStart() {
}


void elliVisualCommandEnd() {
}


void elliVisualSpeakText(
  const String& text
) {

  (void)text;
}


void elliVisualNotifyActivity() {
}



void elliVisualSleepAfterSpeech() {
}



void elliVisualAudioSpeechBegin() {
}


void elliVisualAudioSpeechLevel(
  uint8_t level
) {

  (void)level;
}


void elliVisualAudioSpeechEnd() {
}


#endif