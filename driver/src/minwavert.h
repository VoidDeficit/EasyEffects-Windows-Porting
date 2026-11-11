/*
 * EEVAD WaveRT miniport + stream.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "common.h"
#include "loopback.h"

class CEevadMiniportWaveRT :
    public IMiniportWaveRT,
    public CUnknown
{
public:
    DECLARE_STD_UNKNOWN();

    CEevadMiniportWaveRT(PUNKNOWN OuterUnknown, ULONG EndpointIndex);
    ~CEevadMiniportWaveRT();

    IMP_IMiniportWaveRT;

    const EEVAD_ENDPOINT_CONFIG* Endpoint() const { return m_Endpoint; }

private:
    const EEVAD_ENDPOINT_CONFIG*    m_Endpoint;
    PPORTWAVERT                     m_Port;
};

class CEevadMiniportWaveRTStream :
    public IMiniportWaveRTStream,
    public CUnknown,
    public EevadStreamBase
{
public:
    DECLARE_STD_UNKNOWN();
    DEFINE_STD_CONSTRUCTOR(CEevadMiniportWaveRTStream);
    ~CEevadMiniportWaveRTStream();

    IMP_IMiniportWaveRTStream;

    NTSTATUS Init(
        _In_ CEevadMiniportWaveRT*  Miniport,
        _In_ PPORTWAVERTSTREAM      PortStream,
        _In_ BOOLEAN                Capture,
        _In_ ULONG                  LoopIndex,
        _In_ EEVAD_SAMPLE_KIND      Kind);

    // EevadStreamBase
    void    PumpLocked(LONGLONG NowQpc) override;
    BOOLEAN IsCaptureStream() const override { return m_Capture; }

private:
    void RenderToRingLocked(EEVAD_LOOP* Loop, LONGLONG StartFrame, LONGLONG Frames);
    void RingToCaptureLocked(EEVAD_LOOP* Loop, LONGLONG StartFrame, LONGLONG Frames);

    CEevadMiniportWaveRT*   m_Miniport = nullptr;
    PPORTWAVERTSTREAM       m_PortStream = nullptr;
    BOOLEAN                 m_Capture = FALSE;
    ULONG                   m_Loop = 0;
    EEVAD_SAMPLE_KIND       m_Kind = EevadSampleUnsupported;
    ULONG                   m_BlockAlign = 0;

    PMDL                    m_Mdl = nullptr;
    PBYTE                   m_Buffer = nullptr;
    ULONG                   m_BufferSize = 0;
    LONGLONG                m_BufferFrames = 0;

    KSSTATE                 m_State = KSSTATE_STOP;
    BOOLEAN                 m_Registered = FALSE;

    // Position bookkeeping (guarded by the engine lock).
    LONGLONG                m_RunQpc = 0;
    LONGLONG                m_FramesBase = 0;   // frames processed before the current RUN
    LONGLONG                m_FramesDone = 0;   // total frames processed (linear position)
    LONGLONG                m_ReadPos = 0;      // capture: ring read position
};
