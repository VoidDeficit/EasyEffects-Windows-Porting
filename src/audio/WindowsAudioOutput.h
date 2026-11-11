#pragma once

#include <JuceHeader.h>

namespace eeval {

/**
 * Plays audio produced by the ASIO engine (AsioHost) on a regular Windows
 * Audio (WASAPI) output device.
 *
 * This is the counterpart of WindowsAudioInput: it lets the Output pipeline go
 * from an ASIO input pair, through its effects chain, out to the speakers or
 * headphones Windows knows about (ASIO driver -> effects -> Windows device).
 *
 * The ASIO driver and the playback device run on independent clocks:
 *  - the stream is resampled if WASAPI cannot run at the ASIO rate;
 *  - a FIFO absorbs clock drift (old audio is dropped when it fills up,
 *    silence is played when it runs empty).
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

    /** Called on the ASIO thread: queues processed audio for playback. */
    void writeFrom(const juce::AudioBuffer<float>& source, int numSamples);

private:
    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs,
                                          float* const* outputs, int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String& message) override;

    /** Audio thread: returns how many frames were copied into dest. */
    int pullBlock(juce::AudioBuffer<float>& dest, int numSamples) noexcept;

    static constexpr int fifoFrames    = 1 << 18;
    static constexpr int scratchFrames = 1 << 13;

    std::unique_ptr<juce::AudioIODeviceType> deviceType;
    std::unique_ptr<juce::AudioIODevice> device;
    juce::String currentName;

    juce::AudioBuffer<float> fifoBuffer;
    juce::AbstractFifo fifo { fifoFrames };
    juce::AudioBuffer<float> scratch;
    juce::LagrangeInterpolator interpolators[2];

    std::atomic<double> sourceRate { 0.0 }, targetRate { 0.0 };
    std::atomic<bool> running { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WindowsAudioOutput)
};

} // namespace eeval
