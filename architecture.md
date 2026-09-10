# KIRA Architecture

KIRA (Kinetic Interactive Reactive Assistant) is an ESP32-S3 based AI voice assistant with local wake-word detection, hybrid offline/online voice routing, audio playback, hardware tools, and an animated assistant character named Elli.

## High-Level Architecture

```text
Microphone
   |
   v
ESP-SR AFE / VAD / WakeNet
   |
   +----> Local wake-word detection
   |
   +----> Offline command path
   |         |
   |         v
   |      KIRA Router
   |         |
   |         v
   |      Local actions / tools
   |
   +----> Online speech path
             |
             v
          STT layer
             |
             v
      KIRA Universal Brain
       |       |       |
       |       |       +--> Tools / Devices
       |       +----------> Web / AI providers
       +------------------> Context / Memory
             |
             v
            TTS
             |
             v
      Audio playback engine
             |
             v
      MAX98357A + Speaker
```

## Elli State System

Elli reflects the internal state of KIRA:

```text
IDLE
  |
  v
WAKE
  |
  v
LISTENING
  |
  v
THINKING
  |
  v
SPEAKING
  |
  v
IDLE
```

Additional visual states may include Wi-Fi connecting, Wi-Fi error, curious, surprised, worried, and excited.

## Design Principles

- Wake-word detection should remain local.
- Known commands should work offline where practical.
- Open-ended questions may use online STT and AI services.
- Network loss should not restart the whole device.
- Audio processing should use queues/buffers rather than blocking critical callbacks.
- KIRA remains the main application brain even when borrowing engineering ideas from XiaoZhi.
- Secrets such as Wi-Fi passwords and API keys are never stored in this public repository.

## Target MCU

- ESP32-S3-WROOM-1 N16R8
- 16 MB Flash
- 8 MB PSRAM

## Current Direction

KIRA is moving toward a hybrid streaming architecture using:

- ESP-SR for AFE, VAD and WakeNet
- Opus-ready streaming audio
- WebSocket-style session transport
- buffered TTS playback
- interrupt / barge-in handling
- reactive Elli state events
