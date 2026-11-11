#include "WindowsAudioInput.h"

namespace eeval {

WindowsAudioInput::WindowsAudioInput()
{
    fifoBuffer.setSize(2, fifoFrames, false, true, false);
    fifoBuffer.clear();
    scratch.setSize(2, scratchFrames, false, true, false);
    scratch.clear();
}

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

    const double deviceRate = device->getCurrentSampleRate();
    sourceRate.store (deviceRate > 0.0 ? deviceRate : sampleRate);
    targetRate.store (sampleRate);
    currentName = deviceName;

    interpolators[0].reset();
    interpolators[1].reset();
    fifo.reset();

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
    sourceRate.store (0.0);
    targetRate.store (0.0);
    fifo.reset();
}

void WindowsAudioInput::readInto (juce::AudioBuffer<float>& dest, int numSamples)
{
    dest.clear();

    if (numSamples <= 0 || dest.getNumChannels() <= 0 || ! running.load())
        return;

    const int capacity = fifo.getTotalSize();
    if (capacity <= 0)
        return;

    // Both sides run on their own clock. If the FIFO grows, drop the oldest
    // audio so the latency stays bounded instead of creeping up over time.
    const int ready = fifo.getNumReady();
    if (ready > capacity * 3 / 4) {
        int s1 = 0, n1 = 0, s2 = 0, n2 = 0;
        fifo.prepareToRead (ready - capacity / 2, s1, n1, s2, n2);
        fifo.finishedRead (n1 + n2);
    }

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead (numSamples, start1, size1, start2, size2);

    if (size1 > 0) {
        for (int ch = 0; ch < juce::jmin (2, dest.getNumChannels()); ++ch)
            dest.copyFrom (ch, 0, fifoBuffer.getReadPointer (ch, start1), size1);
    }

    if (size2 > 0) {
        for (int ch = 0; ch < juce::jmin (2, dest.getNumChannels()); ++ch)
            dest.copyFrom (ch, size1, fifoBuffer.getReadPointer (ch, start2), size2);
    }

    fifo.finishedRead (size1 + size2);
}

void WindowsAudioInput::pushBlock (const juce::AudioBuffer<float>& source, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    const int capacity = fifo.getTotalSize();
    if (capacity <= 0)
        return;

    if (fifo.getNumReady() + numSamples > capacity) {
        // The consumer stalled (e.g. the ASIO driver is not running yet):
        // restart with the newest audio instead of keeping stale samples.
        fifo.reset();
        if (numSamples > capacity)
            return;
    }

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

    const int sourceChannels = source.getNumChannels();
    if (sourceChannels <= 0)
        return;

    for (int ch = 0; ch < juce::jmin (2, fifoBuffer.getNumChannels()); ++ch) {
        const auto* in = source.getReadPointer (juce::jmin (ch, sourceChannels - 1));

        if (size1 > 0)
            fifoBuffer.copyFrom (ch, start1, in, size1);
        if (size2 > 0)
            fifoBuffer.copyFrom (ch, start1 + size1, in + size1, size2);
    }

    fifo.finishedWrite (size1 + size2);
}

// --- AudioIODeviceCallback -----------------------------------------------------

void WindowsAudioInput::audioDeviceAboutToStart (juce::AudioIODevice*)
{
    interpolators[0].reset();
    interpolators[1].reset();
}

void WindowsAudioInput::audioDeviceStopped()
{
}

void WindowsAudioInput::audioDeviceError (const juce::String&)
{
}

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

    const double inRate  = sourceRate.load();
    const double outRate = targetRate.load();
    const bool needsResample = inRate > 0.0 && outRate > 0.0
                               && std::abs (inRate - outRate) > 1.0;

    // floor: the interpolator must not read past the end of the capture block
    int outSamples = needsResample ? (int) std::floor (numSamples * outRate / inRate)
                                   : numSamples;
    outSamples = juce::jmin (outSamples, scratch.getNumSamples());
    if (outSamples <= 0)
        return;

    const float* srcLeft  = inputs[first];
    const float* srcRight = inputs[second >= 0 ? second : first];

    if (needsResample) {
        const double ratio = inRate / outRate;
        interpolators[0].process (ratio, srcLeft,  scratch.getWritePointer (0), outSamples);
        interpolators[1].process (ratio, srcRight, scratch.getWritePointer (1), outSamples);
    } else {
        juce::FloatVectorOperations::copy (scratch.getWritePointer (0), srcLeft,  outSamples);
        juce::FloatVectorOperations::copy (scratch.getWritePointer (1), srcRight, outSamples);
    }

    pushBlock (scratch, outSamples);
}

} // namespace eeval
