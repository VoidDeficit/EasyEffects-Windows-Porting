# EasyEffects Windows Quick Start

Welcome to EasyEffects Windows! This is a native Windows audio processor inspired by the original EasyEffects for Linux.

## Getting Started

### 1. Launch the App
Run `EasyEffects.exe` from this folder.

### 2. Configure Devices
1. In the top-left menu, click **Options → Audio Settings...**
2. Set your **Input Device** (e.g., your microphone)
3. Set your **Output Device** (e.g., your headphones)
4. Audio should now pass through the processor!

### 3. Add Effects
Use the left sidebar to add effects like EQ, Compressor, or Noise Reduction.

---

## Virtual Audio (system audio + microphone)

1. Install [Synchronous Audio Router](https://github.com/eiz/SynchronousAudioRouter/releases) once (free). No setup inside it is needed.
2. In EasyEffects, open the **Devices** page and turn on:
   - **Process system audio**: all Windows audio goes through the **Output** tab to your speakers or headphones.
   - **Process microphone**: your mic goes through the **Input** tab to **EasyEffects Microphone**. Pick it in Discord, Zoom or OBS (it also becomes the Windows default input).
3. Allow the **Restart as administrator** prompt. SAR only creates Windows devices for an administrator host.
4. Turning an option off, or closing EasyEffects, restores your normal Windows devices.

---

## Custom ASIO routing (Windows ⇄ ASIO, both directions)

EasyEffects can host an ASIO driver (e.g. Synchronous Audio Router or your own
custom driver) and mix Windows Audio with ASIO channel pairs:

- **Mic path:** Windows microphone → effects → ASIO output pair (virtual mic).
- **Playback path:** ASIO input pair → effects → Windows speakers/headphones.

1. Open **Devices → Audio engine** and choose the ASIO driver.
2. **Input** tab → **Input device** → **Windows Audio microphone** → your mic.
3. **Input** tab → **Recorders** → the ASIO output pair used as the virtual
   microphone (select it in Discord, OBS or Zoom).
4. **Output** tab → **Players** → the ASIO input pair that carries the audio to
   process (e.g. a SAR "Players" endpoint).
5. **Output** tab → **Output device** → **Windows Audio output device** → your
   real speakers or headphones.

Resampling and FIFOs keep the Windows Audio devices and the ASIO driver (which
run on separate clocks) in sync.

---


## Legal & Licensing

- **License:** GNU General Public License v3.0 (GPLv3)
- **External Libraries:** This software uses RNNoise (BSD) and SoundTouch (LGPL, dynamically linked or otherwise documented in source).
- See the `LICENSE` file for full details.
