#pragma once

#include <JuceHeader.h>

namespace eeval::sar {

/**
 * Automatic configuration of Synchronous Audio Router (SAR) so EasyEffects can
 * create the Windows devices it needs without the user touching SAR's panel.
 *
 * SAR reads %APPDATA%\SynchronousAudioRouter\default.json when its ASIO driver
 * object is created (see SarAsioWrapper's constructor), so this must run
 * BEFORE the driver is opened. Format (SarAsio/config.cpp):
 *   { "driverClsid": "{...}",            // hardware ASIO driver SAR wraps
 *     "enableApplicationRouting": false,
 *     "endpoints": [ { "id": "ep_N", "description": "...",
 *                      "type": "playback" | "recording", "channelCount": 2 } ] }
 *
 * Virtual channel names are "<description> 1", "<description> 2"; playback
 * endpoints appear as ASIO *inputs*, recording endpoints as ASIO *outputs*,
 * after the hardware channels.
 */

// Endpoints EasyEffects creates (keep descriptions < 30 chars: ASIO names are 32).
constexpr const char* speakersEndpoint   = "EasyEffects Speakers";    // apps -> Output tab
constexpr const char* microphoneEndpoint = "EasyEffects Microphone";  // Input tab -> Discord/OBS
constexpr const char* speakersId   = "ep_100";
constexpr const char* microphoneId = "ep_101";

constexpr const char* driverName = "Synchronous Audio Router";

inline bool isSarDriver(const juce::String& name) { return name.containsIgnoreCase(driverName); }

/** %APPDATA%\SynchronousAudioRouter\default.json */
juce::File configFile();

/** CLSID of an installed ASIO driver (from HKLM\SOFTWARE\ASIO\<name>\CLSID), or {}. */
juce::String clsidOfAsioDriver(const juce::String& name);

struct Result {
    bool changed = false;         // the file was written
    bool endpointsAdded = false;  // our endpoints were newly created
    juce::String hardware;        // name of the hardware driver SAR wraps
    juce::String error;
};

/**
 * Makes SAR's config contain a valid hardware driver and both EasyEffects
 * endpoints, keeping everything else (the user's own endpoints, app routing).
 * `asioDrivers` = installed ASIO driver names; `preferredHardware` = the
 * user's last choice (may be empty: then the current one, else the first).
 */
Result ensureConfig(const juce::StringArray& asioDrivers, const juce::String& preferredHardware);

} // namespace eeval::sar
