#include "ClockBridge.h"

namespace eeval {

ClockBridge::ClockBridge()
{
    fifoBuffer.setSize(2, fifoFrames, false, true, false);
    staging.setSize(2, stagingFrames, false, true, false);
}

void ClockBridge::prepare(double producerHz, double consumerHz)
{
    producerRate = producerHz > 0.0 ? producerHz : 48000.0;
    consumerRate = consumerHz > 0.0 ? consumerHz : producerRate;
    baseRatio = producerRate / consumerRate;   // producer frames per consumer frame

    fifo.reset();
    fifoBuffer.clear();
    staging.clear();
    stagingCount = 0;
    interpolators[0].reset();
    interpolators[1].reset();
    smoothedFill = 0.0;
    priming = true;
    maxWriteBlock.store(0);
    bufferedMs.store(0.0);
    targetMs.store(0.0);
}

void ClockBridge::write(const float* const* channels, int numChannels, int numFrames) noexcept
{
    if (channels == nullptr || numChannels <= 0 || numFrames <= 0)
        return;

    if (numFrames > maxWriteBlock.load(std::memory_order_relaxed))
        maxWriteBlock.store(numFrames, std::memory_order_relaxed);

    // The consumer drains to the target; a full FIFO means it stopped (e.g. the
    // device disappeared). Drop this block rather than block or overwrite.
    if (fifo.getFreeSpace() < numFrames)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite(numFrames, start1, size1, start2, size2);

    for (int ch = 0; ch < 2; ++ch) {
        const float* in = channels[juce::jmin(ch, numChannels - 1)];
        if (in == nullptr) in = channels[0];
        if (in == nullptr) {
            if (size1 > 0) fifoBuffer.clear(ch, start1, size1);
            if (size2 > 0) fifoBuffer.clear(ch, start2, size2);
            continue;
        }
        if (size1 > 0) fifoBuffer.copyFrom(ch, start1, in, size1);
        if (size2 > 0) fifoBuffer.copyFrom(ch, start2, in + size1, size2);
    }

    fifo.finishedWrite(size1 + size2);
}

int ClockBridge::targetFrames(int readFrames) const noexcept
{
    // Enough for one producer block plus what one read consumes, plus jitter.
    const int consumed = (int)std::ceil((double)readFrames * baseRatio) + 4;
    return maxWriteBlock.load(std::memory_order_relaxed) + consumed
         + (int)std::ceil(producerRate * headroomSeconds);
}

void ClockBridge::pullIntoStaging(int wanted) noexcept
{
    wanted = juce::jmin(wanted, stagingFrames - stagingCount);
    if (wanted <= 0)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead(wanted, start1, size1, start2, size2);
    for (int ch = 0; ch < 2; ++ch) {
        if (size1 > 0) staging.copyFrom(ch, stagingCount, fifoBuffer, ch, start1, size1);
        if (size2 > 0) staging.copyFrom(ch, stagingCount + size1, fifoBuffer, ch, start2, size2);
    }
    fifo.finishedRead(size1 + size2);
    stagingCount += size1 + size2;
}

void ClockBridge::discard(int frames) noexcept
{
    // Oldest audio first: staging, then the FIFO.
    const int fromStaging = juce::jmin(frames, stagingCount);
    if (fromStaging > 0) {
        const int remaining = stagingCount - fromStaging;
        for (int ch = 0; ch < 2; ++ch) {
            auto* d = staging.getWritePointer(ch);
            std::memmove(d, d + fromStaging, sizeof(float) * (size_t)remaining);
        }
        stagingCount = remaining;
        frames -= fromStaging;
    }
    if (frames > 0) {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToRead(juce::jmin(frames, fifo.getNumReady()), start1, size1, start2, size2);
        fifo.finishedRead(size1 + size2);
    }
}

void ClockBridge::read(float* const* outputs, int numOutChannels, int numFrames) noexcept
{
    if (outputs == nullptr || numOutChannels <= 0 || numFrames <= 0)
        return;

    float* left  = outputs[0];
    float* right = numOutChannels > 1 && outputs[1] != nullptr ? outputs[1] : nullptr;
    auto silence = [&] {
        if (left)  juce::FloatVectorOperations::clear(left,  numFrames);
        if (right) juce::FloatVectorOperations::clear(right, numFrames);
    };

    const int target = targetFrames(numFrames);
    int fill = fifo.getNumReady() + stagingCount;

    // Start (or restart after running dry) once the target is buffered, so a
    // short underrun does not turn into continuous crackling.
    if (priming) {
        if (fill < target || maxWriteBlock.load(std::memory_order_relaxed) == 0) {
            silence();
            bufferedMs.store(1000.0 * fill / producerRate);
            targetMs.store(1000.0 * target / producerRate);
            return;
        }
        // Begin exactly at the target, not with whatever piled up meanwhile.
        if (fill > target)
            discard(fill - target);
        fill = fifo.getNumReady() + stagingCount;
        smoothedFill = fill;
        priming = false;
    }

    // Far too much buffered (stall, device hiccup): jump back to the target.
    const int hardLimit = target + (int)(producerRate * hardLimitSeconds);
    if (fill > hardLimit) {
        discard(fill - target);
        fill = fifo.getNumReady() + stagingCount;
        smoothedFill = fill;
    }

    // Steer the buffer towards the target with a tiny speed change.
    smoothedFill += smoothing * ((double)fill - smoothedFill);
    const double errorSeconds = (smoothedFill - (double)target) / producerRate;
    const double adjust = juce::jlimit(-maxSpeedAdjust, maxSpeedAdjust, errorSeconds * gainPerSecond);
    const double ratio = baseRatio * (1.0 + adjust);

    // Make sure the interpolator has all the input it may need.
    const int needed = (int)std::ceil((double)numFrames * ratio) + 4;
    if (stagingCount < needed)
        pullIntoStaging(needed - stagingCount);

    if (stagingCount < needed || needed > stagingFrames) {
        // Ran dry: silence now, and refill to the target before playing again.
        silence();
        priming = true;
        return;
    }

    float* outL = left != nullptr ? left : right;
    const int used = interpolators[0].process(ratio, staging.getReadPointer(0), outL, numFrames);
    if (right != nullptr && left != nullptr)
        interpolators[1].process(ratio, staging.getReadPointer(1), right, numFrames);

    // Drop what the interpolator consumed; keep the rest for the next call.
    const int consumed = juce::jlimit(0, stagingCount, used);
    const int remaining = stagingCount - consumed;
    for (int ch = 0; ch < 2; ++ch) {
        auto* d = staging.getWritePointer(ch);
        std::memmove(d, d + consumed, sizeof(float) * (size_t)remaining);
    }
    stagingCount = remaining;

    bufferedMs.store(1000.0 * (fifo.getNumReady() + stagingCount) / producerRate);
    targetMs.store(1000.0 * target / producerRate);
}

double ClockBridge::getBufferedMs() const noexcept { return bufferedMs.load(); }
double ClockBridge::getTargetMs() const noexcept   { return targetMs.load(); }

} // namespace eeval
