// Offline test for eeval::ClockBridge: simulates two audio devices whose real
// clocks differ slightly and checks that the buffered delay stays small and
// constant over a long run, with no dropouts.
//
// Build:  cmake -B build -DEE_BUILD_TESTS=ON && cmake --build build --config Release --target ClockBridgeTest
// Run:    build\ClockBridgeTest_artefacts\Release\ClockBridgeTest.exe

#include <JuceHeader.h>
#include "audio/ClockBridge.h"
#include <cstdio>
#include <vector>

struct Scenario {
    const char* name;
    double producerNominal, consumerNominal;   // what the bridge is told
    double producerPpm, consumerPpm;           // real clock error
    int producerBlock, consumerBlock;
    double seconds;
    double stallAt = -1.0, stallSeconds = 0.0; // consumer stops for a while
};

static bool run(const Scenario& s)
{
    eeval::ClockBridge bridge;
    bridge.prepare(s.producerNominal, s.consumerNominal);

    const double pRate = s.producerNominal * (1.0 + s.producerPpm * 1e-6);
    const double cRate = s.consumerNominal * (1.0 + s.consumerPpm * 1e-6);
    const double pPeriod = s.producerBlock / pRate, cPeriod = s.consumerBlock / cRate;

    std::vector<float> inL(s.producerBlock), inR(s.producerBlock), outL(s.consumerBlock), outR(s.consumerBlock);
    double phase = 0.0;
    const double freq = 440.0;

    double tP = 0.0, tC = 0.0;
    long silentBlocksAfterStart = 0, readCalls = 0;
    bool started = false;
    double minMs = 1e9, maxMs = 0.0;
    std::vector<double> minuteSum, minuteCount;   // average delay per minute

    while (tP < s.seconds || tC < s.seconds) {
        if (tP <= tC) {
            for (int i = 0; i < s.producerBlock; ++i) {
                inL[i] = inR[i] = (float)std::sin(phase);
                phase += juce::MathConstants<double>::twoPi * freq / s.producerNominal;
            }
            const float* ch[2] = { inL.data(), inR.data() };
            bridge.write(ch, 2, s.producerBlock);
            tP += pPeriod;
        } else {
            const bool stalled = s.stallAt >= 0.0 && tC >= s.stallAt && tC < s.stallAt + s.stallSeconds;
            if (!stalled) {
                float* out[2] = { outL.data(), outR.data() };
                bridge.read(out, 2, s.consumerBlock);
                ++readCalls;
                float peak = 0.0f;
                for (float v : outL) peak = std::max(peak, std::abs(v));
                if (peak > 0.0f) started = true;
                else if (started) ++silentBlocksAfterStart;

                const double ms = bridge.getBufferedMs();
                if (tC > 5.0) { minMs = std::min(minMs, ms); maxMs = std::max(maxMs, ms); }
                const size_t minute = (size_t)(tC / 60.0);
                if (minuteSum.size() <= minute) { minuteSum.resize(minute + 1, 0.0); minuteCount.resize(minute + 1, 0.0); }
                minuteSum[minute] += ms; minuteCount[minute] += 1.0;
            }
            tC += cPeriod;
        }
    }

    // Average delay of each full minute (skipping the first, which includes start-up).
    double firstAvg = 0.0, lastAvg = 0.0, lowAvg = 1e9, highAvg = 0.0;
    for (size_t m = 1; m < minuteSum.size(); ++m) {
        if (minuteCount[m] < 1.0) continue;
        const double avg = minuteSum[m] / minuteCount[m];
        if (firstAvg == 0.0) firstAvg = avg;
        lastAvg = avg; lowAvg = std::min(lowAvg, avg); highAvg = std::max(highAvg, avg);
    }
    // A stall is allowed to cause a short refill gap; nothing else may drop out.
    const long allowedSilence = s.stallSeconds > 0.0 ? (long)(0.2 / cPeriod) : 0;
    const bool flat = highAvg - lowAvg < 2.0;   // the average delay must not creep
    const bool small = maxMs < 60.0;
    const bool ok = flat && small && silentBlocksAfterStart <= allowedSilence;

    std::printf("%-46s %s  delay %.1f..%.1f ms; per-minute average first %.2f, last %.2f, range %.2f..%.2f ms; "
                "silent blocks %ld (allowed %ld)\n",
                s.name, ok ? "PASS" : "FAIL", minMs, maxMs, firstAvg, lastAvg, lowAvg, highAvg,
                silentBlocksAfterStart, allowedSilence);
    return ok;
}

int main()
{
    const Scenario scenarios[] = {
        { "ASIO 48k fast (+80 ppm) -> WASAPI 48k, 1 h",      48000, 48000,  80, 0, 256, 480, 3600 },
        { "ASIO 48k slow (-80 ppm) -> WASAPI 48k, 1 h",      48000, 48000, -80, 0, 256, 480, 3600 },
        { "mic 48k (+150 ppm) -> ASIO 48k blocks 128, 1 h",  48000, 48000, 150, 0, 480, 128, 3600 },
        { "ASIO 48k -> WASAPI 44.1k (+50 ppm), 1 h",         48000, 44100,   0, 50, 512, 441, 3600 },
        { "ASIO 48k -> WASAPI 48k, 400 ms stall at 10 min",  48000, 48000,  80, 0, 256, 480, 1200, 600.0, 0.4 },
    };

    bool all = true;
    for (const auto& s : scenarios)
        all = run(s) && all;
    std::printf(all ? "ALL PASSED\n" : "SOME FAILED\n");
    return all ? 0 : 1;
}
