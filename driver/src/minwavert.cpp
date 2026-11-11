/*
 * EEVAD WaveRT miniport + stream implementation.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "minwavert.h"

//=============================================================================
// Filter descriptors
//=============================================================================

namespace
{
    // Streaming pin data ranges: 48 kHz stereo, 16-bit PCM and 32-bit float.
    KSDATARANGE_AUDIO g_StreamDataRanges[] =
    {
        {
            {
                sizeof(KSDATARANGE_AUDIO), 0, 0, 0,
                STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
                STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
                STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
            },
            EEVAD_CHANNELS, 16, 16, EEVAD_SAMPLE_RATE, EEVAD_SAMPLE_RATE
        },
        {
            {
                sizeof(KSDATARANGE_AUDIO), 0, 0, 0,
                STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
                STATICGUIDOF(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT),
                STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
            },
            EEVAD_CHANNELS, 32, 32, EEVAD_SAMPLE_RATE, EEVAD_SAMPLE_RATE
        },
    };

    PKSDATARANGE g_StreamDataRangePointers[] =
    {
        PKSDATARANGE(&g_StreamDataRanges[0]),
        PKSDATARANGE(&g_StreamDataRanges[1]),
    };

    KSDATARANGE g_BridgeDataRanges[] =
    {
        {
            sizeof(KSDATARANGE), 0, 0, 0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)
        }
    };

    PKSDATARANGE g_BridgeDataRangePointers[] =
    {
        &g_BridgeDataRanges[0]
    };

    //-------------------------------------------------------------------------
    // KSPROPERTY_PIN_PROPOSEDATAFORMAT: lets the audio endpoint builder ask
    // whether a format is supported without opening the pin.
    //-------------------------------------------------------------------------
    NTSTATUS PropertyHandlerProposedFormat(_In_ PPCPROPERTY_REQUEST Request)
    {
        if (Request->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
        {
            if (Request->ValueSize >= sizeof(KSPROPERTY_DESCRIPTION))
            {
                auto* desc = PKSPROPERTY_DESCRIPTION(Request->Value);
                desc->AccessFlags = KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_SET;
                desc->DescriptionSize = sizeof(KSPROPERTY_DESCRIPTION);
                desc->PropTypeSet.Set = KSPROPTYPESETID_General;
                desc->PropTypeSet.Id = VT_ILLEGAL;
                desc->PropTypeSet.Flags = 0;
                desc->MembersListCount = 0;
                desc->Reserved = 0;
                Request->ValueSize = sizeof(KSPROPERTY_DESCRIPTION);
                return STATUS_SUCCESS;
            }
            if (Request->ValueSize >= sizeof(ULONG))
            {
                *PULONG(Request->Value) = KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_SET;
                Request->ValueSize = sizeof(ULONG);
                return STATUS_SUCCESS;
            }
            Request->ValueSize = 0;
            return STATUS_BUFFER_TOO_SMALL;
        }

        if (Request->Verb & KSPROPERTY_TYPE_SET)
        {
            if (Request->ValueSize < sizeof(KSDATAFORMAT) || !Request->Value)
                return STATUS_BUFFER_TOO_SMALL;

            return EevadClassifyFormat(PKSDATAFORMAT(Request->Value), Request->ValueSize) != EevadSampleUnsupported
                ? STATUS_SUCCESS
                : STATUS_NO_MATCH;
        }

        return STATUS_INVALID_DEVICE_REQUEST;
    }

    PCPROPERTY_ITEM g_StreamPinProperties[] =
    {
        {
            &KSPROPSETID_Pin,
            KSPROPERTY_PIN_PROPOSEDATAFORMAT,
            PCPROPERTY_ITEM_FLAG_SET | PCPROPERTY_ITEM_FLAG_BASICSUPPORT,
            PropertyHandlerProposedFormat
        },
    };

    DEFINE_PCAUTOMATION_TABLE_PROP(g_StreamPinAutomation, g_StreamPinProperties);

    //-------------------------------------------------------------------------
    // Render filter: [0] stream sink (IN) -> DAC -> [1] bridge (OUT)
    //-------------------------------------------------------------------------
    PCPIN_DESCRIPTOR g_RenderPins[] =
    {
        {   // EEVAD_PIN_RENDER_WAVE_STREAM
            1, 1, 0,
            &g_StreamPinAutomation,
            {
                0, nullptr, 0, nullptr,
                SIZEOF_ARRAY(g_StreamDataRangePointers), g_StreamDataRangePointers,
                KSPIN_DATAFLOW_IN, KSPIN_COMMUNICATION_SINK,
                &KSCATEGORY_AUDIO, nullptr, 0
            }
        },
        {   // EEVAD_PIN_RENDER_WAVE_BRIDGE
            0, 0, 0,
            nullptr,
            {
                0, nullptr, 0, nullptr,
                SIZEOF_ARRAY(g_BridgeDataRangePointers), g_BridgeDataRangePointers,
                KSPIN_DATAFLOW_OUT, KSPIN_COMMUNICATION_NONE,
                &KSCATEGORY_AUDIO, nullptr, 0
            }
        },
    };

    PCNODE_DESCRIPTOR g_RenderNodes[] =
    {
        { 0, nullptr, &KSNODETYPE_DAC, nullptr },
    };

    PCCONNECTION_DESCRIPTOR g_RenderConnections[] =
    {
        { PCFILTER_NODE, EEVAD_PIN_RENDER_WAVE_STREAM, 0, KSNODEPIN_STANDARD_IN },
        { 0, KSNODEPIN_STANDARD_OUT, PCFILTER_NODE, EEVAD_PIN_RENDER_WAVE_BRIDGE },
    };

    GUID g_RenderFilterCategories[] =
    {
        STATICGUIDOF(KSCATEGORY_AUDIO),
        STATICGUIDOF(KSCATEGORY_RENDER),
        STATICGUIDOF(KSCATEGORY_REALTIME),
    };

    PCFILTER_DESCRIPTOR g_RenderFilter =
    {
        0,
        nullptr,
        sizeof(PCPIN_DESCRIPTOR),
        SIZEOF_ARRAY(g_RenderPins), g_RenderPins,
        sizeof(PCNODE_DESCRIPTOR),
        SIZEOF_ARRAY(g_RenderNodes), g_RenderNodes,
        SIZEOF_ARRAY(g_RenderConnections), g_RenderConnections,
        SIZEOF_ARRAY(g_RenderFilterCategories), g_RenderFilterCategories
    };

    //-------------------------------------------------------------------------
    // Capture filter: [0] bridge (IN) -> ADC -> [1] stream source (OUT)
    //-------------------------------------------------------------------------
    PCPIN_DESCRIPTOR g_CapturePins[] =
    {
        {   // EEVAD_PIN_CAPTURE_WAVE_BRIDGE
            0, 0, 0,
            nullptr,
            {
                0, nullptr, 0, nullptr,
                SIZEOF_ARRAY(g_BridgeDataRangePointers), g_BridgeDataRangePointers,
                KSPIN_DATAFLOW_IN, KSPIN_COMMUNICATION_NONE,
                &KSCATEGORY_AUDIO, nullptr, 0
            }
        },
        {   // EEVAD_PIN_CAPTURE_WAVE_STREAM
            1, 1, 0,
            &g_StreamPinAutomation,
            {
                0, nullptr, 0, nullptr,
                SIZEOF_ARRAY(g_StreamDataRangePointers), g_StreamDataRangePointers,
                KSPIN_DATAFLOW_OUT, KSPIN_COMMUNICATION_SINK,
                &KSCATEGORY_AUDIO, &KSAUDFNAME_RECORDING_CONTROL, 0
            }
        },
    };

    PCNODE_DESCRIPTOR g_CaptureNodes[] =
    {
        { 0, nullptr, &KSNODETYPE_ADC, nullptr },
    };

    PCCONNECTION_DESCRIPTOR g_CaptureConnections[] =
    {
        { PCFILTER_NODE, EEVAD_PIN_CAPTURE_WAVE_BRIDGE, 0, KSNODEPIN_STANDARD_IN },
        { 0, KSNODEPIN_STANDARD_OUT, PCFILTER_NODE, EEVAD_PIN_CAPTURE_WAVE_STREAM },
    };

    GUID g_CaptureFilterCategories[] =
    {
        STATICGUIDOF(KSCATEGORY_AUDIO),
        STATICGUIDOF(KSCATEGORY_CAPTURE),
        STATICGUIDOF(KSCATEGORY_REALTIME),
    };

    PCFILTER_DESCRIPTOR g_CaptureFilter =
    {
        0,
        nullptr,
        sizeof(PCPIN_DESCRIPTOR),
        SIZEOF_ARRAY(g_CapturePins), g_CapturePins,
        sizeof(PCNODE_DESCRIPTOR),
        SIZEOF_ARRAY(g_CaptureNodes), g_CaptureNodes,
        SIZEOF_ARRAY(g_CaptureConnections), g_CaptureConnections,
        SIZEOF_ARRAY(g_CaptureFilterCategories), g_CaptureFilterCategories
    };

    inline LONGLONG Min64(LONGLONG a, LONGLONG b) { return a < b ? a : b; }
}

//=============================================================================
// Format helper
//=============================================================================

EEVAD_SAMPLE_KIND EevadClassifyFormat(PKSDATAFORMAT Format, ULONG FormatSize)
{
    if (!Format || FormatSize < sizeof(KSDATAFORMAT_WAVEFORMATEX))
        return EevadSampleUnsupported;

    if (!IsEqualGUIDAligned(Format->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) ||
        !IsEqualGUIDAligned(Format->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX))
        return EevadSampleUnsupported;

    auto* wfx = PWAVEFORMATEX(Format + 1);

    if (wfx->nChannels != EEVAD_CHANNELS || wfx->nSamplesPerSec != EEVAD_SAMPLE_RATE)
        return EevadSampleUnsupported;

    if (wfx->wFormatTag == WAVE_FORMAT_PCM && wfx->wBitsPerSample == 16)
        return EevadSamplePcm16;

    if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && wfx->wBitsPerSample == 32)
        return EevadSampleFloat32;

    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        FormatSize >= sizeof(KSDATAFORMAT) + sizeof(WAVEFORMATEXTENSIBLE))
    {
        auto* wfxe = PWAVEFORMATEXTENSIBLE(wfx);

        if (IsEqualGUIDAligned(wfxe->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) &&
            wfx->wBitsPerSample == 16 && wfxe->Samples.wValidBitsPerSample == 16)
            return EevadSamplePcm16;

        if (IsEqualGUIDAligned(wfxe->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) &&
            wfx->wBitsPerSample == 32)
            return EevadSampleFloat32;
    }

    return EevadSampleUnsupported;
}

//=============================================================================
// CEevadMiniportWaveRT
//=============================================================================

NTSTATUS CreateEevadMiniportWaveRT(PUNKNOWN* Unknown, ULONG EndpointIndex)
{
    *Unknown = nullptr;

    if (!EevadGetEndpoint(EndpointIndex))
        return STATUS_INVALID_PARAMETER;

    auto* obj = new (POOL_FLAG_NON_PAGED, EEVAD_POOLTAG) CEevadMiniportWaveRT(nullptr, EndpointIndex);
    if (!obj)
        return STATUS_INSUFFICIENT_RESOURCES;

    *Unknown = PUNKNOWN(PMINIPORTWAVERT(obj));
    (*Unknown)->AddRef();
    return STATUS_SUCCESS;
}

CEevadMiniportWaveRT::CEevadMiniportWaveRT(PUNKNOWN OuterUnknown, ULONG EndpointIndex)
    : CUnknown(OuterUnknown),
      m_Endpoint(EevadGetEndpoint(EndpointIndex)),
      m_Port(nullptr)
{
}

CEevadMiniportWaveRT::~CEevadMiniportWaveRT()
{
    if (m_Port)
    {
        m_Port->Release();
        m_Port = nullptr;
    }
}

STDMETHODIMP_(NTSTATUS) CEevadMiniportWaveRT::NonDelegatingQueryInterface(REFIID Interface, PVOID* Object)
{
    if (IsEqualGUIDAligned(Interface, IID_IUnknown))
        *Object = PVOID(PUNKNOWN(PMINIPORTWAVERT(this)));
    else if (IsEqualGUIDAligned(Interface, IID_IMiniport))
        *Object = PVOID(PMINIPORT(this));
    else if (IsEqualGUIDAligned(Interface, IID_IMiniportWaveRT))
        *Object = PVOID(PMINIPORTWAVERT(this));
    else
    {
        *Object = nullptr;
        return STATUS_INVALID_PARAMETER;
    }

    PUNKNOWN(*Object)->AddRef();
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS) CEevadMiniportWaveRT::Init(PUNKNOWN UnknownAdapter, PRESOURCELIST ResourceList, PPORTWAVERT Port)
{
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);

    m_Port = Port;
    m_Port->AddRef();
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS) CEevadMiniportWaveRT::GetDescription(PPCFILTER_DESCRIPTOR* Description)
{
    *Description = m_Endpoint->IsCapture ? &g_CaptureFilter : &g_RenderFilter;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS) CEevadMiniportWaveRT::DataRangeIntersection(
    ULONG PinId, PKSDATARANGE DataRange, PKSDATARANGE MatchingDataRange,
    ULONG OutputBufferLength, PVOID ResultantFormat, PULONG ResultantFormatLength)
{
    UNREFERENCED_PARAMETER(PinId);
    UNREFERENCED_PARAMETER(DataRange);
    UNREFERENCED_PARAMETER(MatchingDataRange);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(ResultantFormat);
    UNREFERENCED_PARAMETER(ResultantFormatLength);

    // Let PortCls' default KSDATARANGE_AUDIO intersection handler do the work.
    return STATUS_NOT_IMPLEMENTED;
}

STDMETHODIMP_(NTSTATUS) CEevadMiniportWaveRT::GetDeviceDescription(PDEVICE_DESCRIPTION DeviceDescription)
{
    RtlZeroMemory(DeviceDescription, sizeof(DEVICE_DESCRIPTION));
    DeviceDescription->Master = TRUE;
    DeviceDescription->ScatterGather = TRUE;
    DeviceDescription->Dma32BitAddresses = TRUE;
    DeviceDescription->InterfaceType = PCIBus;
    DeviceDescription->MaximumLength = 0xFFFFFFFF;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS) CEevadMiniportWaveRT::NewStream(
    PMINIPORTWAVERTSTREAM* Stream, PPORTWAVERTSTREAM PortStream,
    ULONG Pin, BOOLEAN Capture, PKSDATAFORMAT DataFormat)
{
    *Stream = nullptr;

    const ULONG expectedPin = m_Endpoint->IsCapture ? EEVAD_PIN_CAPTURE_WAVE_STREAM : EEVAD_PIN_RENDER_WAVE_STREAM;
    if (Pin != expectedPin || Capture != m_Endpoint->IsCapture)
        return STATUS_INVALID_PARAMETER;

    const EEVAD_SAMPLE_KIND kind = EevadClassifyFormat(DataFormat, DataFormat->FormatSize);
    if (kind == EevadSampleUnsupported)
        return STATUS_NO_MATCH;

    auto* stream = new (POOL_FLAG_NON_PAGED, EEVAD_POOLTAG) CEevadMiniportWaveRTStream(nullptr);
    if (!stream)
        return STATUS_INSUFFICIENT_RESOURCES;

    stream->AddRef();
    NTSTATUS status = stream->Init(this, PortStream, Capture, m_Endpoint->LoopIndex, kind);
    if (NT_SUCCESS(status))
    {
        *Stream = PMINIPORTWAVERTSTREAM(stream);
        (*Stream)->AddRef();
    }
    stream->Release();
    return status;
}

//=============================================================================
// CEevadMiniportWaveRTStream
//=============================================================================

CEevadMiniportWaveRTStream::~CEevadMiniportWaveRTStream()
{
    if (m_Registered)
    {
        EevadEngineUnregister(this, m_Loop);
        m_Registered = FALSE;
    }

    if (m_Mdl && m_PortStream)
    {
        if (m_Buffer)
            m_PortStream->UnmapAllocatedPages(m_Buffer, m_Mdl);
        m_PortStream->FreePagesFromMdl(m_Mdl);
    }
    m_Mdl = nullptr;
    m_Buffer = nullptr;

    if (m_PortStream)
    {
        m_PortStream->Release();
        m_PortStream = nullptr;
    }

    if (m_Miniport)
    {
        m_Miniport->Release();
        m_Miniport = nullptr;
    }
}

STDMETHODIMP_(NTSTATUS) CEevadMiniportWaveRTStream::NonDelegatingQueryInterface(REFIID Interface, PVOID* Object)
{
    if (IsEqualGUIDAligned(Interface, IID_IUnknown))
        *Object = PVOID(PUNKNOWN(PMINIPORTWAVERTSTREAM(this)));
    else if (IsEqualGUIDAligned(Interface, IID_IMiniportWaveRTStream))
        *Object = PVOID(PMINIPORTWAVERTSTREAM(this));
    else
    {
        *Object = nullptr;
        return STATUS_INVALID_PARAMETER;
    }

    PUNKNOWN(*Object)->AddRef();
    return STATUS_SUCCESS;
}

NTSTATUS CEevadMiniportWaveRTStream::Init(
    CEevadMiniportWaveRT* Miniport, PPORTWAVERTSTREAM PortStream,
    BOOLEAN Capture, ULONG LoopIndex, EEVAD_SAMPLE_KIND Kind)
{
    m_Miniport = Miniport;
    m_Miniport->AddRef();

    m_PortStream = PortStream;
    m_PortStream->AddRef();

    m_Capture = Capture;
    m_Loop = LoopIndex;
    m_Kind = Kind;
    m_BlockAlign = EEVAD_CHANNELS * (Kind == EevadSamplePcm16 ? sizeof(SHORT) : sizeof(float));
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS) CEevadMiniportWaveRTStream::SetFormat(PKSDATAFORMAT DataFormat)
{
    // Format changes are only allowed while stopped.
    if (m_State != KSSTATE_STOP || m_Buffer)
        return STATUS_INVALID_DEVICE_REQUEST;

    const EEVAD_SAMPLE_KIND kind = EevadClassifyFormat(DataFormat, DataFormat->FormatSize);
    if (kind == EevadSampleUnsupported)
        return STATUS_NO_MATCH;

    m_Kind = kind;
    m_BlockAlign = EEVAD_CHANNELS * (kind == EevadSamplePcm16 ? sizeof(SHORT) : sizeof(float));
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS) CEevadMiniportWaveRTStream::SetState(KSSTATE State)
{
    switch (State)
    {
    case KSSTATE_RUN:
        if (!m_Registered)
        {
            KIRQL old = EevadEngineLock();
            m_RunQpc = KeQueryPerformanceCounter(nullptr).QuadPart;
            m_FramesBase = m_FramesDone;
            if (m_Capture)
            {
                EEVAD_LOOP* loop = EevadEngineLoopLocked(m_Loop);
                m_ReadPos = loop ? loop->WritePos - EEVAD_TARGET_LAG_FRAMES : 0;
            }
            EevadEngineUnlock(old);

            EevadEngineRegister(this, m_Loop);
            m_Registered = TRUE;
        }
        break;

    case KSSTATE_PAUSE:
    case KSSTATE_ACQUIRE:
        if (m_Registered)
        {
            EevadEngineUnregister(this, m_Loop);
            m_Registered = FALSE;
        }
        break;

    case KSSTATE_STOP:
        if (m_Registered)
        {
            EevadEngineUnregister(this, m_Loop);
            m_Registered = FALSE;
        }
        {
            // WaveRT: STOP resets the position to zero.
            KIRQL old = EevadEngineLock();
            m_FramesDone = 0;
            m_FramesBase = 0;
            EevadEngineUnlock(old);
        }
        if (m_Capture && m_Buffer)
            RtlZeroMemory(m_Buffer, m_BufferSize);
        break;
    }

    m_State = State;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS) CEevadMiniportWaveRTStream::GetPosition(PKSAUDIO_POSITION Position)
{
    EevadEnginePumpNow();

    KIRQL old = EevadEngineLock();
    ULONGLONG offset = 0;
    if (m_BufferFrames > 0)
        offset = ULONGLONG(m_FramesDone % m_BufferFrames) * m_BlockAlign;
    EevadEngineUnlock(old);

    Position->PlayOffset = offset;
    Position->WriteOffset = offset;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS) CEevadMiniportWaveRTStream::AllocateAudioBuffer(
    ULONG RequestedSize, PMDL* AudioBufferMdl, ULONG* ActualSize,
    ULONG* OffsetFromFirstPage, MEMORY_CACHING_TYPE* CacheType)
{
    if (m_Mdl)
        return STATUS_UNSUCCESSFUL;

    ULONG size = RequestedSize - (RequestedSize % m_BlockAlign);
    if (size == 0)
        return STATUS_INVALID_PARAMETER;

    PHYSICAL_ADDRESS high;
    high.QuadPart = MAXLONGLONG;

    PMDL mdl = m_PortStream->AllocatePagesForMdl(high, size);
    if (!mdl)
        return STATUS_INSUFFICIENT_RESOURCES;

    PVOID va = m_PortStream->MapAllocatedPages(mdl, MmCached);
    if (!va)
    {
        m_PortStream->FreePagesFromMdl(mdl);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(va, size);

    KIRQL old = EevadEngineLock();
    m_Mdl = mdl;
    m_Buffer = PBYTE(va);
    m_BufferSize = size;
    m_BufferFrames = size / m_BlockAlign;
    EevadEngineUnlock(old);

    *AudioBufferMdl = mdl;
    *ActualSize = size;
    *OffsetFromFirstPage = 0;
    *CacheType = MmCached;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(VOID) CEevadMiniportWaveRTStream::FreeAudioBuffer(PMDL AudioBufferMdl, ULONG BufferSize)
{
    UNREFERENCED_PARAMETER(BufferSize);

    if (!AudioBufferMdl || AudioBufferMdl != m_Mdl)
        return;

    PBYTE buffer;
    KIRQL old = EevadEngineLock();
    buffer = m_Buffer;
    m_Buffer = nullptr;
    m_BufferSize = 0;
    m_BufferFrames = 0;
    m_Mdl = nullptr;
    EevadEngineUnlock(old);

    if (buffer)
        m_PortStream->UnmapAllocatedPages(buffer, AudioBufferMdl);
    m_PortStream->FreePagesFromMdl(AudioBufferMdl);
}

STDMETHODIMP_(VOID) CEevadMiniportWaveRTStream::GetHWLatency(KSRTAUDIO_HWLATENCY* HwLatency)
{
    HwLatency->ChipsetDelay = 0;
    HwLatency->CodecDelay = 0;
    HwLatency->FifoSize = 0;
}

STDMETHODIMP_(NTSTATUS) CEevadMiniportWaveRTStream::GetPositionRegister(KSRTAUDIO_HWREGISTER* Register)
{
    UNREFERENCED_PARAMETER(Register);
    return STATUS_NOT_SUPPORTED;
}

STDMETHODIMP_(NTSTATUS) CEevadMiniportWaveRTStream::GetClockRegister(KSRTAUDIO_HWREGISTER* Register)
{
    UNREFERENCED_PARAMETER(Register);
    return STATUS_NOT_SUPPORTED;
}

//-----------------------------------------------------------------------------
// Engine callbacks (engine lock held, DISPATCH_LEVEL, FP state saved)
//-----------------------------------------------------------------------------

void CEevadMiniportWaveRTStream::PumpLocked(LONGLONG NowQpc)
{
    if (!m_Buffer || m_BufferFrames <= 0)
        return;

    const LONGLONG elapsed = NowQpc - m_RunQpc;
    if (elapsed <= 0)
        return;

    // elapsed * rate / freq, split to avoid 64-bit overflow on long uptimes.
    const LONGLONG freq = EevadEngineQpcFrequency();
    const LONGLONG target = m_FramesBase
        + (elapsed / freq) * EEVAD_SAMPLE_RATE
        + ((elapsed % freq) * EEVAD_SAMPLE_RATE) / freq;

    LONGLONG frames = target - m_FramesDone;
    if (frames <= 0)
        return;

    LONGLONG start = m_FramesDone;
    if (frames > m_BufferFrames)
    {
        // We were starved (e.g. long DPC latency); only the most recent
        // buffer's worth of audio is still meaningful.
        start = target - m_BufferFrames;
        frames = m_BufferFrames;
    }

    EEVAD_LOOP* loop = EevadEngineLoopLocked(m_Loop);
    if (loop && loop->Ring)
    {
        if (m_Capture)
            RingToCaptureLocked(loop, start, frames);
        else
            RenderToRingLocked(loop, start, frames);
    }

    m_FramesDone = target;
}

void CEevadMiniportWaveRTStream::RenderToRingLocked(EEVAD_LOOP* Loop, LONGLONG StartFrame, LONGLONG Frames)
{
    float* ring = Loop->Ring;

    for (LONGLONG i = 0; i < Frames; ++i)
    {
        const PBYTE src = m_Buffer + ULONG((StartFrame + i) % m_BufferFrames) * m_BlockAlign;
        const ULONG r = ULONG(ULONGLONG(Loop->WritePos + i) & EEVAD_RING_MASK) * EEVAD_CHANNELS;

        if (m_Kind == EevadSamplePcm16)
        {
            const SHORT* s = reinterpret_cast<const SHORT*>(src);
            ring[r]     = float(s[0]) * (1.0f / 32768.0f);
            ring[r + 1] = float(s[1]) * (1.0f / 32768.0f);
        }
        else
        {
            const float* f = reinterpret_cast<const float*>(src);
            ring[r]     = f[0];
            ring[r + 1] = f[1];
        }
    }

    Loop->WritePos += Frames;
}

void CEevadMiniportWaveRTStream::RingToCaptureLocked(EEVAD_LOOP* Loop, LONGLONG StartFrame, LONGLONG Frames)
{
    LONGLONG available = 0;

    if (Loop->ActiveRenderers == 0)
    {
        // Nothing is playing into this cable: deliver silence.
        m_ReadPos = Loop->WritePos;
    }
    else
    {
        available = Loop->WritePos - m_ReadPos;
        if (available < Frames || available > EEVAD_MAX_LAG_FRAMES)
        {
            // Underrun or drifted too far behind: re-anchor a little behind
            // the writer so both sides have slack for timer jitter.
            m_ReadPos = Loop->WritePos - Frames - EEVAD_TARGET_LAG_FRAMES;
            available = Frames + EEVAD_TARGET_LAG_FRAMES;
        }
    }

    const float* ring = Loop->Ring;

    for (LONGLONG i = 0; i < Frames; ++i)
    {
        PBYTE dst = m_Buffer + ULONG((StartFrame + i) % m_BufferFrames) * m_BlockAlign;

        float left = 0.0f, right = 0.0f;
        if (i < available)
        {
            const ULONG r = ULONG(ULONGLONG(m_ReadPos + i) & EEVAD_RING_MASK) * EEVAD_CHANNELS;
            left = ring[r];
            right = ring[r + 1];
        }

        if (m_Kind == EevadSamplePcm16)
        {
            if (left > 1.0f) left = 1.0f; else if (left < -1.0f) left = -1.0f;
            if (right > 1.0f) right = 1.0f; else if (right < -1.0f) right = -1.0f;
            SHORT* s = reinterpret_cast<SHORT*>(dst);
            s[0] = SHORT(left * 32767.0f);
            s[1] = SHORT(right * 32767.0f);
        }
        else
        {
            float* f = reinterpret_cast<float*>(dst);
            f[0] = left;
            f[1] = right;
        }
    }

    m_ReadPos += Min64(Frames, available);
}
