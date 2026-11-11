#include "WindowsAudioOutput.h"

namespace eeval {

WindowsAudioOutput::WindowsAudioOutput() = default;

WindowsAudioOutput::~WindowsAudioOutput()
{
    stop();
}

juce::StringArray WindowsAudioOutput::getDeviceNames()
{
    juce::StringArray names;

   #if JUCE_WINDOWS
    std::unique_ptr<juce::AudioIODeviceType> type(
        juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared));

    if (type != nullptr) {
        type->scanForDevices();
        names = type->getDeviceNames (false);
    }
   #endif

    return names;
}

juce::String WindowsAudioOutput::start (const juce::String& deviceName, double sampleRate)
{
    stop();

    if (deviceName.isEmpty() || sampleRate <= 0.0)
        return "No Windows Audio output device was selected.";

   #if JUCE_WINDOWS
    deviceType.reset (juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared));
    if (deviceType == nullptr)
        return "Windows Audio is not available in this build.";

    deviceType->scanForDevices();
    if (! deviceType->getDeviceNames (false).contains (deviceName))
        return "The Windows Audio output \"" + deviceName + "\" is not available.";

    device.reset (deviceType->createDevice (deviceName, {}));
    if (device == nullptr)
        return "Could not open the Windows Audio output \"" + deviceName + "\".";

    const int numChannels = juce::jmin (2, device->getOutputChannelNames().size());
    if (numChannels <= 0) {
        device.reset();
        return "\"" + deviceName + "\" has no output channels.";
    }

    juce::BigInteger outputMask;
    outputMask.setRange (0, numChannels, true);

    const auto error = device->open (juce::BigInteger(), outputMask, sampleRate,
                                     device->getDefaultBufferSize());
    if (error.isNotEmpty()) {
        device.reset();
        return error;
    }

    // The ASIO engine produces at sampleRate; the device plays at its own rate
    // and on its own clock. The bridge resamples and cancels the drift.
    const double deviceRate = device->getCurrentSampleRate();
    bridge.prepare (sampleRate, deviceRate > 0.0 ? deviceRate : sampleRate);
    currentName = deviceName;

    device->start (this);
    running.store (true);
    return {};
   #else
    juce::ignoreUnused (deviceName, sampleRate);
    return "Windows Audio playback is only available on Windows.";
   #endif
}

void WindowsAudioOutput::stop()
{
    running.store (false);

    if (device != nullptr) {
        device->stop();
        device->close();
        device.reset();
    }

    deviceType.reset();
    currentName.clear();
}

void WindowsAudioOutput::writeFrom (const juce::AudioBuffer<float>& source, int numSamples)
{
    if (numSamples > 0 && source.getNumChannels() > 0)
        bridge.write (source.getArrayOfReadPointers(), source.getNumChannels(), numSamples);
}

// --- AudioIODeviceCallback -----------------------------------------------------

void WindowsAudioOutput::audioDeviceAboutToStart (juce::AudioIODevice*) {}
void WindowsAudioOutput::audioDeviceStopped() {}
void WindowsAudioOutput::audioDeviceError (const juce::String&) {}

void WindowsAudioOutput::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                           float* const* outputs, int numOutputs,
                                                           int numSamples,
                                                           const juce::AudioIODeviceCallbackContext&)
{
    if (outputs == nullptr || numSamples <= 0)
        return;

    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    // Write to the first two channels the device provides.
    int first = -1, second = -1;
    for (int ch = 0; ch < numOutputs; ++ch) {
        if (outputs[ch] == nullptr)
            continue;
        if (first < 0) {
            first = ch;
        } else {
            second = ch;
            break;
        }
    }

    if (first < 0)
        return;

    float* stereo[2] = { outputs[first], second >= 0 ? outputs[second] : nullptr };
    bridge.read (stereo, second >= 0 ? 2 : 1, numSamples);
}

} // namespace eeval
