#pragma once

// Thin wrapper around the Windows Core Audio (MMDevice) API.
// Kept free of JUCE headers so <windows.h> never leaks into JUCE translation units.

#include <optional>
#include <string>
#include <vector>

namespace eeval::winaudio {

enum class Flow { Render, Capture };

struct Endpoint {
    std::string id;     // MMDevice endpoint ID (UTF-8)
    std::string name;   // PKEY_Device_FriendlyName (UTF-8), same string JUCE's WASAPI backend shows
};

// Active endpoints for the given direction.
std::vector<Endpoint> listEndpoints(Flow flow);

// Current default endpoint (eConsole role).
std::optional<Endpoint> getDefaultEndpoint(Flow flow);

// Sets the default endpoint for the console, multimedia and communications roles.
// Uses the (undocumented but long-stable) IPolicyConfig interface, the same one
// the Windows Sound control panel uses. Returns false on failure / non-Windows.
bool setDefaultEndpoint(const std::string& endpointId);

// True when this process runs with administrator rights (UAC elevated).
// Synchronous Audio Router only creates its Windows devices for an elevated host.
bool isProcessElevated();

// Starts another copy of this executable elevated (Windows shows a UAC prompt).
// Returns false if it could not be started or the user declined.
bool relaunchSelfElevated();

} // namespace eeval::winaudio
