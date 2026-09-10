# KIRA Hardware

## Main Controller

**ESP32-S3-WROOM-1 N16R8**

- 16 MB Flash
- 8 MB PSRAM
- dual-core ESP32-S3
- Wi-Fi
- native USB support
- ESP-SR compatible

## Audio Input

KIRA uses a digital microphone path suitable for ESP-SR processing.

Audio goals:
- local wake-word detection
- VAD
- speech capture
- future streaming STT
- noise-tolerant operation

## Audio Output

Current output path:

```text
ESP32-S3
   |
   v
I2S
   |
   v
MAX98357A
   |
   v
Speaker
```

A stable power supply and common ground are important for clean playback.

## Display

KIRA uses a display for Elli, the animated assistant character.

Planned display information includes:
- Elli animations
- Wi-Fi state
- time
- date
- assistant state

## Power

Recommended development target:
- stable 5 V supply
- sufficient current headroom for ESP32-S3 + amplifier + display
- common ground
- local decoupling near the audio amplifier

## Future Hardware

Potential KIRA-connected devices include:
- relay-based room automation
- lights
- fan control
- buttons
- additional sensors
- local status LEDs

## USB / Storage

USB pendrive support was experimentally tested separately from the main KIRA firmware and is currently deferred. It is not required for the present KIRA development path.
