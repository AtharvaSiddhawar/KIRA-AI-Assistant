# KIRA Custom Wake Word

## Current Wake Word

KIRA currently uses the standard ESP-SR WakeNet wake phrase:

**Hi ESP**

## First Custom Target

The first custom KIRA wake phrase is:

**Hey Elli**

Pronunciation:

**Hey EL-ee**

IPA:

**/heɪ ˈɛli/**

Target hardware:
- ESP32-S3
- WakeNet-compatible ESP-SR model
- local inference

## Why Start With One Phrase

Only one custom wake phrase should be integrated first so the following can be validated independently:

```text
custom model loads
   ->
AFE recognizes "Hey Elli"
   ->
wake event fires once
   ->
KIRA enters LISTENING
   ->
existing VAD / STT / routing continues
```

After that becomes reliable, additional phrases can be considered.

## Future Phrase Set

- Hey Elli
- Hi Elli
- Hey Cutie
- Yoo Elli
- Hi Babes

## Testing Plan

The custom wake word should be tested for:

- normal speech
- quiet speech
- louder speech
- different distances
- fan/background noise
- false triggers
- repeated wake cycles
- waking after TTS playback

## Important

Wake-word recognition is intended to remain completely local on the ESP32-S3. No cloud connection should be required just to wake KIRA.
