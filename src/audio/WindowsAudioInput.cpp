#include "WindowsAudioInput.h"

namespace eeval {

WindowsAudioInput::WindowsAudioInput() = default;

WindowsAudioInput::~WindowsAudioInput()
{
    stop();
}

juce::StringArray WindowsAudioInput::getDeviceNames()
{
    juce::StringArray names;

   #if JUCE_WINDOWS
    std::unique_ptr<juce::AudioIODeviceType> type(
        juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared));

    if (type != nullptr) {
        type->scanForDevices();
        names = type->getDeviceNames (true);
    }
   #endif

    return names;
}

juce::String WindowsAudioInput::start (const juce::String& deviceName, double sampleRate)
{
    stop();

    if (deviceName.isEmpty() || sampleRate <= 0.0)
        return "No Windows Audio input device was selected.";

   #if JUCE_WINDOWS
    deviceType.reset (juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared));
    if (deviceType == nullptr)
        return "Windows Audio is not available in this build.";

    deviceType->scanForDevices();
    if (! deviceType->getDeviceNames (true).contains (deviceName))
        return "The Windows Audio input \"" + deviceName + "\" is not available.";

    device.reset (deviceType->createDevice ({}, deviceName));
    if (device == nullptr)
        return "Could not open the Windows Audio input \"" + deviceName + "\".";

    const int numChannels = juce::jmin (2, device->getInputChannelNames().size());
    if (numChannels <= 0) {
        device.reset();
        return "\"" + deviceName + "\" has no input channels.";
    }

    juce::BigInteger inputMask;
    inputMask.setRange (0, numChannels, true);

    const auto error = device->open (inputMask, juce::BigInteger(), sampleRate,
                                     device->getDefaultBufferSize());
    if (error.isNotEmpty()) {
        device.reset();
        return error;
    }

    // The microphone captures at its own rate and clock; the ASIO engine reads
    // at sampleRate. The bridge resamples and cancels the drift.
    const double deviceRate = device->getCurrentSampleRate();
    bridge.prepare (deviceRate > 0.0 ? deviceRate : sampleRate, sampleRate);
    currentName = deviceName;

    device->start (this);
    running.store (true);
    return {};
   #else
    juce::ignoreUnused (deviceName, sampleRate);
    return "Windows Audio capture is only available on Windows.";
   #endif
}

void WindowsAudioInput::stop()
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

void WindowsAudioInput::readInto (juce::AudioBuffer<float>& dest, int numSamples)
{
    if (numSamples <= 0 || dest.getNumChannels() <= 0)
        return;

    if (! running.load()) {
        dest.clear();
        return;
    }

    bridge.read (dest.getArrayOfWritePointers(), juce::jmin (2, dest.getNumChannels()), numSamples);
}

// --- AudioIODeviceCallback -----------------------------------------------------

void WindowsAudioInput::audioDeviceAboutToStart (juce::AudioIODevice*) {}
void WindowsAudioInput::audioDeviceStopped() {}
void WindowsAudioInput::audioDeviceError (const juce::String&) {}

void WindowsAudioInput::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                          float* const*, int, int numSamples,
                                                          const juce::AudioIODeviceCallbackContext&)
{
    if (inputs == nullptr || numSamples <= 0)
        return;

    // Use the first two channels the device delivers; mono sources are copied
    // to both sides so the pipeline always gets a stereo input.
    int first = -1, second = -1;
    for (int ch = 0; ch < numInputs; ++ch) {
        if (inputs[ch] == nullptr)
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

    const float* stereo[2] = { inputs[first], inputs[second >= 0 ? second : first] };
    bridge.write (stereo, 2, numSamples);
}

} // namespace eeval
