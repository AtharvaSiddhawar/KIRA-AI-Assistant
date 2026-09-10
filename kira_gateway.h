#pragma once

#include <Arduino.h>

bool kiraGatewayBegin();
void kiraGatewayService();

bool kiraGatewayEnabled();
bool kiraGatewayReady();

bool kiraGatewaySendControl(
  const String& eventName,
  const String& payloadJson = "{}"
);

bool kiraGatewaySendAudio(
  const uint8_t* data,
  size_t bytes
);

bool kiraGatewayStartListening(
  const char* mode = "auto"
);

bool kiraGatewayStopListening();

bool kiraGatewayAbort(
  const char* reason = "user_interrupt"
);

const String& kiraGatewaySessionId();
const String& kiraGatewayLastTranscript();

uint32_t kiraGatewayProtocolRxCount();
uint32_t kiraGatewayProtocolErrorCount();
uint32_t kiraGatewayAudioRxBytes();

const char* kiraGatewayModeName();
