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
    /** otherPipelineOutput: the output device of the other (Input) pipeline. If
        that pipeline already sends into VB-Cable, this one won't listen to the
        same cable on first run (that would play your microphone back to you). */
    SecondaryPipeline(juce::PropertySet* settings, const juce::String& key,
                      const juce::String& otherPipelineOutput = {});

    /** True when the two pipelines are wired through the same VB-Cable
        (Input pipeline -> "CABLE Input", Output pipeline <- "CABLE Output"). */
    static bool sharesCableWith(const juce::String& thisInput, const juce::String& otherOutput) {
        return thisInput.startsWithIgnoreCase("CABLE Output") && otherOutput.startsWithIgnoreCase("CABLE Input");
    }
    ~SecondaryPipeline();

    EasyEffectsAudioProcessor& processor() noexcept      { return *proc; }
    juce::AudioDeviceManager& deviceManager() noexcept   { return devices; }

    /** Writes the chain + device setup into the settings. */
    void save();

private:
    void openDevices();

    juce::PropertySet* settings;
    juce::String key;
    juce::String otherOutput;

    std::unique_ptr<EasyEffectsAudioProcessor> proc;
    juce::AudioDeviceManager devices;
    juce::AudioProcessorPlayer player;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SecondaryPipeline)
};

} // namespace eeval
