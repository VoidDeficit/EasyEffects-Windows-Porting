/*
 * EEVAD loopback engine.
 *
 * A single high-resolution timer drives every running WaveRT stream. On each
 * tick the stream position is advanced from the performance counter; render
 * streams copy the frames that were just "played" into the loop's ring and
 * capture streams fill their DMA buffer from the same ring.
 *
 * All state below is protected by one spin lock (DISPATCH_LEVEL).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "common.h"

class EevadStreamBase
{
public:
    // Called with the engine lock held, at DISPATCH_LEVEL, with the
    // extended (FPU/SSE) processor state saved.
    virtual void PumpLocked(LONGLONG NowQpc) = 0;
    virtual BOOLEAN IsCaptureStream() const = 0;
};

struct EEVAD_LOOP
{
    float*      Ring;           // EEVAD_RING_FRAMES * EEVAD_CHANNELS
    LONGLONG    WritePos;       // total frames written by the render side
    ULONG       ActiveRenderers;
};

NTSTATUS    EevadEngineInitialize();
void        EevadEngineCleanup();       // PASSIVE_LEVEL, waits for the timer

// Register / unregister a stream that entered / left KSSTATE_RUN.
void        EevadEngineRegister(_In_ EevadStreamBase* Stream, _In_ ULONG LoopIndex);
void        EevadEngineUnregister(_In_ EevadStreamBase* Stream, _In_ ULONG LoopIndex);

// Brings every running stream up to "now" (used by GetPosition so the
// reported position is exact, not timer-granular).
void        EevadEnginePumpNow();

// Engine lock, for streams that need a consistent snapshot of their own state.
KIRQL       EevadEngineLock();
void        EevadEngineUnlock(_In_ KIRQL OldIrql);

// Only valid while the engine lock is held.
EEVAD_LOOP* EevadEngineLoopLocked(_In_ ULONG LoopIndex);
LONGLONG    EevadEngineQpcFrequency();
