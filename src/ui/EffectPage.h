#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../dsp/EffectRegistry.h"
#include "Theme.h"

namespace eeval {
namespace ui {

/**
 * The strip EasyEffects shows above every plugin:
 *
 *   Input                       Output
 *   [ 0.00 dB +- ] [-17] [-17]   [ 0.00 dB +- ] [-19] [-19]        [ Bypass ]
 *
 * Gain spin boxes are bound to "slotN.inGain" / "slotN.outGain"; the boxes show
 * the left/right peak level (dBFS) before and after the effect.
 */
class SlotIOStrip : public juce::Component, private juce::Timer {
public:
    SlotIOStrip(EasyEffectsAudioProcessor& p, int slot)
        : processor(p), slotIndex(slot)
    {
        const auto sp = EffectRegistry::slotPrefix(slotIndex);

        for (auto* s : { &inGain, &outGain }) {
            s->setSliderStyle(juce::Slider::IncDecButtons);
            s->setIncDecButtonsMode(juce::Slider::incDecButtonsDraggable_Vertical);
            s->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 80, spinHeight);
            s->setTextValueSuffix(" dB");
            s->setNumDecimalPlacesToDisplay(2);
            addAndMakeVisible(*s);
        }
        inAttach  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters, sp + ".inGain", inGain);
        outAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.parameters, sp + ".outGain", outGain);
        // The attachment installs the parameter's own text formatter (1 decimal);
        // override it for the EasyEffects "0.00 dB" look (the " dB" suffix is appended).
        for (auto* s : { &inGain, &outGain }) {
            s->textFromValueFunction = [](double v) { return juce::String(v, 2); };
            s->updateText();
        }

        bypassBtn.setButtonText("Bypass");
        bypassBtn.getProperties().set("icon", (int)theme::icon::power);
        bypassBtn.setClickingTogglesState(true);
        bypassBtn.setColour(juce::TextButton::buttonOnColourId, theme::accentDanger);
        addAndMakeVisible(bypassBtn);
        bypassAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.parameters, sp + ".bypass", bypassBtn);

        resetBtn.getProperties().set("icon", (int)theme::icon::reset);
        resetBtn.setTooltip("Reset this effect to its default settings");
        resetBtn.onClick = [this] { if (onReset) onReset(); };
        addAndMakeVisible(resetBtn);

        startTimerHz(20);
    }

    void paint(juce::Graphics& g) override {
        g.setColour(theme::textSecondary);
        g.setFont(theme::caption());
        g.drawText("Input",  inLabel,  juce::Justification::bottomLeft, false);
        g.drawText("Output", outLabel, juce::Justification::bottomLeft, false);

        for (int ch = 0; ch < 2; ++ch) {
            theme::drawLevelBox(g, inBoxes[ch],  shownIn[ch]);
            theme::drawLevelBox(g, outBoxes[ch], shownOut[ch]);
        }

        // Thin separator under the strip
        g.setColour(theme::divider);
        g.fillRect(0, getHeight() - 1, getWidth(), 1);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(14, 6);
        area.removeFromBottom(4);

        // Same row as the spin boxes (below the 18px label + 4px gap).
        bypassBtn.setBounds(area.removeFromRight(112).withY(area.getY() + 22).withHeight(spinHeight));
        area.removeFromRight(6);
        resetBtn.setBounds(area.removeFromRight(spinHeight + 4).withY(area.getY() + 22).withHeight(spinHeight));

        auto layoutGroup = [this](juce::Rectangle<int> r, juce::Rectangle<int>& label,
                                  juce::Slider& spin, juce::Rectangle<int>* boxes) {
            label = r.removeFromTop(18);
            auto row = r.withSizeKeepingCentre(r.getWidth(), spinHeight).withY(label.getBottom() + 4);
            spin.setBounds(row.removeFromLeft(spinWidth));
            row.removeFromLeft(6);
            boxes[0] = row.removeFromLeft(boxWidth);
            row.removeFromLeft(4);
            boxes[1] = row.removeFromLeft(boxWidth);
        };

        const int groupWidth = spinWidth + 6 + boxWidth * 2 + 4;
        auto inGroup = area.removeFromLeft(groupWidth);
        area.removeFromLeft(24);
        auto outGroup = area.removeFromLeft(groupWidth);

        layoutGroup(inGroup,  inLabel,  inGain,  inBoxes);
        layoutGroup(outGroup, outLabel, outGain, outBoxes);
    }

    static constexpr int preferredHeight = 66;

    std::function<void()> onReset;   // set by the main window

private:
    void timerCallback() override {
        auto* m = processor.getChain().getSlotModule(slotIndex);
        bool changed = false;
        for (int ch = 0; ch < 2; ++ch) {
            const float in  = m ? m->getMeterInDb(ch)  : -120.0f;
            const float out = m ? m->getMeterOutDb(ch) : -120.0f;
            changed |= smooth(shownIn[ch], in);
            changed |= smooth(shownOut[ch], out);
        }
        if (changed) repaint();
    }

    // Peak meter ballistics: instant attack, ~40 dB/s release.
    static bool smooth(float& shown, float target) {
        const float next = target > shown ? target : juce::jmax(target, shown - 2.0f);
        if (std::abs(next - shown) < 0.05f) return false;
        shown = next;
        return true;
    }

    static constexpr int spinHeight = 28;
    static constexpr int spinWidth = 106;
    static constexpr int boxWidth = 34;

    EasyEffectsAudioProcessor& processor;
    int slotIndex;

    juce::Slider inGain, outGain;
    juce::TextButton bypassBtn, resetBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inAttach, outAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttach;

    juce::Rectangle<int> inLabel, outLabel;
    juce::Rectangle<int> inBoxes[2], outBoxes[2];
    float shownIn[2]  { -120.0f, -120.0f };
    float shownOut[2] { -120.0f, -120.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlotIOStrip)
};

/**
 * An effect page = the Input/Output strip on top + the effect's own editor.
 * Inner editors may publish a "preferredHeight" component property so the
 * page can grow (the main window scrolls it in a viewport).
 */
class EffectPage : public juce::Component {
public:
    EffectPage(EasyEffectsAudioProcessor& p, int slot, std::unique_ptr<juce::Component> innerEditor)
        : strip(p, slot), inner(std::move(innerEditor))
    {
        addAndMakeVisible(strip);
        if (inner) addAndMakeVisible(*inner);
    }

    void setOnReset(std::function<void()> f) { strip.onReset = std::move(f); }

    int getPreferredHeight() const {
        const int innerH = inner ? (int)inner->getProperties().getWithDefault("preferredHeight", 0) : 0;
        return SlotIOStrip::preferredHeight + innerH;
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(theme::bgBase);
    }

    void resized() override {
        auto a = getLocalBounds();
        strip.setBounds(a.removeFromTop(SlotIOStrip::preferredHeight));
        if (inner) inner->setBounds(a);
    }

private:
    SlotIOStrip strip;
    std::unique_ptr<juce::Component> inner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectPage)
};

} // namespace ui
} // namespace eeval
