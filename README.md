# KIRA – Kinetic Interactive Reactive Assistant

KIRA is a personal AI voice-assistant project built around the ESP32-S3.

## Hardware

- ESP32-S3-WROOM-1 N16R8
- 16 MB Flash
- 8 MB PSRAM
- Digital microphone
- MAX98357A I2S amplifier
- Speaker
- LCD display for Elli, KIRA's animated assistant character

## Current Features

- ESP-SR local wake-word detection
- Current WakeNet9 "Hi ESP" wake word
- Voice activity detection
- Offline command recognition
- Online speech-to-text fallback
- AI/Web response system
- Text-to-speech
- Elli state and emotion system
- Wi-Fi connectivity
- Local/online hybrid routing

## Custom Wake Word Goal

KIRA currently uses the standard ESP-SR "Hi ESP" model.

The goal is to replace it with a custom local wake phrase:

**Hey Elli**

Pronunciation: **Hey EL-ee**

IPA: **/heɪ ˈɛli/**

Target:
- ESP32-S3
- WakeNet9
- English
- TTS Pipeline V3

Wake-word recognition will remain entirely local on the ESP32-S3.

## Project Status

KIRA is under active development.

The main firmware repository is currently kept private because it contains
device-specific configuration and API integrations. This public repository
is used for project documentation and development information.
