#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../dsp/SpectrumAnalyzer.h"
#include "Theme.h"

namespace eeval {
namespace ui {

/**
 * SpectrumAnalyzerEditor - full-width bar spectrum in the EasyEffects style:
 * green bars on black, logarithmic 20 Hz – 20 kHz axis, and a frequency scale
 * underneath labelled like EasyEffects (20, 37, 70, 132 ... 10.7k, Hz).
 */
class SpectrumAnalyzerEditor : public juce::Component, private juce::Timer {
public:
    explicit SpectrumAnalyzerEditor(EasyEffectsAudioProcessor& p)
        : processor(p)
    {
        setOpaque(true);
        startTimerHz(30);
    }

    ~SpectrumAnalyzerEditor() override = default;

    void paint(juce::Graphics& g) override {
        auto area = getLocalBounds();
        g.fillAll(theme::spectrumBg);

        auto scale = area.removeFromBottom(scaleHeight);
        auto plot = area.reduced(4, 6);

        // --- bars ---------------------------------------------------------
        const auto& data = processor.getSpectrumAnalyzer().getDisplayData();
        const int bins = (int)data.size();
        double sr = processor.getSampleRate();
        if (sr <= 0.0) sr = 48000.0;
        const double binHz = sr / (double)dsp::SpectrumAnalyzer::fftSize;

        const int numBars = juce::jlimit(32, 160, plot.getWidth() / 6);
        const float slot = (float)plot.getWidth() / (float)numBars;
        const float gap = slot > 4.0f ? 2.0f : 1.0f;
        const float barW = juce::jmax(1.0f, slot - gap);
        const float h = (float)plot.getHeight();

        g.setColour(theme::spectrumBar);
        for (int i = 0; i < numBars; ++i) {
            // Frequency edges of this bar on a log axis.
            const double f0 = minHz * std::pow(maxHz / minHz, (double)i / numBars);
            const double f1 = minHz * std::pow(maxHz / minHz, (double)(i + 1) / numBars);
            int b0 = juce::jlimit(0, bins - 1, (int)std::floor(f0 / binHz));
            int b1 = juce::jlimit(0, bins - 1, (int)std::ceil(f1 / binHz));
            if (b1 < b0) b1 = b0;

            float db = -100.0f;
            for (int b = b0; b <= b1; ++b)
                db = juce::jmax(db, data[(size_t)b]);

            const float level = juce::jlimit(0.0f, 1.0f, juce::jmap(db, -90.0f, -10.0f, 0.0f, 1.0f));
            const float barH = level * h;
            if (barH < 1.0f)
                continue;   // silence: draw nothing, like EasyEffects
            const float x = (float)plot.getX() + (float)i * slot;
            g.fillRect(x, (float)plot.getBottom() - barH, barW, barH);
        }

        // --- frequency scale ------------------------------------------------
        g.setColour(theme::bgToolbar);
        g.fillRect(scale);
        g.setColour(theme::textSecondary);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));

        // 11 equal steps on the log axis, exactly like EasyEffects' labels.
        constexpr int steps = 11;
        const float w = (float)plot.getWidth();
        for (int k = 0; k < steps; ++k) {
            const double f = minHz * std::pow(maxHz / minHz, (double)k / steps);
            const float x = (float)plot.getX() + w * (float)k / (float)steps;
            g.drawText(formatHz(f), juce::Rectangle<float>(x + 2.0f, (float)scale.getY(), 60.0f, (float)scale.getHeight()),
                       juce::Justification::centredLeft, false);
        }
        g.drawText("Hz", scale.withTrimmedRight(6), juce::Justification::centredRight, false);
    }

    void timerCallback() override {
        processor.getSpectrumAnalyzer().performAnalysis();
        repaint();
    }

    static constexpr int scaleHeight = 16;

private:
    static juce::String formatHz(double f) {
        if (f >= 1000.0)
            return juce::String(f / 1000.0, 1) + "k";
        return juce::String(juce::roundToInt(f));
    }

    static constexpr double minHz = 20.0;
    static constexpr double maxHz = 20000.0;

    EasyEffectsAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyzerEditor)
};

} // namespace ui
} // namespace eeval
