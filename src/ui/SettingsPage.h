#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include <functional>

namespace eeval {
namespace ui {

/**
 * Windows 11 Settings-style card:
 *
 *   ┌────────────────────────────────────────────────────────────┐
 *   │ [icon]  Title                                  [ control ] │
 *   │         Description (secondary text)                       │
 *   └────────────────────────────────────────────────────────────┘
 *
 * The control is a ToggleSwitch, a button or a drop-down button. Every text is
 * a callback so refresh() can keep the card in sync with the app state.
 */
class SettingsCard : public juce::Component {
public:
    using TextFn = std::function<juce::String()>;
    using BoolFn = std::function<bool()>;

    SettingsCard(juce::juce_wchar glyph, juce::String titleText, TextFn description = {})
        : icon(glyph), title(std::move(titleText)), describe(std::move(description))
    {
        refreshText();
    }

    SettingsCard& withToggle(BoolFn get, std::function<void(bool)> set, BoolFn enabled = {}) {
        toggleGet = std::move(get);
        isEnabledFn = std::move(enabled);
        toggle = std::make_unique<juce::ToggleButton>();
        toggle->getProperties().set("switch", true);
        toggle->onClick = [this, set = std::move(set)] {
            const bool v = toggle->getToggleState();
            if (set) set(v);
            refresh();
        };
        addAndMakeVisible(*toggle);
        refresh();
        return *this;
    }

    SettingsCard& withButton(TextFn text, std::function<void(juce::Component&)> click,
                             bool dropdown = false, bool accent = false, BoolFn enabled = {}) {
        buttonText = std::move(text);
        isEnabledFn = std::move(enabled);
        button = std::make_unique<juce::TextButton>();
        button->getProperties().set("dropdown", dropdown);
        button->getProperties().set("accent", accent);
        button->onClick = [this, click = std::move(click)] { if (click) click(*button); };
        addAndMakeVisible(*button);
        refresh();
        return *this;
    }

    void refresh() {
        refreshText();
        const bool en = isEnabledFn ? isEnabledFn() : true;
        if (toggle) {
            toggle->setEnabled(en);
            const bool v = toggleGet ? toggleGet() : false;
            toggle->setToggleState(v, juce::dontSendNotification);
            toggle->setButtonText(v ? "On" : "Off");
        }
        if (button) {
            button->setEnabled(en);
            const auto t = buttonText ? buttonText() : juce::String();
            if (t != button->getButtonText()) { button->setButtonText(t); resized(); }
        }
        repaint();
    }

    int getPreferredHeight() const { return descText.isNotEmpty() ? 70 : 56; }

    void paint(juce::Graphics& g) override {
        theme::fillCard(g, getLocalBounds().toFloat());
        auto r = getLocalBounds().reduced(16, 0);
        if (icon != 0) {
            theme::drawIcon(g, icon, r.removeFromLeft(20).toFloat(), theme::textPrimary, 18.0f);
            r.removeFromLeft(16);
        }
        r.removeFromRight(controlWidth() + (controlWidth() > 0 ? 16 : 0));

        if (descText.isEmpty()) {
            g.setColour(theme::textPrimary);
            g.setFont(theme::body());
            g.drawText(title, r, juce::Justification::centredLeft, true);
            return;
        }
        auto block = r.withSizeKeepingCentre(r.getWidth(), 40);
        g.setColour(theme::textPrimary);
        g.setFont(theme::body());
        g.drawText(title, block.removeFromTop(20), juce::Justification::centredLeft, true);
        g.setColour(theme::textSecondary);
        g.setFont(theme::caption());
        g.drawText(descText, block, juce::Justification::centredLeft, true);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(16, 0);
        if (toggle)
            toggle->setBounds(r.removeFromRight(controlWidth()).withSizeKeepingCentre(controlWidth(), 32));
        if (button)
            button->setBounds(r.removeFromRight(controlWidth()).withSizeKeepingCentre(controlWidth(), 32));
    }

private:
    int controlWidth() const {
        if (toggle) return 92;
        if (button) {
            const float w = juce::GlyphArrangement::getStringWidth(theme::body(), button->getButtonText());
            const bool dd = button->getProperties().getWithDefault("dropdown", false);
            return juce::jlimit(dd ? 200 : 120, 340, juce::roundToInt(w) + (dd ? 52 : 32));
        }
        return 0;
    }

    void refreshText() {
        auto d = describe ? describe() : juce::String();
        if (d != descText) { descText = d; repaint(); }
    }

    juce::juce_wchar icon;
    juce::String title, descText;
    TextFn describe, buttonText;
    BoolFn toggleGet, isEnabledFn;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::TextButton> button;
};

/**
 * A page of settings cards grouped in sections, with a page title on top.
 * Lives in a viewport; getPreferredHeight() tells it how tall to be.
 */
class SettingsPage : public juce::Component {
public:
    explicit SettingsPage(juce::String pageTitle, juce::String pageSubtitle = {})
        : titleText(std::move(pageTitle)), subtitleText(std::move(pageSubtitle)) {}

    void addSection(const juce::String& name) { entries.push_back({ name, nullptr }); }

    SettingsCard& addCard(juce::juce_wchar glyph, const juce::String& title, SettingsCard::TextFn desc = {}) {
        auto card = std::make_unique<SettingsCard>(glyph, title, std::move(desc));
        auto& ref = *card;
        addAndMakeVisible(ref);
        entries.push_back({ {}, std::move(card) });
        return ref;
    }

    void refresh() {
        for (auto& e : entries)
            if (e.card) e.card->refresh();
        resized();
    }

    int getPreferredHeight() const {
        int y = headerHeight();
        for (auto& e : entries)
            y += e.card ? e.card->getPreferredHeight() + cardGap : sectionHeight;
        return y + 32;
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(theme::bgBase);
        auto col = column();
        g.setColour(theme::textPrimary);
        g.setFont(theme::title());
        g.drawText(titleText, col.getX(), 24, col.getWidth(), 40, juce::Justification::centredLeft, true);
        if (subtitleText.isNotEmpty()) {
            g.setColour(theme::textSecondary);
            g.setFont(theme::body());
            g.drawText(subtitleText, col.getX(), 64, col.getWidth(), 20, juce::Justification::centredLeft, true);
        }

        int y = headerHeight();
        for (auto& e : entries) {
            if (e.card) { y += e.card->getPreferredHeight() + cardGap; continue; }
            g.setColour(theme::textPrimary);
            g.setFont(theme::bodyStrong());
            g.drawText(e.section, col.getX() + 2, y + sectionHeight - 30, col.getWidth(), 22,
                       juce::Justification::centredLeft, true);
            y += sectionHeight;
        }
    }

    void resized() override {
        auto col = column();
        int y = headerHeight();
        for (auto& e : entries) {
            if (!e.card) { y += sectionHeight; continue; }
            const int h = e.card->getPreferredHeight();
            e.card->setBounds(col.getX(), y, col.getWidth(), h);
            y += h + cardGap;
        }
    }

private:
    struct Entry { juce::String section; std::unique_ptr<SettingsCard> card; };

    juce::Rectangle<int> column() const {
        auto r = getLocalBounds().reduced(36, 0);
        return r.withWidth(juce::jmin(r.getWidth(), 1000));
    }
    int headerHeight() const { return subtitleText.isNotEmpty() ? 92 : 76; }

    static constexpr int sectionHeight = 44;
    static constexpr int cardGap = 4;

    juce::String titleText, subtitleText;
    std::vector<Entry> entries;
};

} // namespace ui
} // namespace eeval
