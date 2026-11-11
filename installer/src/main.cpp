/*
 * EEVirtualAudioSetup.exe - installs / removes the EasyEffects Virtual Audio driver.
 *
 *   EEVirtualAudioSetup.exe              install (or update) the driver
 *   EEVirtualAudioSetup.exe /uninstall   remove the device and the driver package
 *   EEVirtualAudioSetup.exe /status      show driver / signing / Secure Boot state
 *   options: /quiet  (no "press Enter" at the end)
 *            /force  (install a test-signed build even if Windows won't load it)
 *
 * The driver package (.sys/.inf/.cat, plus the test certificate for test-signed
 * builds) is embedded as RCDATA resources, extracted to a temp folder and
 * installed with the same SetupAPI calls devcon uses.
 *
 * Exit codes: 0 = success, 3010 = success but reboot required, 1 = failure,
 *             2 = refused (test-signed build on a system that cannot load it).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
 #define NOMINMAX
#endif

#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <cfgmgr32.h>
#include <wincrypt.h>

#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

#include "resource.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "newdev.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

constexpr wchar_t kHardwareId[] = L"ROOT\\EasyEffectsVirtualAudio";
constexpr wchar_t kInfName[]    = L"EEVirtualAudio.inf";
constexpr wchar_t kSysName[]    = L"EEVirtualAudio.sys";
constexpr wchar_t kCatName[]    = L"EEVirtualAudio.cat";

constexpr int kExitOk       = 0;
constexpr int kExitFailed   = 1;
constexpr int kExitRefused  = 2;
constexpr int kExitReboot   = 3010;

#ifdef EEVAD_TEST_SIGNED
constexpr bool kTestSigned = true;
#else
constexpr bool kTestSigned = false;
#endif

//------------------------------------------------------------------------------
// Output helpers
//------------------------------------------------------------------------------

void say(const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfwprintf(stdout, fmt, args);
    va_end(args);
    fputwc(L'\n', stdout);
    fflush(stdout);
}

std::wstring errorText(DWORD code)
{
    wchar_t* buf = nullptr;
    const DWORD len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, code, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring text = len ? std::wstring(buf, len) : L"Unknown error";
    if (buf) LocalFree(buf);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' '))
        text.pop_back();

    wchar_t code_buf[32];
    swprintf(code_buf, 32, L" (0x%08lX)", code);
    return text + code_buf;
}

void fail(const wchar_t* what, DWORD code = GetLastError())
{
    say(L"ERROR: %ls: %ls", what, errorText(code).c_str());
}

//------------------------------------------------------------------------------
// System state
//------------------------------------------------------------------------------

bool isSecureBootEnabled()
{
    DWORD value = 0, size = sizeof(value);
    return RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State",
                        L"UEFISecureBootEnabled", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS
        && value != 0;
}

bool isTestSigningEnabled()
{
    struct CodeIntegrityInfo { ULONG Length; ULONG CodeIntegrityOptions; };
    using NtQuerySystemInformationFn = LONG (WINAPI*)(ULONG, PVOID, ULONG, PULONG);

    auto query = reinterpret_cast<NtQuerySystemInformationFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
    if (!query)
        return false;

    CodeIntegrityInfo info { sizeof(CodeIntegrityInfo), 0 };
    constexpr ULONG SystemCodeIntegrityInformation = 103;
    constexpr ULONG CODEINTEGRITY_OPTION_TESTSIGN = 0x02;

    return query(SystemCodeIntegrityInformation, &info, sizeof(info), nullptr) >= 0
        && (info.CodeIntegrityOptions & CODEINTEGRITY_OPTION_TESTSIGN) != 0;
}

//------------------------------------------------------------------------------
// Embedded package
//------------------------------------------------------------------------------

bool loadResource(int id, const BYTE*& data, DWORD& size)
{
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (!res) return false;
    HGLOBAL handle = LoadResource(nullptr, res);
    if (!handle) return false;
    data = static_cast<const BYTE*>(LockResource(handle));
    size = SizeofResource(nullptr, res);
    return data != nullptr && size > 0;
}

bool writeFile(const std::wstring& path, const BYTE* data, DWORD size)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, data, size, &written, nullptr) && written == size;
    CloseHandle(file);
    return ok;
}

struct ExtractedPackage
{
    std::wstring dir;
    std::wstring inf;

    ~ExtractedPackage()
    {
        if (dir.empty()) return;
        for (const wchar_t* name : { kInfName, kSysName, kCatName })
            DeleteFileW((dir + L"\\" + name).c_str());
        RemoveDirectoryW(dir.c_str());
    }
};

bool extractPackage(ExtractedPackage& pkg)
{
    wchar_t temp[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temp)) { fail(L"GetTempPath"); return false; }

    wchar_t dir[MAX_PATH];
    swprintf(dir, MAX_PATH, L"%lsEEVirtualAudioSetup-%lu", temp, GetCurrentProcessId());
    if (!CreateDirectoryW(dir, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        fail(L"Creating temp folder");
        return false;
    }
    pkg.dir = dir;

    const struct { int id; const wchar_t* name; } files[] = {
        { IDR_EEVAD_SYS, kSysName }, { IDR_EEVAD_INF, kInfName }, { IDR_EEVAD_CAT, kCatName },
    };

    for (const auto& f : files) {
        const BYTE* data = nullptr; DWORD size = 0;
        if (!loadResource(f.id, data, size)) {
            say(L"ERROR: driver file %ls is missing from this setup program.", f.name);
            return false;
        }
        if (!writeFile(pkg.dir + L"\\" + f.name, data, size)) {
            fail(L"Extracting driver files");
            return false;
        }
    }

    pkg.inf = pkg.dir + L"\\" + kInfName;
    return true;
}

bool trustTestCertificate()
{
    const BYTE* data = nullptr; DWORD size = 0;
    if (!loadResource(IDR_EEVAD_CER, data, size))
        return true;   // production build: nothing to trust

    for (const wchar_t* storeName : { L"Root", L"TrustedPublisher" }) {
        HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                         CERT_SYSTEM_STORE_LOCAL_MACHINE, storeName);
        if (!store) { fail(L"Opening certificate store"); return false; }

        const BOOL ok = CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                         data, size, CERT_STORE_ADD_REPLACE_EXISTING, nullptr);
        const DWORD err = GetLastError();
        CertCloseStore(store, 0);
        if (!ok) { fail(L"Adding test certificate", err); return false; }
    }
    say(L"Trusted the driver's test certificate (LocalMachine Root + TrustedPublisher).");
    return true;
}

//------------------------------------------------------------------------------
// Device enumeration
//------------------------------------------------------------------------------

bool hasOurHardwareId(HDEVINFO set, SP_DEVINFO_DATA& dev)
{
    wchar_t buf[1024] = {};
    DWORD type = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(set, &dev, SPDRP_HARDWAREID, &type,
                                           reinterpret_cast<BYTE*>(buf), sizeof(buf) - 2 * sizeof(wchar_t), nullptr))
        return false;

    for (const wchar_t* p = buf; *p; p += wcslen(p) + 1)
        if (_wcsicmp(p, kHardwareId) == 0)
            return true;
    return false;
}

// Calls fn(set, dev) for every (present or phantom) device with our hardware ID.
template <typename Fn>
int forEachOurDevice(Fn fn)
{
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"ROOT", nullptr, DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE)
        return 0;

    int count = 0;
    SP_DEVINFO_DATA dev { sizeof(SP_DEVINFO_DATA) };
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &dev); ++i) {
        if (hasOurHardwareId(set, dev)) {
            ++count;
            fn(set, dev);
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return count;
}

// Returns a human-readable device state, e.g. "running" or "problem 52 (unsigned driver)".
std::wstring deviceState(SP_DEVINFO_DATA& dev)
{
    ULONG status = 0, problem = 0;
    if (CM_Get_DevNode_Status(&status, &problem, dev.DevInst, 0) != CR_SUCCESS)
        return L"not present";
    if (status & DN_HAS_PROBLEM) {
        wchar_t buf[128];
        const wchar_t* hint = problem == CM_PROB_UNSIGNED_DRIVER ? L" (Windows refused the driver signature)"
                            : problem == CM_PROB_NEED_RESTART    ? L" (restart required)"
                            : problem == CM_PROB_FAILED_START    ? L" (driver failed to start)"
                                                                 : L"";
        swprintf(buf, 128, L"problem code %lu%ls", problem, hint);
        return buf;
    }
    return (status & DN_STARTED) ? L"running" : L"installed, not started";
}

//------------------------------------------------------------------------------
// Install / uninstall
//------------------------------------------------------------------------------

bool createRootDevice(const std::wstring& inf)
{
    GUID classGuid;
    wchar_t className[MAX_CLASS_NAME_LEN];
    if (!SetupDiGetINFClassW(inf.c_str(), &classGuid, className, MAX_CLASS_NAME_LEN, nullptr)) {
        fail(L"Reading INF class");
        return false;
    }

    HDEVINFO set = SetupDiCreateDeviceInfoList(&classGuid, nullptr);
    if (set == INVALID_HANDLE_VALUE) { fail(L"SetupDiCreateDeviceInfoList"); return false; }

    SP_DEVINFO_DATA dev { sizeof(SP_DEVINFO_DATA) };
    bool ok = SetupDiCreateDeviceInfoW(set, className, &classGuid, nullptr, nullptr, DICD_GENERATE_ID, &dev) != FALSE;
    if (!ok) fail(L"Creating device node");

    if (ok) {
        // REG_MULTI_SZ: the ID followed by two terminating NULs.
        std::vector<wchar_t> hwid(kHardwareId, kHardwareId + wcslen(kHardwareId));
        hwid.push_back(L'\0');
        hwid.push_back(L'\0');
        ok = SetupDiSetDeviceRegistryPropertyW(set, &dev, SPDRP_HARDWAREID,
                                               reinterpret_cast<const BYTE*>(hwid.data()),
                                               static_cast<DWORD>(hwid.size() * sizeof(wchar_t))) != FALSE;
        if (!ok) fail(L"Setting hardware ID");
    }

    if (ok) {
        ok = SetupDiCallClassInstaller(DIF_REGISTERDEVICE, set, &dev) != FALSE;
        if (!ok) fail(L"Registering device");
    }

    SetupDiDestroyDeviceInfoList(set);
    return ok;
}

int install(bool force)
{
    const bool secureBoot = isSecureBootEnabled();
    const bool testSigning = isTestSigningEnabled();

    say(L"EasyEffects Virtual Audio driver setup");
    say(L"  Build:        %ls", kTestSigned ? L"test-signed (development)" : L"release (Microsoft-signed package)");
    say(L"  Secure Boot:  %ls", secureBoot ? L"ON" : L"off");
    say(L"  Test signing: %ls", testSigning ? L"ON" : L"off");
    say(L"");

    if (kTestSigned && !testSigning && !force) {
        say(L"This is a TEST-SIGNED build. Windows will not load it in the current configuration.");
        if (secureBoot) {
            say(L"Secure Boot is on, so only Microsoft-signed kernel drivers can load. Either:");
            say(L"  * install a release build of this setup (driver attestation-signed by Microsoft), or");
            say(L"  * for development: disable Secure Boot in your UEFI settings, then run");
            say(L"      bcdedit /set testsigning on");
            say(L"    from an Administrator prompt and reboot.");
        } else {
            say(L"Enable test signing from an Administrator prompt and reboot:");
            say(L"    bcdedit /set testsigning on");
        }
        say(L"See driver\\README.md (\"Secure Boot\") for how to produce a Microsoft-signed build.");
        say(L"Run with /force to install anyway.");
        return kExitRefused;
    }

    ExtractedPackage pkg;
    if (!extractPackage(pkg))
        return kExitFailed;

    if (kTestSigned && !trustTestCertificate())
        return kExitFailed;

    const int existing = forEachOurDevice([](HDEVINFO, SP_DEVINFO_DATA&) {});
    if (existing == 0) {
        say(L"Creating device %ls ...", kHardwareId);
        if (!createRootDevice(pkg.inf))
            return kExitFailed;
    } else {
        say(L"Device already exists, updating its driver ...");
    }

    BOOL reboot = FALSE;
    if (!UpdateDriverForPlugAndPlayDevicesW(nullptr, kHardwareId, pkg.inf.c_str(), INSTALLFLAG_FORCE, &reboot)) {
        const DWORD err = GetLastError();
        fail(L"Installing driver", err);
        if (existing == 0) {
            // Don't leave a driverless phantom device behind.
            forEachOurDevice([](HDEVINFO set, SP_DEVINFO_DATA& dev) {
                SetupDiCallClassInstaller(DIF_REMOVE, set, &dev);
            });
        }
        if (err == ERROR_INVALID_IMAGE_HASH || err == TRUST_E_NOSIGNATURE || err == static_cast<DWORD>(TRUST_E_BAD_DIGEST))
            say(L"Windows rejected the driver signature. Test-signed builds need test signing mode and Secure Boot off.");
        return kExitFailed;
    }

    bool problem = false;
    forEachOurDevice([&](HDEVINFO, SP_DEVINFO_DATA& dev) {
        const auto state = deviceState(dev);
        say(L"Device state: %ls", state.c_str());
        problem = problem || state.rfind(L"problem", 0) == 0;
    });

    say(L"");
    say(L"Installed. New audio devices:");
    say(L"  Playback:  EasyEffects Speakers, EasyEffects Mic Feed");
    say(L"  Recording: EasyEffects Speakers Monitor, EasyEffects Microphone");
    say(L"Open EasyEffects and choose a mode with the 'Virtual Audio' button.");

    if (reboot) {
        say(L"A restart is required to finish the installation.");
        return kExitReboot;
    }
    return problem ? kExitFailed : kExitOk;
}

bool infMentionsOurDevice(const std::wstring& path)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    std::vector<char> data(64 * 1024);
    DWORD read = 0;
    ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr);
    CloseHandle(file);
    data.resize(read);

    // ANSI and UTF-16LE spellings of the hardware ID.
    const std::string ansi = "EasyEffectsVirtualAudio";
    std::string wide;
    for (char c : ansi) { wide.push_back(c); wide.push_back('\0'); }

    const std::string text(data.begin(), data.end());
    return text.find(ansi) != std::string::npos || text.find(wide) != std::string::npos;
}

int uninstall()
{
    bool reboot = false;

    const int removed = forEachOurDevice([&](HDEVINFO set, SP_DEVINFO_DATA& dev) {
        BOOL needReboot = FALSE;
        if (DiUninstallDevice(nullptr, set, &dev, 0, &needReboot))
            say(L"Removed device.");
        else
            fail(L"Removing device");
        reboot = reboot || needReboot;
    });
    if (removed == 0)
        say(L"No EasyEffects Virtual Audio device found.");

    // Remove our driver package(s) from the driver store.
    wchar_t windir[MAX_PATH];
    GetWindowsDirectoryW(windir, MAX_PATH);
    const std::wstring infDir = std::wstring(windir) + L"\\INF\\";

    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((infDir + L"oem*.inf").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (infMentionsOurDevice(infDir + fd.cFileName)) {
                if (SetupUninstallOEMInfW(fd.cFileName, SUOI_FORCEDELETE, nullptr))
                    say(L"Removed driver package %ls.", fd.cFileName);
                else
                    fail(L"Removing driver package");
            }
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }

    say(L"EasyEffects Virtual Audio removed.");
    if (reboot) {
        say(L"A restart is required to finish removal.");
        return kExitReboot;
    }
    return kExitOk;
}

int status()
{
    say(L"Build:        %ls", kTestSigned ? L"test-signed (development)" : L"release (Microsoft-signed package)");
    say(L"Secure Boot:  %ls", isSecureBootEnabled() ? L"ON" : L"off");
    say(L"Test signing: %ls", isTestSigningEnabled() ? L"ON" : L"off");

    const int count = forEachOurDevice([](HDEVINFO, SP_DEVINFO_DATA& dev) {
        say(L"Device:       %ls", deviceState(dev).c_str());
    });
    if (count == 0)
        say(L"Device:       not installed");
    return kExitOk;
}

bool hasArg(int argc, wchar_t** argv, const wchar_t* name)
{
    for (int i = 1; i < argc; ++i) {
        const wchar_t* a = argv[i];
        if (*a == L'/' || *a == L'-') ++a;
        if (*a == L'-') ++a;
        if (_wcsicmp(a, name) == 0)
            return true;
    }
    return false;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    const bool quiet = hasArg(argc, argv, L"quiet");

    int rc;
    if (hasArg(argc, argv, L"uninstall"))
        rc = uninstall();
    else if (hasArg(argc, argv, L"status"))
        rc = status();
    else
        rc = install(hasArg(argc, argv, L"force"));

    // Keep the window open when launched by double-click.
    if (!quiet) {
        DWORD processes[2];
        if (GetConsoleProcessList(processes, 2) <= 1) {
            say(L"\nPress Enter to close.");
            (void)getwchar();
        }
    }
    return rc;
}
