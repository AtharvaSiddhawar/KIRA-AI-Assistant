# KIRA Development Roadmap

The KIRA Next roadmap is grouped into five implementation groups.

## Group A — Core Foundation

### Next 1 — Audio Foundation
- audio engine
- VAD foundation
- wake service
- queued audio handling

### Next 2 — Routing and State
- Router V2
- state bus
- session manager
- interrupt framework

### Next 3 — Tools and Diagnostics
- typed tools
- recovery framework
- diagnostics

Status: foundation stage.

---

## Group B — Streaming Backbone

### Next 4 — Protocol Core
- session protocol
- hello/listen/abort events
- STT/LLM/TTS event handling
- WebSocket-ready transport layer

### Next 5 — Opus Audio
- PCM to Opus encoding
- Opus to PCM decoding
- streaming frame queues

### Next 6 — Streaming STT
- microphone streaming
- endpoint handling
- long-question support
- preserve offline command routing

Target result:

```text
Mic -> AFE/VAD -> PCM -> Opus -> Transport -> STT -> KIRA Brain
```

---

## Group C — Natural Voice Interaction

### Next 7 — Streaming TTS V2
- queued network receive
- decode queue
- playback queue
- I2S buffering
- reduced truncation and crackling

### Next 8 — Full-Duplex Barge-In
- interrupt current speech
- flush old audio
- cancel old session
- return to listening cleanly

### Next 9 — Custom WakeNet
Initial target:
- Hey Elli

Future target phrases:
- Hey Elli
- Hi Elli
- Hey Cutie
- Yoo Elli
- Hi Babes

---

## Group D — Intelligence and Interaction

### Next 10 — Advanced AFE/VAD
- noise handling
- fan-noise tests
- silence timing
- microphone tuning

### Next 11 — Elli Reactive State Engine
- listening
- thinking
- speaking
- connection/error states
- emotions

### Next 12 — Device and Tool Bridge
- relays
- room automation
- timers
- routines
- future hardware tools

---

## Group E — Final Hybrid KIRA

### Next 13 — Offline/Online Hybrid V2
- local command priority
- online fallback
- network-loss degradation
- automatic recovery

### Next 14 — Reliability and Security
- Wi-Fi recovery
- provider failover
- watchdog protection
- queue protection
- TLS / credential handling
- memory monitoring

### Next 15 — Production Integration
- long-duration testing
- latency measurements
- PSRAM/heap stability
- repeated wake/listen/speak cycles
- network failure tests
- final optimization

## Final Goal

```text
Wake locally
   ->
Listen naturally
   ->
Route offline or online
   ->
KIRA Brain
   ->
Stream TTS
   ->
Speak smoothly
   ->
Allow interruption
   ->
Return to idle
```
