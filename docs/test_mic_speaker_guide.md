# USB Audio Device Testing Guide (Ubuntu Native Tools)

Device in `audio.yaml`: `plughw:CARD=Device,DEV=0` (USB PnP Sound Device)

## 1. Confirm Device is Recognized

```bash
# List all recording devices
arecord -l

# List all playback devices
aplay -l
```

Find the line `card X: Device [USB PnP Sound Device]` and confirm the card number.

## 2. Test Microphone (Record and Playback)

```bash
# Record 5 seconds, then play back
arecord -D "plughw:CARD=Device,DEV=0" -f S16_LE -r 16000 -d 5 test_mic.wav
aplay -D "plughw:CARD=Device,DEV=0" test_mic.wav
```

## 3. Real-time Loopback (Mic → Speaker)

```bash
# Record and play simultaneously — if you hear yourself, both paths work
arecord -D "plughw:CARD=Device,DEV=0" -f S16_LE -r 16000 | aplay -D "plughw:CARD=Device,DEV=0" -f S16_LE -r 16000
```

Press `Ctrl+C` to stop.

## 4. Adjust Volume (If No Sound)

```bash
# Interactive mixer
alsamixer

# Or set mic gain to 80%
amixer -c Device set Mic 80%
```

In `alsamixer`, press `F4` to switch to capture view, make sure the mic is not muted (`MM` -> `OO`).

> **Tip**: `plughw` handles sample-rate conversion automatically, so 16kHz recording works fine. If using the `hw:` prefix, the sample rate must match the device's native rate.
