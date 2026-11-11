#pragma once

#include <JuceHeader.h>

namespace eeval {

/**
 * Runs a Windows Audio (WASAPI) capture device as an input source for the ASIO
 * engine (AsioHost).
 *
 * Why: most ASIO drivers only expose inputs for the hardware connected to the
 * ASIO interface. Microphones that Windows can see but the ASIO driver cannot
 * (USB headsets, webcams, Bluetooth mics, ...) therefore could not be used with
 * the ASIO engine. This class captures such a device through WASAPI and hands
 * the audio to the ASIO callback over a lock-free FIFO, so the Input pipeline
 * can process the Windows microphone and write it to any ASIO output pair
 * (e.g. a virtual microphone endpoint of a custom ASIO driver).
 *
 * The ASIO driver and the capture device run on independent clocks:
 *  - if the device cannot run at the ASIO rate, the stream is resampled;
 *  - a small FIFO absorbs clock drift (old audio is dropped when it fills up,
 *    silence is produced when it runs empty).
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

    /** Called on the audio thread: fills dest (1 or 2 channels) with
        numSamples; silence when the FIFO has run dry. */
    void readInto(juce::AudioBuffer<float>& dest, int numSamples);

private:
    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs,
                                          float* const* outputs, int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String& message) override;

    void pushBlock(const juce::AudioBuffer<float>& source, int numSamples) noexcept;

    // All buffers are allocated once so no audio callback ever has to allocate.
    static constexpr int fifoFrames   = 1 << 18;   // 5.4 s at 48 kHz, 1.4 s at 192 kHz
    static constexpr int scratchFrames = 1 << 13;  // 8192 frames per channel

    std::unique_ptr<juce::AudioIODeviceType> deviceType;
    std::unique_ptr<juce::AudioIODevice> device;
    juce::String currentName;

    juce::AudioBuffer<float> fifoBuffer;
    juce::AbstractFifo fifo { fifoFrames };
    juce::AudioBuffer<float> scratch;
    juce::LagrangeInterpolator interpolators[2];

    std::atomic<double> sourceRate { 0.0 }, targetRate { 0.0 };
    std::atomic<bool> running { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WindowsAudioInput)
};

} // namespace eeval
