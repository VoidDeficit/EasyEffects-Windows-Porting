/*
 * EEVAD adapter: DriverEntry, AddDevice and StartDevice.
 *
 * Registers 4 WaveRT + 4 topology subdevices and wires each wave filter to its
 * topology filter with a physical connection. Subdevice names must match the
 * KSNAME_* strings in EEVirtualAudio.inf.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "common.h"
#include "loopback.h"

//=============================================================================
// Endpoint table
//=============================================================================

namespace
{
    const EEVAD_ENDPOINT_CONFIG g_Endpoints[EEVAD_ENDPOINT_COUNT] =
    {
        // System audio cable
        { L"Wave0", L"Topo0", FALSE, 0, &KSNODETYPE_SPEAKER,        &EEVAD_NAME_SPEAKERS },
        { L"Wave1", L"Topo1", TRUE,  0, &KSNODETYPE_LINE_CONNECTOR, &EEVAD_NAME_SPEAKERS_MONITOR },
        // Microphone cable
        { L"Wave2", L"Topo2", FALSE, 1, &KSNODETYPE_LINE_CONNECTOR, &EEVAD_NAME_MIC_FEED },
        { L"Wave3", L"Topo3", TRUE,  1, &KSNODETYPE_MICROPHONE,     &EEVAD_NAME_MICROPHONE },
    };

    PDRIVER_UNLOAD g_PortClsUnload = nullptr;
}

const EEVAD_ENDPOINT_CONFIG* EevadGetEndpoint(ULONG Index)
{
    return Index < EEVAD_ENDPOINT_COUNT ? &g_Endpoints[Index] : nullptr;
}

//=============================================================================
// Pool-backed operator new / delete
//=============================================================================

PVOID operator new(size_t Size, POOL_FLAGS Flags, ULONG Tag)
{
    return ExAllocatePool2(Flags, Size, Tag);   // zero-initialised
}

// Plain operator delete(void*) is provided by stdunk.lib (ExFreePool, any tag).

void __cdecl operator delete(PVOID Ptr, size_t Size)
{
    UNREFERENCED_PARAMETER(Size);
    if (Ptr)
        ExFreePoolWithTag(Ptr, EEVAD_POOLTAG);
}

void __cdecl operator delete[](PVOID Ptr)
{
    if (Ptr)
        ExFreePoolWithTag(Ptr, EEVAD_POOLTAG);
}

//=============================================================================
// Subdevice helpers
//=============================================================================

namespace
{
    NTSTATUS InstallSubdevice(
        _In_  PDEVICE_OBJECT    DeviceObject,
        _In_  PIRP              Irp,
        _In_  PCWSTR            Name,
        _In_  REFGUID           PortClassId,
        _In_  PUNKNOWN          Miniport,
        _In_  PRESOURCELIST     ResourceList,
        _Out_ PUNKNOWN*         OutPort)
    {
        *OutPort = nullptr;

        PPORT port = nullptr;
        NTSTATUS status = PcNewPort(&port, PortClassId);
        if (!NT_SUCCESS(status))
            return status;

        status = port->Init(DeviceObject, Irp, Miniport, nullptr, ResourceList);
        if (NT_SUCCESS(status))
            status = PcRegisterSubdevice(DeviceObject, const_cast<PWSTR>(Name), port);

        if (NT_SUCCESS(status))
        {
            *OutPort = PUNKNOWN(port);   // transfer our reference
            return STATUS_SUCCESS;
        }

        port->Release();
        return status;
    }

    NTSTATUS InstallEndpoint(PDEVICE_OBJECT DeviceObject, PIRP Irp, PRESOURCELIST ResourceList, ULONG Index)
    {
        const EEVAD_ENDPOINT_CONFIG* ep = &g_Endpoints[Index];

        PUNKNOWN topoMiniport = nullptr;
        PUNKNOWN waveMiniport = nullptr;
        PUNKNOWN topoPort = nullptr;
        PUNKNOWN wavePort = nullptr;

        NTSTATUS status = CreateEevadMiniportTopology(&topoMiniport, Index);
        if (NT_SUCCESS(status))
            status = InstallSubdevice(DeviceObject, Irp, ep->TopoName, CLSID_PortTopology,
                                      topoMiniport, ResourceList, &topoPort);

        if (NT_SUCCESS(status))
            status = CreateEevadMiniportWaveRT(&waveMiniport, Index);

        if (NT_SUCCESS(status))
            status = InstallSubdevice(DeviceObject, Irp, ep->WaveName, CLSID_PortWaveRT,
                                      waveMiniport, ResourceList, &wavePort);

        if (NT_SUCCESS(status))
        {
            if (!ep->IsCapture)
            {
                status = PcRegisterPhysicalConnection(DeviceObject,
                    wavePort, EEVAD_PIN_RENDER_WAVE_BRIDGE,
                    topoPort, EEVAD_PIN_RENDER_TOPO_BRIDGE);
            }
            else
            {
                status = PcRegisterPhysicalConnection(DeviceObject,
                    topoPort, EEVAD_PIN_CAPTURE_TOPO_BRIDGE,
                    wavePort, EEVAD_PIN_CAPTURE_WAVE_BRIDGE);
            }
        }

        if (wavePort)     wavePort->Release();
        if (topoPort)     topoPort->Release();
        if (waveMiniport) waveMiniport->Release();
        if (topoMiniport) topoMiniport->Release();
        return status;
    }
}

//=============================================================================
// PnP entry points
//=============================================================================

extern "C" DRIVER_INITIALIZE DriverEntry;
static DRIVER_UNLOAD EevadDriverUnload;
static DRIVER_ADD_DEVICE EevadAddDevice;

static NTSTATUS EevadStartDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp,
    _In_ PRESOURCELIST  ResourceList)
{
    PAGED_CODE();

    for (ULONG i = 0; i < EEVAD_ENDPOINT_COUNT; ++i)
    {
        NTSTATUS status = InstallEndpoint(DeviceObject, Irp, ResourceList, i);
        if (!NT_SUCCESS(status))
            return status;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS EevadAddDevice(_In_ PDRIVER_OBJECT DriverObject, _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PAGED_CODE();

    // One slot per subdevice (4 wave + 4 topology).
    return PcAddAdapterDevice(DriverObject, PhysicalDeviceObject,
                              PCPFNSTARTDEVICE(EevadStartDevice),
                              EEVAD_ENDPOINT_COUNT * 2, 0);
}

static VOID EevadDriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();

    EevadEngineCleanup();

    if (g_PortClsUnload)
        g_PortClsUnload(DriverObject);
}

extern "C" NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS status = EevadEngineInitialize();
    if (!NT_SUCCESS(status))
        return status;

    status = PcInitializeAdapterDriver(DriverObject, RegistryPath, PDRIVER_ADD_DEVICE(EevadAddDevice));
    if (!NT_SUCCESS(status))
    {
        EevadEngineCleanup();
        return status;
    }

    // Chain our unload in front of PortCls' so the engine timer is torn down.
    g_PortClsUnload = DriverObject->DriverUnload;
    DriverObject->DriverUnload = EevadDriverUnload;
    return STATUS_SUCCESS;
}
