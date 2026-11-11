/*
 * EasyEffects Virtual Audio Driver (EEVAD)
 *
 * Shared definitions for the PortCls / WaveRT virtual audio driver that exposes
 * two loopback "cables" so EasyEffects no longer depends on third-party
 * virtual audio cables:
 *
 *   Loop 0 (system audio):  [render]  "EasyEffects Speakers"
 *                       ->  [capture] "EasyEffects Speakers Monitor"   (read by the app)
 *
 *   Loop 1 (microphone):    [render]  "EasyEffects Mic Feed"           (written by the app)
 *                       ->  [capture] "EasyEffects Microphone"         (used by Discord/OBS/...)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

extern "C" {
#include <wdm.h>
}

#include <portcls.h>
#include <stdunk.h>
#include <ksmedia.h>
#include <mmreg.h>

#define EEVAD_POOLTAG               'DAVE'

// Fixed stream format exposed by every endpoint.
#define EEVAD_SAMPLE_RATE           48000
#define EEVAD_CHANNELS              2

// Loopback ring: 2^16 frames (~1.36 s at 48 kHz), stereo float.
#define EEVAD_RING_FRAMES           65536
#define EEVAD_RING_MASK             (EEVAD_RING_FRAMES - 1)

// Capture side keeps roughly this much latency behind the render side and
// re-syncs if it drifts further than EEVAD_MAX_LAG_FRAMES.
#define EEVAD_TARGET_LAG_FRAMES     480     // 10 ms
#define EEVAD_MAX_LAG_FRAMES        4800    // 100 ms

// Engine tick (100 ns units). 1 ms high-resolution timer.
#define EEVAD_TIMER_PERIOD_100NS    10000

#define EEVAD_LOOP_COUNT            2
#define EEVAD_ENDPOINT_COUNT        4
#define EEVAD_MAX_STREAMS           8

enum EEVAD_SAMPLE_KIND
{
    EevadSampleUnsupported = 0,
    EevadSamplePcm16,
    EevadSampleFloat32,
};

struct EEVAD_ENDPOINT_CONFIG
{
    const WCHAR*    WaveName;       // must match KSNAME_WaveN in the INF
    const WCHAR*    TopoName;       // must match KSNAME_TopoN in the INF
    BOOLEAN         IsCapture;
    ULONG           LoopIndex;
    const GUID*     JackCategory;   // KSNODETYPE_* of the external bridge pin
    const GUID*     JackName;       // MediaCategories name GUID (see INF)
};

// Name GUIDs referenced by the INF (HKLM\...\MediaCategories\{GUID}\Name).
// Windows uses these to label the endpoints in Sound settings.
// {E3A1C0D1-5A1E-4E0B-9E51-7A11EEFA0001}
EXTERN_C const GUID DECLSPEC_SELECTANY EEVAD_NAME_SPEAKERS =
{ 0xe3a1c0d1, 0x5a1e, 0x4e0b, { 0x9e, 0x51, 0x7a, 0x11, 0xee, 0xfa, 0x00, 0x01 } };
// {E3A1C0D1-5A1E-4E0B-9E51-7A11EEFA0002}
EXTERN_C const GUID DECLSPEC_SELECTANY EEVAD_NAME_SPEAKERS_MONITOR =
{ 0xe3a1c0d1, 0x5a1e, 0x4e0b, { 0x9e, 0x51, 0x7a, 0x11, 0xee, 0xfa, 0x00, 0x02 } };
// {E3A1C0D1-5A1E-4E0B-9E51-7A11EEFA0003}
EXTERN_C const GUID DECLSPEC_SELECTANY EEVAD_NAME_MIC_FEED =
{ 0xe3a1c0d1, 0x5a1e, 0x4e0b, { 0x9e, 0x51, 0x7a, 0x11, 0xee, 0xfa, 0x00, 0x03 } };
// {E3A1C0D1-5A1E-4E0B-9E51-7A11EEFA0004}
EXTERN_C const GUID DECLSPEC_SELECTANY EEVAD_NAME_MICROPHONE =
{ 0xe3a1c0d1, 0x5a1e, 0x4e0b, { 0x9e, 0x51, 0x7a, 0x11, 0xee, 0xfa, 0x00, 0x04 } };

const EEVAD_ENDPOINT_CONFIG* EevadGetEndpoint(ULONG Index);

// Validates a KSDATAFORMAT against what the driver supports (48 kHz stereo,
// 16-bit PCM or 32-bit float, WAVEFORMATEX or WAVEFORMATEXTENSIBLE).
EEVAD_SAMPLE_KIND EevadClassifyFormat(_In_ PKSDATAFORMAT Format, _In_ ULONG FormatSize);

// Pool helpers (kernel C++ has no default operator new).
// (plain operator delete(void*) comes from stdunk.lib)
PVOID operator new(size_t Size, POOL_FLAGS Flags, ULONG Tag);
void __cdecl operator delete(PVOID Ptr, size_t Size);
void __cdecl operator delete[](PVOID Ptr);

// Miniport factories (minwavert.cpp / mintopo.cpp)
NTSTATUS CreateEevadMiniportWaveRT(_Out_ PUNKNOWN* Unknown, _In_ ULONG EndpointIndex);
NTSTATUS CreateEevadMiniportTopology(_Out_ PUNKNOWN* Unknown, _In_ ULONG EndpointIndex);

// Pin numbering shared by adapter.cpp (physical connections) and the miniports.
//   Render wave:  0 = streaming sink (IN),  1 = bridge (OUT)  -> topo pin 0
//   Render topo:  0 = bridge from wave (IN), 1 = jack (OUT)
//   Capture topo: 0 = jack (IN),             1 = bridge to wave (OUT) -> wave pin 0
//   Capture wave: 0 = bridge (IN),           1 = streaming source (OUT)
#define EEVAD_PIN_RENDER_WAVE_STREAM    0
#define EEVAD_PIN_RENDER_WAVE_BRIDGE    1
#define EEVAD_PIN_RENDER_TOPO_BRIDGE    0
#define EEVAD_PIN_RENDER_TOPO_JACK      1
#define EEVAD_PIN_CAPTURE_TOPO_JACK     0
#define EEVAD_PIN_CAPTURE_TOPO_BRIDGE   1
#define EEVAD_PIN_CAPTURE_WAVE_BRIDGE   0
#define EEVAD_PIN_CAPTURE_WAVE_STREAM   1
