# EasyEffects Windows Porting

A native Windows audio effects processor inspired by [EasyEffects](https://github.com/wwmm/easyeffects) for Linux.

![EasyEffects Windows Screenshot](screenshots/MainApp.png)

## Tutorial Video

https://github.com/user-attachments/assets/d775c197-f497-41ad-902c-9434b1a11909

## What this project IS
A real-time, modular DSP audio processing chain for Windows, built with C++ and JUCE. It provides 25+ professional audio effects, spectrum analysis, and flexible routing.

## What it is NOT
**This is NOT system-wide by default.** Unlike the Linux version, which uses PipeWire to take over all system audio natively, Windows does not make this easy.
**For system-wide audio or mic processing, install [Synchronous Audio Router](https://github.com/eiz/SynchronousAudioRouter/releases) (SAR, free).** EasyEffects then creates its own Windows devices, **EasyEffects Speakers** and **EasyEffects Microphone**, and routes them for you. No virtual cable is needed.

## Current Status: Alpha / Under Development
**Please note:** This project is currently in the **Alpha** stage and is under active development. While the core architecture and many effects are stable, you may encounter bugs, UI glitches, or unoptimized CPU usage. Some specific audio effects might not yet work perfectly or exactly match their Linux counterparts. We appreciate your patience and bug reports!

---

## Feature Highlights

- **Real-time DSP Chain:** Dynamically add, remove, and reorder effects with zero audio stuttering.
- **25+ Professional Effects:** Including 10-Band Parametric EQ, Compressor, RNNoise (AI Noise Reduction), Pitch Shifter, Exciter, Limiter, and more.
- **Advanced Analytics:** Real-time 2048-point lock-free FFT Spectrum Analyzer and dynamic I/O metering.
- **Preset System:** Save and load full chain configurations or per-module presets (JSON based).
- **Standalone & VST3:** Run as a standalone desktop app or load it as a VST3 plugin in OBS Studio or your favorite DAW.

---

## Download & Installation

1. Go to the [Releases](../../releases) page.
2. Download the latest `EasyEffects-Windows-vX.X.X.zip`.
3. Extract the folder.
4. Run `EasyEffects.exe`.

---

## Virtual Audio with Synchronous Audio Router (recommended)

EasyEffects hosts [Synchronous Audio Router](https://github.com/eiz/SynchronousAudioRouter/releases)
(SAR) through ASIO and uses it to create its own Windows devices:

| Windows device | Type | Route |
|---|---|---|
| **EasyEffects Speakers** | Playback | apps → **Output** tab effects → your speakers/headphones |
| **EasyEffects Microphone** | Recording | your mic → **Input** tab effects → Discord / OBS / Zoom |

1. Install SAR once (it is signed and works with Secure Boot on). You do **not** need to configure it.
2. In EasyEffects, open the **Devices** page and turn on **Process system audio** and/or **Process microphone**. EasyEffects then:
   - writes SAR's configuration (your audio interface's ASIO driver as the hardware, plus the two EasyEffects devices);
   - starts SAR and wires both tabs to the new devices;
   - makes **EasyEffects Speakers** / **EasyEffects Microphone** the Windows default devices.
3. SAR only creates Windows devices for a host running **as administrator**, so EasyEffects offers to restart itself that way.
4. Turning an option off, choosing **Turn virtual audio off** (Devices page), or closing EasyEffects restores your previous Windows default devices. The devices exist only while EasyEffects is running.

Your audio interface needs an ASIO driver (most USB interfaces and mixers ship one, e.g. the FLOW 8's *FLOW ASIO Driver*).

---

## Custom ASIO routing (Windows ⇄ ASIO, both directions)

EasyEffects can host an ASIO driver (Synchronous Audio Router, FLOW ASIO, your
own driver, ...) and use Windows Audio for the other end of each pipeline:

| Pipeline | Input | Output | Typical use |
|---|---|---|---|
| **Input** (mic) | Windows Audio microphone | ASIO output pair | Windows mic → effects → virtual mic in your ASIO driver |
| **Output** (system) | ASIO input pair | Windows Audio output device | Apps/ASIO players → effects → real speakers/headphones |

1. **Devices** → **Audio engine** → pick your ASIO driver. (ASIO is single-client, so
   EasyEffects must be the only host using it.)
2. **Input** tab → **Input device** → **Windows Audio microphone** → your real
   microphone. It is read through WASAPI, so it does not have to appear in the
   ASIO driver's input channels.
3. **Input** tab → **Recorders** → the ASIO output pair that your driver exposes
   as its virtual microphone. Select that device in Discord/OBS/Zoom.
4. **Output** tab → **Players** → the ASIO input pair that carries the audio you
   want to process (e.g. a Synchronous Audio Router "Players" endpoint).
5. **Output** tab → **Output device** → **Windows Audio output device** → your
   speakers or headphones.

Both Windows Audio devices run on the Windows clock while the ASIO driver runs
on its own: EasyEffects resamples when the rates differ and FIFOs absorb clock
drift, so the two sides stay in sync without growing latency.

---

## VST3 Installation (For OBS / DAWs)

1. From the release zip, copy the `EasyEffects.vst3` folder.
2. Paste it into your system's VST3 directory:
   `C:\Program Files\Common Files\VST3\`
3. Restart OBS Studio or your DAW, and EasyEffects will appear in your plugins list.

---

## Build from Source

You will need:
- Windows 10/11
- Visual Studio 2022 (or newer) with C++ Desktop Development tools
- CMake (3.24+)
- Git

### 1. Clone the repository and submodules
```bash
git clone https://github.com/YOUR_USERNAME/easyeffects-windows.git
cd easyeffects-windows
git submodule update --init --recursive
```

### 2. Generate and Build
```bash
cmake -B build -G "Visual Studio 17 2022" -A x64 "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
cmake --build build --config Release
```

### 3. Run or Package
You can run the compiled standalone executable directly:
`build/EasyEffects_artefacts/Release/Standalone/EasyEffects.exe`

Alternatively, you can use the provided PowerShell script to package the release:
```powershell
# Create a folder release
powershell -ExecutionPolicy Bypass -File scripts\package_release.ps1 -NoZip

# Or create a Zip archive
powershell -ExecutionPolicy Bypass -File scripts\package_release.ps1
```
The output will be placed in the `releases/` directory.

---

## License & Compliance

This project is licensed under the **GNU General Public License v3.0 (GPLv3)**. See the `LICENSE` file for details.

**External Libraries:**
- [JUCE Framework](https://juce.com/) (GPLv3 for open source)
- [RNNoise](https://github.com/xiph/rnnoise) (BSD 3-Clause)
- [SoundTouch](https://codeberg.org/soundtouch/soundtouch) (LGPL) - Dynamically linked where applicable or utilized under LGPL compliance terms.
- Steinberg ASIO interface headers (bundled with JUCE), used under GPLv3 (Steinberg dual-licenses the ASIO SDK under its proprietary licence or GPLv3).
- [Synchronous Audio Router](https://github.com/eiz/SynchronousAudioRouter) is installed separately by the user; EasyEffects only hosts it through ASIO.

---

## AI-Assisted Development

**Full disclosure:** The vast majority of the C++ architecture, JUCE implementation, and DSP porting in this repository was heavily assisted and generated by AI (specifically Google's Gemini / Claude) acting as a pair-programmer. It was guided and architected by a human, but the code itself was largely AI-written.

---

*Disclaimer: This project is an independent Windows port and is not directly affiliated with the original EasyEffects Linux developers.*
