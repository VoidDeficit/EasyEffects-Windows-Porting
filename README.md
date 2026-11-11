# EasyEffects Windows Porting

A native Windows audio effects processor inspired by [EasyEffects](https://github.com/wwmm/easyeffects) for Linux.

![EasyEffects Windows Screenshot](screenshots/MainApp.png)

## Tutorial Video

https://github.com/user-attachments/assets/d775c197-f497-41ad-902c-9434b1a11909

## What this project IS
A real-time, modular DSP audio processing chain for Windows, built with C++ and JUCE. It provides 25+ professional audio effects, spectrum analysis, and flexible routing.

## What it is NOT
**This is NOT system-wide by default.** Unlike the Linux version, which uses PipeWire to take over all system audio natively, Windows does not make this easy.
**For system-wide audio or mic processing, install the bundled EasyEffects Virtual Audio driver** (`driver/`). It adds its own virtual speaker and microphone devices, so you don't need a third-party cable. VB-Cable still works if you prefer it.

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

## Built-in Virtual Audio (recommended)

EasyEffects ships its own virtual audio driver with two loopback "cables":

| Playback device | Recording device | Purpose |
|---|---|---|
| **EasyEffects Speakers** | EasyEffects Speakers Monitor | System audio into EasyEffects |
| EasyEffects Mic Feed | **EasyEffects Microphone** | Processed voice out to Discord/OBS/Zoom |

1. Install the driver once: run **`EEVirtualAudioSetup.exe`** (next to `EasyEffects.exe`) or use **Virtual Audio → Install virtual audio driver...**. Release builds use a Microsoft-signed driver and work with Secure Boot on. Self-built development drivers need test signing mode; see [driver/README.md](driver/README.md#secure-boot).
2. In EasyEffects, click **Virtual Audio** (top right) and choose:
   - **Process system audio**: Windows output is routed through EasyEffects to your real speakers or headphones. Pick them in the same menu.
   - **Process microphone**: your mic is processed and appears as **EasyEffects Microphone**, which is also made the Windows default input.
3. Choosing **Off**, or closing the app, restores your previous Windows default devices.

The manual VB-Cable setups below still work.

---

## Custom ASIO routing (Windows ⇄ ASIO, both directions)

EasyEffects can host an ASIO driver (Synchronous Audio Router, FLOW ASIO, your
own driver, ...) and use Windows Audio for the other end of each pipeline:

| Pipeline | Input | Output | Typical use |
|---|---|---|---|
| **Input** (mic) | Windows Audio microphone | ASIO output pair | Windows mic → effects → virtual mic in your ASIO driver |
| **Output** (system) | ASIO input pair | Windows Audio output device | Apps/ASIO players → effects → real speakers/headphones |

1. → **Audio engine** → pick your ASIO driver. (ASIO is single-client, so
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

## Microphone Audio Setup with VB-Cable (Voice Processing)

To process your microphone for Discord, Zoom, or OBS:

1. Download and install a free Virtual Audio Cable like [VB-Cable](https://vb-audio.com/Cable/).
2. Open EasyEffects Windows. Click **Options -> Audio Settings**.
3. Set **Input** to your physical microphone (e.g., *HyperX QuadCast* or *Focusrite USB*).
4. Set **Output** to **CABLE Input**.
5. In your chat application (Discord, Zoom, etc.), set your input device to **CABLE Output**.

Now your microphone is processed by EasyEffects before reaching your chat apps!

---

## System-Wide Audio Setup with VB-Cable

To process audio from games, Spotify, or your whole system:

1. Download and install a free Virtual Audio Cable like [VB-Cable](https://vb-audio.com/Cable/).
2. In Windows Sound Settings, set your Default Playback Device to **CABLE Input**.
3. Open EasyEffects Windows. Click **Options -> Audio Settings**.
4. Set **Input** to **CABLE Output**.
5. Set **Output** to your physical headphones/speakers.

Now, all system audio routes through EasyEffects before reaching your ears!

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

### 4. Virtual Audio Driver (optional)
The driver needs the Windows Driver Kit (WDK) in addition to Visual Studio:
```bash
msbuild driver\EEVirtualAudio.vcxproj /p:Configuration=Release /p:Platform=x64
```
To build the installer exe (driver included):
```bash
powershell -ExecutionPolicy Bypass -File scripts\build_driver_installer.ps1
```
`package_release.ps1` then puts `EEVirtualAudioSetup.exe` next to `EasyEffects.exe`. For a Secure Boot-compatible (Microsoft-signed) release, see [driver/README.md](driver/README.md#secure-boot).

---

## License & Compliance

This project is licensed under the **GNU General Public License v3.0 (GPLv3)**. See the `LICENSE` file for details.

**External Libraries:**
- [JUCE Framework](https://juce.com/) (GPLv3 for open source)
- [RNNoise](https://github.com/xiph/rnnoise) (BSD 3-Clause)
- [SoundTouch](https://codeberg.org/soundtouch/soundtouch) (LGPL) - Dynamically linked where applicable or utilized under LGPL compliance terms.

---

## AI-Assisted Development

**Full disclosure:** The vast majority of the C++ architecture, JUCE implementation, and DSP porting in this repository was heavily assisted and generated by AI (specifically Google's Gemini / Claude) acting as a pair-programmer. It was guided and architected by a human, but the code itself was largely AI-written.

---

*Disclaimer: This project is an independent Windows port and is not directly affiliated with the original EasyEffects Linux developers.*
