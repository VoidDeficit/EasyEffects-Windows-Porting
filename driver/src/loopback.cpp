/*
 * EEVAD loopback engine implementation. See loopback.h.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "loopback.h"

// x87 + SSE state (bits 0 and 1). Not exported by the kernel-mode headers.
#ifndef XSTATE_MASK_LEGACY
 #define XSTATE_MASK_LEGACY 0x3ULL
#endif

namespace
{
    struct EEVAD_ENGINE
    {
        KSPIN_LOCK          Lock;
        PEX_TIMER           Timer;
        BOOLEAN             TimerRunning;
        LONGLONG            QpcFrequency;

        EevadStreamBase*    Streams[EEVAD_MAX_STREAMS];
        ULONG               StreamLoop[EEVAD_MAX_STREAMS];
        ULONG               StreamCount;

        EEVAD_LOOP          Loops[EEVAD_LOOP_COUNT];
    };

    EEVAD_ENGINE* g_Engine = nullptr;

    // Engine lock must be held. Render streams first so capture streams see
    // the frames written during the same pump.
    void PumpAllLocked(LONGLONG nowQpc)
    {
        if (g_Engine->StreamCount == 0)
            return;

        XSTATE_SAVE xstate;
        if (!NT_SUCCESS(KeSaveExtendedProcessorState(XSTATE_MASK_LEGACY, &xstate)))
            return;

        for (ULONG i = 0; i < g_Engine->StreamCount; ++i)
            if (!g_Engine->Streams[i]->IsCaptureStream())
                g_Engine->Streams[i]->PumpLocked(nowQpc);

        for (ULONG i = 0; i < g_Engine->StreamCount; ++i)
            if (g_Engine->Streams[i]->IsCaptureStream())
                g_Engine->Streams[i]->PumpLocked(nowQpc);

        KeRestoreExtendedProcessorState(&xstate);
    }

    _Function_class_(EXT_CALLBACK)
    _IRQL_requires_max_(DISPATCH_LEVEL)
    VOID TimerCallback(_In_ PEX_TIMER Timer, _In_opt_ PVOID Context)
    {
        UNREFERENCED_PARAMETER(Timer);
        UNREFERENCED_PARAMETER(Context);

        if (!g_Engine)
            return;

        KIRQL old;
        KeAcquireSpinLock(&g_Engine->Lock, &old);
        PumpAllLocked(KeQueryPerformanceCounter(nullptr).QuadPart);
        KeReleaseSpinLock(&g_Engine->Lock, old);
    }
}

NTSTATUS EevadEngineInitialize()
{
    PAGED_CODE();

    g_Engine = (EEVAD_ENGINE*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(EEVAD_ENGINE), EEVAD_POOLTAG);
    if (!g_Engine)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeInitializeSpinLock(&g_Engine->Lock);

    LARGE_INTEGER freq;
    KeQueryPerformanceCounter(&freq);
    g_Engine->QpcFrequency = freq.QuadPart;

    for (ULONG i = 0; i < EEVAD_LOOP_COUNT; ++i)
    {
        g_Engine->Loops[i].Ring = (float*)ExAllocatePool2(
            POOL_FLAG_NON_PAGED,
            sizeof(float) * EEVAD_RING_FRAMES * EEVAD_CHANNELS,
            EEVAD_POOLTAG);

        if (!g_Engine->Loops[i].Ring)
        {
            EevadEngineCleanup();
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    g_Engine->Timer = ExAllocateTimer(TimerCallback, nullptr, EX_TIMER_HIGH_RESOLUTION);
    if (!g_Engine->Timer)
    {
        EevadEngineCleanup();
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return STATUS_SUCCESS;
}

void EevadEngineCleanup()
{
    PAGED_CODE();

    if (!g_Engine)
        return;

    if (g_Engine->Timer)
    {
        // Cancel + wait for any in-flight callback.
        ExDeleteTimer(g_Engine->Timer, TRUE, TRUE, nullptr);
        g_Engine->Timer = nullptr;
    }

    for (ULONG i = 0; i < EEVAD_LOOP_COUNT; ++i)
        if (g_Engine->Loops[i].Ring)
            ExFreePoolWithTag(g_Engine->Loops[i].Ring, EEVAD_POOLTAG);

    ExFreePoolWithTag(g_Engine, EEVAD_POOLTAG);
    g_Engine = nullptr;
}

void EevadEngineRegister(EevadStreamBase* Stream, ULONG LoopIndex)
{
    if (!g_Engine || LoopIndex >= EEVAD_LOOP_COUNT)
        return;

    KIRQL old;
    KeAcquireSpinLock(&g_Engine->Lock, &old);

    for (ULONG i = 0; i < g_Engine->StreamCount; ++i)
    {
        if (g_Engine->Streams[i] == Stream)
        {
            KeReleaseSpinLock(&g_Engine->Lock, old);
            return;
        }
    }

    if (g_Engine->StreamCount < EEVAD_MAX_STREAMS)
    {
        g_Engine->Streams[g_Engine->StreamCount] = Stream;
        g_Engine->StreamLoop[g_Engine->StreamCount] = LoopIndex;
        g_Engine->StreamCount++;

        if (!Stream->IsCaptureStream())
            g_Engine->Loops[LoopIndex].ActiveRenderers++;

        if (!g_Engine->TimerRunning)
        {
            ExSetTimer(g_Engine->Timer, -EEVAD_TIMER_PERIOD_100NS, EEVAD_TIMER_PERIOD_100NS, nullptr);
            g_Engine->TimerRunning = TRUE;
        }
    }

    KeReleaseSpinLock(&g_Engine->Lock, old);
}

void EevadEngineUnregister(EevadStreamBase* Stream, ULONG LoopIndex)
{
    if (!g_Engine || LoopIndex >= EEVAD_LOOP_COUNT)
        return;

    KIRQL old;
    KeAcquireSpinLock(&g_Engine->Lock, &old);

    for (ULONG i = 0; i < g_Engine->StreamCount; ++i)
    {
        if (g_Engine->Streams[i] != Stream)
            continue;

        // Flush the stream up to "now" before it stops so no frames are lost.
        PumpAllLocked(KeQueryPerformanceCounter(nullptr).QuadPart);

        if (!Stream->IsCaptureStream() && g_Engine->Loops[LoopIndex].ActiveRenderers > 0)
            g_Engine->Loops[LoopIndex].ActiveRenderers--;

        for (ULONG j = i; j + 1 < g_Engine->StreamCount; ++j)
        {
            g_Engine->Streams[j] = g_Engine->Streams[j + 1];
            g_Engine->StreamLoop[j] = g_Engine->StreamLoop[j + 1];
        }
        g_Engine->StreamCount--;
        g_Engine->Streams[g_Engine->StreamCount] = nullptr;
        break;
    }

    if (g_Engine->StreamCount == 0 && g_Engine->TimerRunning)
    {
        ExCancelTimer(g_Engine->Timer, nullptr);
        g_Engine->TimerRunning = FALSE;
    }

    KeReleaseSpinLock(&g_Engine->Lock, old);
}

void EevadEnginePumpNow()
{
    if (!g_Engine)
        return;

    KIRQL old;
    KeAcquireSpinLock(&g_Engine->Lock, &old);
    PumpAllLocked(KeQueryPerformanceCounter(nullptr).QuadPart);
    KeReleaseSpinLock(&g_Engine->Lock, old);
}

KIRQL EevadEngineLock()
{
    KIRQL old = PASSIVE_LEVEL;
    if (g_Engine)
        KeAcquireSpinLock(&g_Engine->Lock, &old);
    return old;
}

void EevadEngineUnlock(KIRQL OldIrql)
{
    if (g_Engine)
        KeReleaseSpinLock(&g_Engine->Lock, OldIrql);
}

EEVAD_LOOP* EevadEngineLoopLocked(ULONG LoopIndex)
{
    if (!g_Engine || LoopIndex >= EEVAD_LOOP_COUNT)
        return nullptr;
    return &g_Engine->Loops[LoopIndex];
}

LONGLONG EevadEngineQpcFrequency()
{
    return g_Engine ? g_Engine->QpcFrequency : 1;
}
