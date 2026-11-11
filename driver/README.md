# EasyEffects Virtual Audio Driver

A small kernel-mode audio driver (PortCls / WaveRT) that gives EasyEffects its own
virtual devices, so you no longer need VB-Cable or another third-party cable.

It creates **two loopback cables**. Whatever is played into the playback side of a
cable comes out of its recording side:

| Cable | Playback endpoint (render) | Recording endpoint (capture) | Used for |
|-------|---------------------------|------------------------------|----------|
| System | **EasyEffects Speakers** | EasyEffects Speakers Monitor | Windows/apps play here → EasyEffects reads the monitor |
| Mic | EasyEffects Mic Feed | **EasyEffects Microphone** | EasyEffects writes processed voice → Discord/OBS/Zoom record it |

All endpoints run at **48 kHz, stereo**, 16-bit PCM or 32-bit float.

The standalone app's **Virtual Audio** button (top right) does the routing for you:

- **Process system audio:** sets the Windows default output to *EasyEffects Speakers*.
  The app reads *EasyEffects Speakers Monitor* and plays the result on your real
  speakers or headphones.
- **Process microphone:** the app reads your real microphone and plays the result
  into *EasyEffects Mic Feed*. It also sets the Windows default input to
  *EasyEffects Microphone*.
- **Off / closing the app:** your previous Windows default devices are restored.

## How it works

```
 apps ──► EasyEffects Speakers ─┐                        ┌─► your speakers
          (WaveRT render)       │ loop 0 ring (float)    │
                                └─► Speakers Monitor ──► EasyEffects DSP
                                    (WaveRT capture)

 your mic ──► EasyEffects DSP ──► Mic Feed ─┐
                                 (render)   │ loop 1 ring
                                            └─► EasyEffects Microphone ──► Discord / OBS
                                                (capture)
```

- `src/adapter.cpp`: DriverEntry/AddDevice/StartDevice. Registers 4 WaveRT and 4
  topology subdevices and their physical connections.
- `src/minwavert.cpp`: the WaveRT miniport and stream. Allocates the cyclic DMA buffer,
  reports the position and converts between PCM16/float and the ring.
- `src/mintopo.cpp`: a minimal topology filter per endpoint. The jack pin's
  category and name GUID give Windows the endpoint name.
- `src/loopback.cpp`: a 1 ms high-resolution timer that moves every running stream
  forward from the performance counter. Render streams write to the loop ring, and
  capture streams read from it with about 10 ms of slack.
- `EEVirtualAudio.inf`: device install, KS interfaces and endpoint names.

## Build

Requirements: Visual Studio 2022 with *Desktop development with C++* and the
**Windows Driver Kit (WDK)** for Windows 11 (including the VS extension).

```powershell
msbuild driver\EEVirtualAudio.vcxproj /p:Configuration=Release /p:Platform=x64
```

The output goes to `driver\build\x64\Release\`: `EEVirtualAudio.sys`, the stamped
`.inf`, the `.cat` catalog and the WDK test certificate `EEVirtualAudio.cer`.

## Installer: `EEVirtualAudioSetup.exe`

`installer/` builds a single self-contained exe with the driver package built in.
It asks for admin rights through UAC and then installs, updates or removes the
driver. No WDK or `devcon` is needed on the target PC.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_driver_installer.ps1           # development (test-signed)
powershell -ExecutionPolicy Bypass -File scripts\build_driver_installer.ps1 -Release  # Microsoft-signed package
```

Output: `build-installer\Release\EEVirtualAudioSetup.exe`. `package_release.ps1`
copies it next to `EasyEffects.exe`, and the app's **Virtual Audio** menu then shows
**Install / Uninstall virtual audio driver...** entries.

| Command | Effect |
|---|---|
| `EEVirtualAudioSetup.exe` | install or update |
| `EEVirtualAudioSetup.exe /uninstall` | remove the device and the driver package |
| `EEVirtualAudioSetup.exe /status` | show build type, Secure Boot, test signing and device state |
| `/quiet` | don't wait for Enter at the end |
| `/force` | install a test-signed build even if Windows won't load it |

