#pragma once

#include <JuceHeader.h>
#include <atomic>

namespace eeval {

/**
 * Moves stereo audio between two devices that run on independent clocks (an
 * ASIO driver and a Windows Audio device) with a small, constant delay.
 *
 * The producer thread calls write() at its own rate; the consumer thread calls
 * read() at its own rate. Two clocks never run at exactly the same speed (tens
 * of ppm apart is normal), so a plain FIFO slowly fills up (the delay grows
 * minute by minute) or runs empty (dropouts).
 *
 * The bridge keeps the amount of buffered audio at a target that only covers
 * the two block sizes plus a little jitter headroom:
 *  - it resamples on the consumer side, and nudges the speed by at most
 *    +/- 0.3 % (inaudible) to steer the buffer towards the target, which
 *    cancels clock drift continuously;
 *  - if the buffer jumps far above the target (e.g. after a stall), it skips
 *    the excess at once instead of keeping the extra delay;
 *  - if it runs empty, it outputs silence and waits until the target is
 *    reached again (no rapid crackling).
 *
 * write() and read() are real-time safe: everything is allocated in prepare().
 */
class ClockBridge
{
public:
    ClockBridge();

    /** Not real-time: call before the devices start. Rates in Hz. */
    void prepare(double producerRate, double consumerRate);

    /** Producer thread. Channels beyond 2 are ignored; mono is duplicated. */
    void write(const float* const* channels, int numChannels, int numFrames) noexcept;

    /** Consumer thread. Fills numOutChannels (1 or 2) with numFrames. */
    void read(float* const* outputs, int numOutChannels, int numFrames) noexcept;

    /** Current buffered audio in milliseconds (any thread; approximate). */
    double getBufferedMs() const noexcept;

    /** Target buffer in milliseconds (any thread; approximate). */
    double getTargetMs() const noexcept;

    // Tuning (public for the offline test).
    static constexpr double maxSpeedAdjust   = 0.003;  // +/- 0.3 %
    static constexpr double gainPerSecond    = 0.1;    // 10 ms off -> 0.1 % faster/slower
    static constexpr double smoothing        = 0.02;   // per read() call
    static constexpr double headroomSeconds  = 0.002;  // jitter allowance on top of the blocks
    static constexpr double hardLimitSeconds = 0.060;  // skip excess beyond target + this

private:
    static constexpr int fifoFrames    = 1 << 18;
    static constexpr int stagingFrames = 1 << 14;

    int targetFrames(int readFrames) const noexcept;
    void pullIntoStaging(int wanted) noexcept;
    void discard(int frames) noexcept;

    juce::AbstractFifo fifo { fifoFrames };
    juce::AudioBuffer<float> fifoBuffer;
    juce::AudioBuffer<float> staging;      // consumer side: frames taken from the FIFO, not yet resampled
    int stagingCount = 0;
    juce::LagrangeInterpolator interpolators[2];

    double producerRate = 48000.0, consumerRate = 48000.0, baseRatio = 1.0;
    double smoothedFill = 0.0;
    bool priming = true;
    std::atomic<int> maxWriteBlock { 0 };
    std::atomic<double> bufferedMs { 0.0 }, targetMs { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClockBridge)
};

} // namespace eeval
