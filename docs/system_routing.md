# Windows System-wide Audio Routing Guide

Because JUCE does not directly interact with the low-level Windows Audio Processing Object (APO) architecture, EasyEffects Windows operates as a Standalone Audio Host. 

To apply these effects to your system-wide audio (YouTube, Spotify, games), audio has to be looped through a virtual device.

> **Recommended:** use the bundled **EasyEffects Virtual Audio** driver (`driver/`, see [driver/README.md](../driver/README.md)). After installing it, the app's **Virtual Audio** button does every step below automatically, using *EasyEffects Speakers* / *EasyEffects Speakers Monitor* for system audio and *EasyEffects Mic Feed* / *EasyEffects Microphone* for voice. It restores your Windows defaults when you turn it off.
>
> The rest of this guide describes the manual **Virtual Audio Cable (VAC)** workflow with third-party cables.

## Overview of the Pipeline

1. **System Output** is sent to a *Virtual Cable Input*.
2. **EasyEffects App** listens to the *Virtual Cable Output*.
3. **EasyEffects App** processes the audio through the DSP chain.
4. **EasyEffects App** pushes the processed audio to your physical *Headphones/Speakers*.

---

## Step-by-Step Setup

### Step 1: Install a Virtual Audio Cable
You need a kernel-level driver that creates a virtual loopback device. The two best options are:
- [VB-Audio Virtual Cable (Free)](https://vb-audio.com/Cable/)
- [Virtual Audio Cable (Paid/Trial)](https://vac.muzychenko.net/en/)

Download and install your chosen cable, and then explicitly **Restart your PC**.

### Step 2: Configure Windows Audio
1. Open the Windows **Settings** app -> **System** -> **Sound**.
2. Under **Output**, set your default playback device to **CABLE Input (VB-Audio Virtual Cable)**.
   *Note: At this point, you will not hear any audio on your system because it is being piped into the void.*

### Step 3: Configure EasyEffects Windows
1. Open the **EasyEffects Standalone** application.
2. In the top OS menubar of the app, click **Options -> Audio/MIDI Settings**.
3. Configure the settings exactly as follows:
   * **Audio Device Type:** `Windows Audio`
   * **Input Device:** `CABLE Output (VB-Audio Virtual Cable)`
   * **Output Device:** `Your Real Physical Headphones / Speakers (e.g., Speakers K75)`
   * **Sample Rate:** `48000 Hz` (Recommended to match standard Windows format)
   * **Audio Buffer Size:** `512` samples (Lower means less latency, but higher CPU usage).
   * **Feedback Loop:** Uncheck "Mute audio input" so audio flows through.

### Step 4: Verify the DSP Chain
1. Play some audio from a web browser or Spotify.
2. Open EasyEffects and check the **Level Meter** in the footer. You should see the graphical meters moving in real-time.
3. Turn on the **Equalizer** or **Limiter** and make extreme changes to verify that the core audio pipeline is successfully routing into your headphones.

---

## Discord / Voice Chat Routing (Microphone Processing)

For processing your **microphone** audio before sending it to Discord, the routing is different:

### Signal Chain
```
Physical Mic (Fifine) → EasyEffects → VB-Audio Virtual Cable → Discord
```

### EasyEffects Audio/MIDI Settings
| Setting | Value |
|---------|-------|
| Audio Device Type | Windows Audio |
| **Input** | **Microphone (Fifine Microphone)** |
| **Output** | **CABLE Input (VB-Audio Virtual Cable)** |
| Feedback Loop | **Uncheck** "Mute audio input" |
| Sample Rate | 48000 Hz |
| Audio Buffer Size | 512 samples (10.7 ms) |

### Discord Settings
| Setting | Value |
|---------|-------|
| **Input Device** | **CABLE Output (VB-Audio Virtual Cable)** |
| **Output Device** | **Speakers (K75) or your headphones** |

### Recommended Voice Effects Settings

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

## ASIO ⇄ Windows Audio (Synchronous Audio Router, custom ASIO drivers)

EasyEffects can host the ASIO driver itself, and each side of a pipeline may use
Windows Audio instead of an ASIO channel pair:

| Pipeline | Input | Output |
|---|---|---|
| **Input** (mic) | Windows Audio microphone (WASAPI) | ASIO output pair |
| **Output** (system) | ASIO input pair | Windows Audio output device (WASAPI) |

Setup for "mic → custom ASIO" **and** "ASIO → speakers":

1. Install the ASIO driver (e.g. Synchronous Audio Router on top of your
   interface's ASIO driver). Run EasyEffects as administrator if the driver only
   creates its Windows devices for elevated hosts (SAR does).
2. → **Audio engine** → select the driver.
3. **Input** tab → **Input device** → **Windows Audio microphone** → your real
   microphone. It is captured through WASAPI, so Windows-only microphones work
   even when the ASIO driver does not list them.
4. **Input** tab → **Recorders** → the ASIO output pair that is your virtual
   microphone. Pick that pair as the microphone in Discord/OBS/Zoom.
5. **Output** tab → **Players** → the ASIO input pair carrying the audio to
   process (e.g. a SAR "Players" endpoint).
6. **Output** tab → **Output device** → **Windows Audio output device** → your
   real speakers/headphones.

The WASAPI devices and the ASIO driver have independent clocks, so EasyEffects
resamples if the rates differ and buffers through FIFOs (dropping old audio or
inserting silence on drift) to keep latency stable.

---

## Latency Optimization Tips
- **Buffer Size:** If you experience "crackling" or "pops", increase the buffer size in EasyEffects (e.g. 512 -> 1024). If you experience visual lag while watching videos, *decrease* the buffer size (e.g. 512 -> 128).
- **Match Sample Rates:** Ensure that the Windows Sound Control Panel properties for both your *Physical Headphones* and the *Virtual Cable* are perfectly matched (e.g., exactly `24-bit, 48000 Hz`). Mixed sample rates force Windows into heavy resampling, destroying latency.
