#pragma once

#include <JuceHeader.h>
#include "ClockBridge.h"

namespace eeval {

/**
 * Runs a Windows Audio (WASAPI) capture device as an input source for the ASIO
 * engine (AsioHost).
 *
 * Why: most ASIO drivers only expose inputs for the hardware connected to the
 * ASIO interface. Microphones that Windows can see but the ASIO driver cannot
 * (USB headsets, webcams, Bluetooth mics, ...) therefore could not be used with
 * the ASIO engine. This class captures such a device through WASAPI and hands
 * the audio to the ASIO callback, so the Input pipeline can process the Windows
 * microphone and write it to any ASIO output pair (e.g. a virtual microphone).
 *
 * The ASIO driver and the capture device run on independent clocks. A
 * ClockBridge resamples between them and continuously cancels the drift, so
 * the delay stays small and constant instead of growing over time.
 */
class WindowsAudioInput : private juce::AudioIODeviceCallback
{
public:
    WindowsAudioInput();
    ~WindowsAudioInput() override;

    /** Capture device names from the Windows Audio backend. */
    static juce::StringArray getDeviceNames();

    /** Opens and starts the named capture device, converted to sampleRate.
        Returns an empty string on success, otherwise an error message. */
    juce::String start(const juce::String& deviceName, double sampleRate);
    void stop();

    bool isRunning() const noexcept { return running.load(); }
    juce::String getDeviceName() const { return currentName; }

    /** Buffered audio between the device and ASIO, in milliseconds. */
    double getBufferedMs() const noexcept { return bridge.getBufferedMs(); }

    /** Called on the ASIO thread: fills dest (1 or 2 channels) with
        numSamples; silence while the bridge is (re)filling. */
    void readInto(juce::AudioBuffer<float>& dest, int numSamples);

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WindowsAudioInput)
};

} // namespace eeval
