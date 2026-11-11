# Windows System-wide Audio Routing Guide

Windows has no PipeWire-style way for an app to take over system audio, so
EasyEffects Windows runs as a standalone audio host and gets audio in and out
through virtual devices. Those devices come from
[Synchronous Audio Router](https://github.com/eiz/SynchronousAudioRouter/releases)
(SAR), a free, signed driver that works with Secure Boot on. EasyEffects hosts
SAR through ASIO and configures it automatically.

## Overview

| Windows device | Created by | Signal path |
|---|---|---|
| **EasyEffects Speakers** (playback) | SAR, configured by EasyEffects | apps → **Output** tab effects → your speakers/headphones |
| **EasyEffects Microphone** (recording) | SAR, configured by EasyEffects | your mic → **Input** tab effects → Discord / OBS / Zoom |

Both tabs run at the same time, through one ASIO driver.

---

## Setup

1. **Install SAR** from its releases page (the signed release). Nothing needs
   to be set up inside it.
2. **Make sure your audio interface has an ASIO driver** (e.g. the Behringer
   FLOW 8's *FLOW ASIO Driver*). SAR uses it as the real hardware and clock.
3. **Open EasyEffects → Devices** and, under **Virtual audio**, turn on
   **Process system audio** and/or **Process microphone**. EasyEffects:
   - writes `%APPDATA%\SynchronousAudioRouter\default.json`: the interface's
     ASIO driver as the hardware and the two EasyEffects devices (any endpoints
     you created yourself are kept; the original file is backed up once as
     `default.json.before-easyeffects`);
   - switches the audio engine to SAR (**Devices** → **Audio engine**);
   - wires the **Output** tab: EasyEffects Speakers → effects → the
     interface's first outputs; and the **Input** tab: the interface's first
     inputs → effects → EasyEffects Microphone;
   - makes EasyEffects Speakers / EasyEffects Microphone the Windows defaults.
4. **Allow "Restart as administrator".** SAR only creates its Windows devices
   for a host running as administrator.
5. In **Discord / OBS / Zoom**, choose **EasyEffects Microphone** as the input
   (it is also the Windows default while the option is on).

Turning an option off, choosing **Turn virtual audio off**, or closing
EasyEffects hands the Windows default devices back to the ones you used before.
The EasyEffects devices exist only while EasyEffects is running.

You can change any route afterwards with the sidebar rows (▾) on each tab. The
automatic wiring never overrides a route you picked yourself; use
**Virtual Audio → Connect both tabs to the EasyEffects devices** to reapply it.

---

## Recommended Voice Effects (Input tab)

| Effect | Recommended Settings | Purpose |
|--------|---------------------|---------|
| **Gate** | Threshold: -40 dB, Attack: 1 ms, Release: 100 ms | Cuts background noise when not speaking |
| **Compressor** | Threshold: -20 dB, Ratio: 4:1, Attack: 5 ms, Release: 50 ms | Evens out voice volume |
| **EQ** | Slight boost at 2-4 kHz bands | Adds clarity/presence to voice |
| **Deesser** | Threshold: -20 dB, Freq: 6000 Hz | Reduces harsh sibilant "S" sounds |
| **Limiter** | Threshold: -3 dB | Prevents clipping when speaking loudly |
| **Gain** | Adjust to taste (0 to +6 dB) | Final output level adjustment |

**Bypass (disable)** effects you don't need for voice: Exciter, Bass Enhancer, Convolver, Delay, Reverb.

---

## ASIO ⇄ Windows Audio (manual routing)

In ASIO mode each side of a pipeline may also use Windows Audio instead of an
ASIO channel pair:

| Pipeline | Input | Output |
|---|---|---|
| **Input** (mic) | Windows Audio microphone (WASAPI) | ASIO output pair |
| **Output** (system) | ASIO input pair | Windows Audio output device (WASAPI) |

- **Input** tab → **Input device** → **Windows Audio microphone**: for mics that
  your ASIO driver doesn't expose.
- **Output** tab → **Output device** → **Windows Audio output device**: to
  play the processed system audio on speakers/headphones outside the interface.

The WASAPI devices and the ASIO driver have independent clocks, so EasyEffects
resamples if the rates differ and buffers through FIFOs (dropping old audio or
inserting silence on drift) to keep latency stable.

---

## Latency Optimization Tips
- **Buffer size:** set it in the ASIO driver's panel (**Devices** → **Sample rate and buffer size** → **Open control panel**). If you hear crackling or pops, increase it; if video and audio drift apart, decrease it.
- **Match sample rates:** keep your interface and Windows' device formats at the same rate (e.g. 48000 Hz). Mismatched rates force extra resampling.
