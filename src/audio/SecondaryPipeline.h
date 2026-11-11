#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"

namespace eeval {

/**
 * A second, fully independent processing pipeline for the standalone app:
 * its own EasyEffectsAudioProcessor (effect chain, parameters, spectrum),
 * its own AudioDeviceManager (input + output device) and its own player.
 *
 * EasyEffects has an "Output" pipeline (what your apps play) and an "Input"
 * pipeline (your microphone) running at the same time. The JUCE standalone
 * holder already owns one pipeline; this class provides the other.
 *
 * Its chain and device choice are stored in the app settings under `key`.
 */
class SecondaryPipeline
{
public:
    SecondaryPipeline(juce::PropertySet* settings, const juce::String& key);
    ~SecondaryPipeline();

    EasyEffectsAudioProcessor& processor() noexcept      { return *proc; }
    juce::AudioDeviceManager& deviceManager() noexcept   { return devices; }

    /** Writes the chain + device setup into the settings. */
    void save();

private:
    void openDevices();

    juce::PropertySet* settings;
    juce::String key;

    std::unique_ptr<EasyEffectsAudioProcessor> proc;
    juce::AudioDeviceManager devices;
    juce::AudioProcessorPlayer player;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SecondaryPipeline)
};

} // namespace eeval
