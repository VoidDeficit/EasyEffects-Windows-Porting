#include "SingleInstance.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <atomic>
#include <thread>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

namespace eeval {
namespace app {

// Session-local names (one EasyEffects per signed-in user session).
static const wchar_t* lockName = L"Local\\EasyEffectsForWindows.SingleInstance";
static const wchar_t* showName = L"Local\\EasyEffectsForWindows.ShowWindow";

static HANDLE lockHandle = nullptr;
static HANDLE showEvent  = nullptr;
static HANDLE stopEvent  = nullptr;
static std::thread listener;

// Everyone may use the objects, and they carry a low integrity label, so a
// normal copy can still see and signal objects made by an administrator copy.
struct OpenSecurity {
    SECURITY_ATTRIBUTES sa {};
    PSECURITY_DESCRIPTOR sd = nullptr;
    OpenSecurity() {
        sa.nLength = sizeof(sa);
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;WD)S:(ML;;NW;;;LW)",
                                                                 SDDL_REVISION_1, &sd, nullptr))
            sa.lpSecurityDescriptor = sd;
    }
    ~OpenSecurity() { if (sd != nullptr) LocalFree(sd); }
};

bool acquireSingleInstance()
{
    if (lockHandle != nullptr)
        return true;

    OpenSecurity sec;
    HANDLE h = CreateMutexW(&sec.sa, FALSE, lockName);
    const DWORD err = GetLastError();
    if (h == nullptr)
        return err != ERROR_ACCESS_DENIED;   // can't see it = someone else owns it
    if (err == ERROR_ALREADY_EXISTS) {
        CloseHandle(h);
        return false;
    }
    lockHandle = h;
    return true;
}

void releaseSingleInstance()
{
    if (lockHandle != nullptr) {
        CloseHandle(lockHandle);
        lockHandle = nullptr;
    }
}

bool requestShowRunningInstance()
{
    HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, showName);
    if (h == nullptr)
        return false;
    const bool ok = SetEvent(h) != FALSE;
    CloseHandle(h);
    return ok;
}

void listenForShowRequests(std::function<void()> onShowRequested)
{
    if (showEvent != nullptr)
        return;
    OpenSecurity sec;
    showEvent = CreateEventW(&sec.sa, FALSE, FALSE, showName);
    stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (showEvent == nullptr || stopEvent == nullptr)
        return;

    listener = std::thread([cb = std::move(onShowRequested)] {
        HANDLE handles[] = { showEvent, stopEvent };
        for (;;) {
            const DWORD r = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            if (r != WAIT_OBJECT_0)
                break;   // stop requested or error
            if (cb) cb();
        }
    });
}

void stopListeningForShowRequests()
{
    if (stopEvent != nullptr)
        SetEvent(stopEvent);
    if (listener.joinable())
        listener.join();
    if (showEvent != nullptr) { CloseHandle(showEvent); showEvent = nullptr; }
    if (stopEvent != nullptr) { CloseHandle(stopEvent); stopEvent = nullptr; }
}

void showAlreadyRunningMessage(bool windowShown)
{
    const wchar_t* text = windowShown
        ? L"EasyEffects is already open.\n\nIts window has been brought to the front. "
          L"While it runs you also find it in the notification area (system tray)."
        : L"EasyEffects is already open.\n\nLook for its icon in the notification area (system tray) "
          L"and click it to show the window.";
    MessageBoxW(nullptr, text, L"EasyEffects", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
}

} // namespace app
} // namespace eeval
