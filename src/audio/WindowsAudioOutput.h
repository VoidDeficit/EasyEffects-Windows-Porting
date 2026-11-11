#pragma once

#include <JuceHeader.h>
#include "ClockBridge.h"

namespace eeval {

/**
 * Plays audio produced by the ASIO engine (AsioHost) on a regular Windows
 * Audio (WASAPI) output device.
 *
 * This is the counterpart of WindowsAudioInput: it lets the Output pipeline go
 * from an ASIO input pair, through its effects chain, out to the speakers or
 * headphones Windows knows about (ASIO driver -> effects -> Windows device).
 *
 * The ASIO driver and the playback device run on independent clocks. A
 * ClockBridge resamples between them and continuously cancels the drift, so
 * the delay stays small and constant instead of growing over time.
 */
class WindowsAudioOutput : private juce::AudioIODeviceCallback
{
public:
    WindowsAudioOutput();
    ~WindowsAudioOutput() override;

    /** Playback device names from the Windows Audio backend. */
    static juce::StringArray getDeviceNames();

    /** Opens and starts the named output device, fed at sampleRate.
        Returns an empty string on success, otherwise an error message. */
    juce::String start(const juce::String& deviceName, double sampleRate);
    void stop();

    bool isRunning() const noexcept { return running.load(); }
    juce::String getDeviceName() const { return currentName; }

    /** Buffered audio between ASIO and the device, in milliseconds. */
    double getBufferedMs() const noexcept { return bridge.getBufferedMs(); }

    /** Called on the ASIO thread: queues processed audio for playback. */
    void writeFrom(const juce::AudioBuffer<float>& source, int numSamples);

private:
    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs,
                                          float* const* outputs, int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String& message) override;

    std::unique_ptr<juce::AudioIODeviceType> deviceType;
    std::unique_ptr<juce::AudioIODevice> device;
    juce::String currentName;

    ClockBridge bridge;
    std::atomic<bool> running { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WindowsAudioOutput)
};

} // namespace eeval