Exit codes: `0` = OK, `3010` = OK but a restart is needed, `1` = failed, `2` = refused
(test-signed build on a PC that can't load it).

## Secure Boot

With Secure Boot on, Windows loads a kernel driver **only if Microsoft signed it**.
No code or installer change can get around that, so there are two kinds of build:

| Build | Signed by | Works with Secure Boot ON |
|---|---|---|
| Development (`build_driver_installer.ps1`) | WDK test certificate | No. Needs Secure Boot off and `bcdedit /set testsigning on` |
| Release (`build_driver_installer.ps1 -Release`) | Microsoft (attestation) | **Yes**, on any Windows 10/11 x64 PC |

### Producing a Secure Boot release (attestation signing)

1. **One-time setup.** Buy an **EV code-signing certificate** (from DigiCert, Sectigo,
   GlobalSign, etc.). Then register a company account in the **Windows Hardware Developer
   Program** (Partner Center), which is free once you have the EV certificate.
2. **Make the submission CAB.** This builds the driver, packs the `.inf`, `.sys` and
   `.pdb`, and signs the CAB with your EV certificate:
   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts\sign_driver_attestation.ps1 -CertThumbprint <EV cert SHA1 thumbprint>
   ```
3. **Submit.** In Partner Center go to *Hardware → Submit new hardware*, upload
   `driver\attestation\EEVirtualAudio.cab`, and select the Windows 10/11 x64
   attestation targets.
4. **Download** the signed package when it's done (usually minutes) and extract it
   into `driver\signed\`.
5. **Build the release installer:**
   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts\build_driver_installer.ps1 -Release
   ```
   This `EEVirtualAudioSetup.exe` contains no test certificate. It installs and loads
   on normal PCs with Secure Boot on, and without test mode.

Attestation-signed drivers load on retail Windows 10/11 client editions. Windows
Server needs full WHQL (HLK) certification instead.

### Development on a Secure Boot PC

To test your own unsigned changes, either sign them through attestation as above or
use a separate test machine or VM with Secure Boot off and test signing on.

## Loading a self-signed driver with Secure Boot on (CustomKernelSigners / SSDE)

Windows has a built-in feature, **CustomKernelSigners (CKS)**, that lets *you*
authorize your *own* signing certificate for kernel drivers while Secure Boot stays
on — **if you own your machine's Secure Boot Platform Key (PK).** This is the
mechanism [SSDE](https://github.com/valinet/ssde) automates. It is free (you
self-sign, no purchased certificate), but it is involved and only works on a PC
whose platform key you control, so it is **for your own machine, not for
distribution** to others.

This repo does the signing half for you:

```powershell
scripts\sign_driver_selfsigned.cmd
```

This creates a self-signed code-signing certificate in your user store, signs
`EEVirtualAudio.sys` and its catalog with it, and builds a matching
`EEVirtualAudioSetup.exe` (which trusts that certificate on install).

The remaining steps change **security and firmware settings, so you must do them
yourself** — this project's scripts never touch them:

1. **Own the platform key.** In your UEFI/BIOS, put Secure Boot into *Setup Mode* /
   clear the factory keys and enroll your own PK (and KEK/db). Keep Secure Boot on.
   *This can stop other OSes or some pre-signed boot components from loading — be sure
   you can recover your firmware first.*
2. **Enable CustomKernelSigners.** Follow SSDE's instructions (its `EnableCKS` /
   `ssde_enable`, and the boot-start `ssde.sys` that keeps the policy active across
   reboots). Verify with its `ssde_query`.
3. **Install our driver.** Run the `EEVirtualAudioSetup.exe` produced above.

SSDE's own README warns the process is **"hit or miss"** and can **bootloop or
BSOD**, and it names only one tested Windows 11 build. Treat it as advanced,
at-your-own-risk tinkering on a machine you can afford to recover. The driver code
itself is identical — only who signed it, and whether Windows is told to trust that
signer, differ between this path and the Microsoft-signed release above.

### Which path should I use?

| Goal | Path |
|---|---|
| Just my own PC, willing to change BIOS keys, free | Self-signed + CKS (this section) |
| My own PC, quick, don't mind lowered protection | Test signing (`bcdedit /set testsigning on`, Secure Boot off) |
| Ship to other people, Secure Boot stays on for them | Microsoft attestation signing (previous section) — needs an EV certificate |

## Install (scripts, development)

The PowerShell scripts still work for development:

```powershell
powershell -ExecutionPolicy Bypass -File driver\install.ps1     # needs devcon.exe from the WDK
powershell -ExecutionPolicy Bypass -File driver\uninstall.ps1
```

## Limitations

- Fixed at 48 kHz stereo. Windows resamples other formats in its audio engine.
- No hardware volume or mute nodes, so Windows uses software endpoint volume.
- If nothing plays into a cable, its recording side delivers silence.
