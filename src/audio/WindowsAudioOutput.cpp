#include "WindowsAudioOutput.h"

namespace eeval {

WindowsAudioOutput::WindowsAudioOutput()
{
    fifoBuffer.setSize(2, fifoFrames, false, true, false);
    fifoBuffer.clear();
    scratch.setSize(2, scratchFrames, false, true, false);
    scratch.clear();
}

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

    const double deviceRate = device->getCurrentSampleRate();
    sourceRate.store (sampleRate);
    targetRate.store (deviceRate > 0.0 ? deviceRate : sampleRate);
    currentName = deviceName;

    interpolators[0].reset();
    interpolators[1].reset();
    fifo.reset();

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
    sourceRate.store (0.0);
    targetRate.store (0.0);
    fifo.reset();
}

void WindowsAudioOutput::writeFrom (const juce::AudioBuffer<float>& source, int numSamples)
{
    if (numSamples <= 0)
        return;

    const int capacity = fifo.getTotalSize();
    if (capacity <= 0)
        return;

    if (fifo.getNumReady() + numSamples > capacity) {
        // Playback stalled (e.g. the device was unplugged): drop the backlog
        // so latency does not creep up when it comes back.
        fifo.reset();
        if (numSamples > capacity)
            return;
    }

    const int sourceChannels = source.getNumChannels();
    if (sourceChannels <= 0)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

    for (int ch = 0; ch < juce::jmin (2, fifoBuffer.getNumChannels()); ++ch) {
        const auto* in = source.getReadPointer (juce::jmin (ch, sourceChannels - 1));

        if (size1 > 0)
            fifoBuffer.copyFrom (ch, start1, in, size1);
        if (size2 > 0)
            fifoBuffer.copyFrom (ch, start1 + size1, in + size1, size2);
    }

    fifo.finishedWrite (size1 + size2);
}

int WindowsAudioOutput::pullBlock (juce::AudioBuffer<float>& dest, int numSamples) noexcept
{
    dest.clear();

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
    return size1 + size2;
}

// --- AudioIODeviceCallback -----------------------------------------------------

void WindowsAudioOutput::audioDeviceAboutToStart (juce::AudioIODevice*)
{
    interpolators[0].reset();
    interpolators[1].reset();
}

void WindowsAudioOutput::audioDeviceStopped()
{
}

void WindowsAudioOutput::audioDeviceError (const juce::String&)
{
}

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

    const double asioRate = sourceRate.load();
    const double devRate  = targetRate.load();
    const bool needsResample = asioRate > 0.0 && devRate > 0.0
                               && std::abs (asioRate - devRate) > 1.0;

    // Never process more frames than the scratch buffer can hold; the rest of
    // the output buffer stays silent (it was cleared above).
    const int outFrames = juce::jmin (numSamples, scratch.getNumSamples());

    int needed = needsResample ? (int) std::ceil (outFrames * asioRate / devRate)
                               : outFrames;
    // A couple of guard frames: the interpolator may consume slightly beyond
    // the ideal ratio at block boundaries.
    needed = juce::jmin (needed + (needsResample ? 4 : 0), scratch.getNumSamples());

    scratch.clear();
    pullBlock (scratch, needed);

    float* left  = outputs[first];
    float* right = outputs[second >= 0 ? second : first];

    if (needsResample) {
        const double ratio = asioRate / devRate;
        interpolators[0].process (ratio, scratch.getReadPointer (0), left,  outFrames);
        interpolators[1].process (ratio, scratch.getReadPointer (1), right, outFrames);
    } else {
        juce::FloatVectorOperations::copy (left,  scratch.getReadPointer (0), outFrames);
        juce::FloatVectorOperations::copy (right, scratch.getReadPointer (1), outFrames);
    }
}

} // namespace eeval
