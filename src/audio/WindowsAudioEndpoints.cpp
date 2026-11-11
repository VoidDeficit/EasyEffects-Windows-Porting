#include "WindowsAudioEndpoints.h"

#if defined(_WIN32)

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <propidl.h>
#include <propsys.h>

#include <shellapi.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace eeval::winaudio {

namespace {

// PKEY_Device_FriendlyName, defined locally to avoid needing INITGUID / propsys.lib.
const PROPERTYKEY kFriendlyNameKey = {
    { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14
};

const CLSID kMMDeviceEnumerator = { 0xbcde0395, 0xe52f, 0x467c, { 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e } };
const IID   kIMMDeviceEnumerator = { 0xa95664d2, 0x9614, 0x4f35, { 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6 } };

// IPolicyConfig (Windows 7+). Only SetDefaultEndpoint is used; the other
// methods are declared to keep the vtable layout correct.
const CLSID kPolicyConfigClient = { 0x870af99c, 0x171d, 0x4f9e, { 0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9 } };
const IID   kIPolicyConfig      = { 0xf8679f50, 0x850a, 0x41cf, { 0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8 } };

struct IPolicyConfig : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR deviceId, ERole role) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};

struct ComScope {
    bool needsUninit = false;
    ComScope() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        needsUninit = SUCCEEDED(hr);   // S_OK or S_FALSE; RPC_E_CHANGED_MODE means COM is already up
    }
    ~ComScope() { if (needsUninit) CoUninitialize(); }
};

template <typename T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

std::string toUtf8(const wchar_t* w)
{
    if (!w || !*w) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string out((size_t)len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring toWide(const std::string& s)
{
    if (s.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring out((size_t)len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
    return out;
}

std::optional<Endpoint> describe(IMMDevice* device)
{
    Endpoint ep;

    LPWSTR id = nullptr;
    if (FAILED(device->GetId(&id)) || !id)
        return std::nullopt;
    ep.id = toUtf8(id);
    CoTaskMemFree(id);

    ComPtr<IPropertyStore> props;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)) && props) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        if (SUCCEEDED(props->GetValue(kFriendlyNameKey, &pv)) && pv.vt == VT_LPWSTR)
            ep.name = toUtf8(pv.pwszVal);
        PropVariantClear(&pv);
    }
    return ep;
}

EDataFlow toDataFlow(Flow f) { return f == Flow::Render ? eRender : eCapture; }

} // namespace

std::vector<Endpoint> listEndpoints(Flow flow)
{
    std::vector<Endpoint> result;
    ComScope com;

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(kMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIMMDeviceEnumerator, (void**)&enumerator)))
        return result;

    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(toDataFlow(flow), DEVICE_STATE_ACTIVE, &collection)))
        return result;

    UINT count = 0;
    collection->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (SUCCEEDED(collection->Item(i, &device)) && device)
            if (auto ep = describe(device.p))
                result.push_back(std::move(*ep));
    }
    return result;
}

std::optional<Endpoint> getDefaultEndpoint(Flow flow)
{
    ComScope com;

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(kMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIMMDeviceEnumerator, (void**)&enumerator)))
        return std::nullopt;

    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(toDataFlow(flow), eConsole, &device)) || !device)
        return std::nullopt;

    return describe(device.p);
}

bool setDefaultEndpoint(const std::string& endpointId)
{
    if (endpointId.empty())
        return false;

    ComScope com;

    ComPtr<IPolicyConfig> policy;
    if (FAILED(CoCreateInstance(kPolicyConfigClient, nullptr, CLSCTX_ALL, kIPolicyConfig, (void**)&policy)))
        return false;

    const std::wstring id = toWide(endpointId);
    bool ok = true;
    for (ERole role : { eConsole, eMultimedia, eCommunications })
        ok = SUCCEEDED(policy->SetDefaultEndpoint(id.c_str(), role)) && ok;
    return ok;
}

bool isProcessElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    TOKEN_ELEVATION elevation {};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) != FALSE;
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

bool relaunchSelfElevated()
{
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0)
        return false;

    SHELLEXECUTEINFOW info {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";   // UAC prompt
    info.lpFile = path;
    info.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&info) != FALSE;   // FALSE also when the user declines
}

} // namespace eeval::winaudio

#else // !_WIN32

namespace eeval::winaudio {
std::vector<Endpoint> listEndpoints(Flow) { return {}; }
std::optional<Endpoint> getDefaultEndpoint(Flow) { return std::nullopt; }
bool setDefaultEndpoint(const std::string&) { return false; }
bool isProcessElevated() { return false; }
bool relaunchSelfElevated() { return false; }
}

#endif
