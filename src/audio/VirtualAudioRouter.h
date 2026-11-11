#pragma once

#include <JuceHeader.h>

namespace eeval {

/**
 * Routes the standalone app through the built-in EasyEffects Virtual Audio
 * driver (driver/), replacing the manual VB-Cable setup.
 *
 *  SystemAudio:  Windows default output -> "EasyEffects Speakers"
 *                app input  = "EasyEffects Speakers Monitor"
 *                app output = your real speakers / headphones
 *
 *  Microphone:   app input  = your real microphone
 *                app output = "EasyEffects Mic Feed"
 *                Windows default input -> "EasyEffects Microphone"
 *                (Discord / OBS / Zoom pick up the processed voice)
 *
 * The previous Windows default devices are remembered in the app settings and
 * restored when the mode is turned off or the router is destroyed.
 */
class VirtualAudioRouter
{
public:
    enum class Mode { Off = 0, SystemAudio = 1, Microphone = 2 };

    // Endpoint names created by EEVirtualAudio.inf.
    static constexpr const char* speakersName        = "EasyEffects Speakers";
    static constexpr const char* speakersMonitorName = "EasyEffects Speakers Monitor";
    static constexpr const char* micFeedName         = "EasyEffects Mic Feed";
    static constexpr const char* microphoneName      = "EasyEffects Microphone";

    /** settings: the standalone holder's PropertySet (may be null; then nothing is persisted). */
    VirtualAudioRouter(juce::AudioDeviceManager& deviceManager, juce::PropertySet* settings);
    ~VirtualAudioRouter();

    /** True when all four virtual endpoints are present. */
    bool isDriverInstalled();

    Mode getMode() const noexcept { return mode; }

    /** Switches routing. Returns an empty string on success, otherwise an error message. */
    juce::String setMode(Mode newMode);

    /** Re-applies the mode stored in the settings (call once at startup). */
    juce::String restoreSavedMode();

    /** Real (non-virtual) devices the user can choose from. */
    juce::StringArray getPhysicalOutputs();
    juce::StringArray getPhysicalInputs();

    juce::String getPhysicalOutput() const { return physicalOutput; }
    juce::String getPhysicalInput() const  { return physicalInput; }

    /** Changes the real device used by the current/next mode (re-applies if active). */
    juce::String setPhysicalOutput(const juce::String& name);
    juce::String setPhysicalInput(const juce::String& name);

    /** True for any endpoint created by the EasyEffects driver. */
    static bool isVirtualDeviceName(const juce::String& deviceName);

    /** Mode labels for UI. */
    static juce::String getModeName(Mode m);

private:
    juce::AudioIODeviceType* getWasapiType();
    juce::String findDevice(bool isInput, const juce::String& endpointName);
    juce::String pickDefaultPhysical(bool isInput);
    juce::String applyDeviceSetup(const juce::String& input, const juce::String& output);

    void switchWindowsDefault(bool capture, const juce::String& endpointName);
    void restoreWindowsDefaults();

    void saveSettings();
    void flushSettings();

    juce::AudioDeviceManager& deviceManager;
    juce::PropertySet* settings;

    Mode mode = Mode::Off;
    juce::String physicalOutput;
    juce::String physicalInput;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VirtualAudioRouter)
};

} // namespace eeval
