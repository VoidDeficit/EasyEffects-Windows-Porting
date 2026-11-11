/*
 * EEVAD topology miniport.
 *
 * Each endpoint has a trivial topology filter: one bridge pin facing the wave
 * filter and one "jack" pin whose category/name GUID Windows uses to label the
 * endpoint ("EasyEffects Speakers", "EasyEffects Microphone", ...). There are
 * no volume/mute nodes, so Windows applies software endpoint volume.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "common.h"

namespace
{
    KSDATARANGE g_TopoBridgeRanges[] =
    {
        {
            sizeof(KSDATARANGE), 0, 0, 0,
            STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
            STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG),
            STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)
        }
    };

    PKSDATARANGE g_TopoBridgeRangePointers[] =
    {
        &g_TopoBridgeRanges[0]
    };

    GUID g_TopoCategories[] =
    {
        STATICGUIDOF(KSCATEGORY_AUDIO),
        STATICGUIDOF(KSCATEGORY_TOPOLOGY),
    };

    PCCONNECTION_DESCRIPTOR g_TopoConnections[] =
    {
        // Pin 0 (IN) -> Pin 1 (OUT) for both render and capture layouts.
        { PCFILTER_NODE, 0, PCFILTER_NODE, 1 },
    };
}

class CEevadMiniportTopology :
    public IMiniportTopology,
    public CUnknown
{
public:
    DECLARE_STD_UNKNOWN();

    CEevadMiniportTopology(PUNKNOWN OuterUnknown, ULONG EndpointIndex);
    ~CEevadMiniportTopology();

    IMP_IMiniportTopology;

private:
    void BuildDescriptor();

    const EEVAD_ENDPOINT_CONFIG*    m_Endpoint;
    PPORTTOPOLOGY                   m_Port;

    // Per-instance descriptor because the jack pin category/name differ per endpoint.
    PCPIN_DESCRIPTOR                m_Pins[2];
    PCFILTER_DESCRIPTOR             m_Filter;
};

NTSTATUS CreateEevadMiniportTopology(PUNKNOWN* Unknown, ULONG EndpointIndex)
{
    *Unknown = nullptr;

    if (!EevadGetEndpoint(EndpointIndex))
        return STATUS_INVALID_PARAMETER;

    auto* obj = new (POOL_FLAG_NON_PAGED, EEVAD_POOLTAG) CEevadMiniportTopology(nullptr, EndpointIndex);
    if (!obj)
        return STATUS_INSUFFICIENT_RESOURCES;

    *Unknown = PUNKNOWN(PMINIPORTTOPOLOGY(obj));
    (*Unknown)->AddRef();
    return STATUS_SUCCESS;
}

CEevadMiniportTopology::CEevadMiniportTopology(PUNKNOWN OuterUnknown, ULONG EndpointIndex)
    : CUnknown(OuterUnknown),
      m_Endpoint(EevadGetEndpoint(EndpointIndex)),
      m_Port(nullptr)
{
    BuildDescriptor();
}

CEevadMiniportTopology::~CEevadMiniportTopology()
{
    if (m_Port)
    {
        m_Port->Release();
        m_Port = nullptr;
    }
}

void CEevadMiniportTopology::BuildDescriptor()
{
    RtlZeroMemory(m_Pins, sizeof(m_Pins));
    RtlZeroMemory(&m_Filter, sizeof(m_Filter));

    for (ULONG i = 0; i < 2; ++i)
    {
        m_Pins[i].KsPinDescriptor.DataRangesCount = SIZEOF_ARRAY(g_TopoBridgeRangePointers);
        m_Pins[i].KsPinDescriptor.DataRanges = g_TopoBridgeRangePointers;
        m_Pins[i].KsPinDescriptor.Communication = KSPIN_COMMUNICATION_NONE;
    }

    if (!m_Endpoint->IsCapture)
    {
        // [0] from wave filter (IN)  ->  [1] speaker / line-out jack (OUT)
        m_Pins[EEVAD_PIN_RENDER_TOPO_BRIDGE].KsPinDescriptor.DataFlow = KSPIN_DATAFLOW_IN;
        m_Pins[EEVAD_PIN_RENDER_TOPO_BRIDGE].KsPinDescriptor.Category = &KSCATEGORY_AUDIO;

        m_Pins[EEVAD_PIN_RENDER_TOPO_JACK].KsPinDescriptor.DataFlow = KSPIN_DATAFLOW_OUT;
        m_Pins[EEVAD_PIN_RENDER_TOPO_JACK].KsPinDescriptor.Category = m_Endpoint->JackCategory;
        m_Pins[EEVAD_PIN_RENDER_TOPO_JACK].KsPinDescriptor.Name = m_Endpoint->JackName;
    }
    else
    {
        // [0] microphone / line-in jack (IN)  ->  [1] to wave filter (OUT)
        m_Pins[EEVAD_PIN_CAPTURE_TOPO_JACK].KsPinDescriptor.DataFlow = KSPIN_DATAFLOW_IN;
        m_Pins[EEVAD_PIN_CAPTURE_TOPO_JACK].KsPinDescriptor.Category = m_Endpoint->JackCategory;
        m_Pins[EEVAD_PIN_CAPTURE_TOPO_JACK].KsPinDescriptor.Name = m_Endpoint->JackName;

        m_Pins[EEVAD_PIN_CAPTURE_TOPO_BRIDGE].KsPinDescriptor.DataFlow = KSPIN_DATAFLOW_OUT;
        m_Pins[EEVAD_PIN_CAPTURE_TOPO_BRIDGE].KsPinDescriptor.Category = &KSCATEGORY_AUDIO;
    }

    m_Filter.Version = 0;
    m_Filter.AutomationTable = nullptr;
    m_Filter.PinSize = sizeof(PCPIN_DESCRIPTOR);
    m_Filter.PinCount = SIZEOF_ARRAY(m_Pins);
    m_Filter.Pins = m_Pins;
    m_Filter.NodeSize = sizeof(PCNODE_DESCRIPTOR);
    m_Filter.NodeCount = 0;
    m_Filter.Nodes = nullptr;
    m_Filter.ConnectionCount = SIZEOF_ARRAY(g_TopoConnections);
    m_Filter.Connections = g_TopoConnections;
    m_Filter.CategoryCount = SIZEOF_ARRAY(g_TopoCategories);
    m_Filter.Categories = g_TopoCategories;
}

STDMETHODIMP_(NTSTATUS) CEevadMiniportTopology::NonDelegatingQueryInterface(REFIID Interface, PVOID* Object)
{
    if (IsEqualGUIDAligned(Interface, IID_IUnknown))
        *Object = PVOID(PUNKNOWN(PMINIPORTTOPOLOGY(this)));
    else if (IsEqualGUIDAligned(Interface, IID_IMiniport))
        *Object = PVOID(PMINIPORT(this));
    else if (IsEqualGUIDAligned(Interface, IID_IMiniportTopology))
        *Object = PVOID(PMINIPORTTOPOLOGY(this));
    else
    {
        *Object = nullptr;
        return STATUS_INVALID_PARAMETER;
    }

    PUNKNOWN(*Object)->AddRef();
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS) CEevadMiniportTopology::Init(PUNKNOWN UnknownAdapter, PRESOURCELIST ResourceList, PPORTTOPOLOGY Port)
{
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);

    m_Port = Port;
    m_Port->AddRef();
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS) CEevadMiniportTopology::GetDescription(PPCFILTER_DESCRIPTOR* Description)
{
    *Description = &m_Filter;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS) CEevadMiniportTopology::DataRangeIntersection(
    ULONG PinId, PKSDATARANGE DataRange, PKSDATARANGE MatchingDataRange,
    ULONG OutputBufferLength, PVOID ResultantFormat, PULONG ResultantFormatLength)
{
    UNREFERENCED_PARAMETER(PinId);
    UNREFERENCED_PARAMETER(DataRange);
    UNREFERENCED_PARAMETER(MatchingDataRange);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(ResultantFormat);
    UNREFERENCED_PARAMETER(ResultantFormatLength);
    return STATUS_NOT_IMPLEMENTED;
}
