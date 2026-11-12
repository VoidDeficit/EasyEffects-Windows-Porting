# EasyEffects for Windows: Quick Start

EasyEffects runs a chain of audio effects on what your apps play (**Output**) and on your
microphone (**Input**), like EasyEffects on Linux.

## 1. Start EasyEffects

Run `EasyEffects.exe`. It also puts an icon in the notification area (system tray): closing or
minimizing the window keeps it running with your effects on. Right-click the tray icon to quit.

## 2. Choose your devices

Open **Devices** in the navigation pane (bottom left):

- **Output > Players / Output device:** where the audio you want to process comes from, and your
  speakers or headphones.
- **Input > Input device / Recorders:** your microphone, and where the processed microphone goes.

The same choices are at the top and bottom of the **Effects** list on the **Output** and **Input** pages.

## 3. Add effects

On the **Output** or **Input** page, click **Add** and pick an effect. Each effect has an on/off
switch, a menu (reset to defaults, move, remove) and a drag handle to reorder it.

---

## Process all system audio and your microphone (Virtual Audio)

1. Install [Synchronous Audio Router](https://github.com/eiz/SynchronousAudioRouter/releases) once (free).
   No setup inside it is needed.
2. In EasyEffects, open **Devices** and turn on:
   - **Process system audio:** all Windows audio goes through the **Output** effects to your speakers or headphones.
   - **Process microphone:** your mic goes through the **Input** effects to **EasyEffects Microphone**.
     Pick it in Discord, Zoom or OBS (it also becomes the Windows default input).
3. SAR only creates its Windows devices when EasyEffects runs as administrator. The installed version
   does that automatically; the portable version asks to restart as administrator.
4. Turning an option off, or quitting EasyEffects, restores your normal Windows devices.

---

## Custom ASIO routing

EasyEffects can host any ASIO driver and combine it with Windows Audio:

- **Mic path:** Windows microphone → Input effects → ASIO output pair (virtual mic).
- **Playback path:** ASIO input pair → Output effects → Windows speakers or headphones.

1. **Devices > Audio engine:** choose the ASIO driver.
2. **Devices > Input device:** your microphone (under **Windows Audio microphone**).
3. **Devices > Recorders:** the ASIO output pair used as the virtual microphone.
4. **Devices > Players:** the ASIO input pair that carries the audio to process.
5. **Devices > Output device:** your speakers or headphones (under **Windows Audio output device**).

EasyEffects keeps the Windows Audio devices and the ASIO driver (separate clocks) in sync with a
small, constant delay.

---

## License

- GNU General Public License v3.0 (GPLv3), see `LICENSE`.
- Uses JUCE (GPLv3), RNNoise (BSD) and SoundTouch (LGPL).
