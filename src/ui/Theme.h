#pragma once
#include <JuceHeader.h>

namespace eeval {
namespace theme {

// ============================================================================
// Palette — modelled on EasyEffects 8 (Qt / Kirigami) with the Breeze Dark
// colour scheme: dark window, darker views, Breeze blue (#3DAEE9) accent,
// green spectrum on black, amber level read-outs.
// The original names (bgBase, bgSurface, ...) are kept so every editor in the
// project picks up the new look automatically.
// ============================================================================

const juce::Colour bgBase        = juce::Colour(0xFF202326); // window / page background
const juce::Colour bgSurface     = juce::Colour(0xFF1B1E21); // sidebar, footer (Breeze "view")
const juce::Colour bgToolbar     = juce::Colour(0xFF2A2E32); // toolbar / header
const juce::Colour bgOverlay     = juce::Colour(0xFF31363B); // buttons, hover
const juce::Colour bgCard        = juce::Colour(0xFF292C30); // titled cards
const juce::Colour bgInput       = juce::Colour(0xFF1B1E20); // text fields / spin boxes
const juce::Colour borderSubtle  = juce::Colour(0xFF3E4348); // frames
const juce::Colour borderStrong  = juce::Colour(0xFF5A5F65); // control outlines

const juce::Colour textPrimary   = juce::Colour(0xFFFCFCFC);
const juce::Colour textSecondary = juce::Colour(0xFFA1A9B1);
const juce::Colour textDisabled  = juce::Colour(0xFF6E757C);

const juce::Colour accentPrimary = juce::Colour(0xFF3DAEE9); // Breeze highlight blue
const juce::Colour accentSuccess = juce::Colour(0xFF35D06E); // EasyEffects spectrum green
const juce::Colour accentDanger  = juce::Colour(0xFFDA4453); // Breeze negative
const juce::Colour accentWarning = juce::Colour(0xFFF67400); // Breeze neutral/orange

const juce::Colour spectrumBg    = juce::Colour(0xFF0E0F10);
const juce::Colour spectrumBar   = accentSuccess;

// EasyEffects shows input/output levels in small tinted boxes.
const juce::Colour levelBoxBg    = juce::Colour(0xFF4A3527);
const juce::Colour levelBoxText  = juce::Colour(0xFFFFD2B0);

constexpr float radius = 4.0f;  // Breeze corner radius

// ---------------------------------------------------------------------------
// Drawing helpers shared by the editors
// ---------------------------------------------------------------------------

// Titled card ("Controls", "Loudness", ...) — returns the content area.
inline juce::Rectangle<int> drawCard(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title)
{
    auto r = bounds.toFloat().reduced(0.5f);
    g.setColour(bgCard);
    g.fillRoundedRectangle(r, radius);
    g.setColour(borderSubtle);
    g.drawRoundedRectangle(r, radius, 1.0f);

    auto content = bounds.reduced(12, 8);
    if (title.isNotEmpty()) {
        auto titleArea = content.removeFromTop(26);
        g.setColour(textPrimary);
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText(title, titleArea, juce::Justification::centredLeft, true);
        content.removeFromTop(4);
    }
    return content;
}

// Amber level read-out box showing a dB value ("-17", "-inf").
inline void drawLevelBox(juce::Graphics& g, juce::Rectangle<int> bounds, float db)
{
    const bool silent = db <= -99.0f;
    auto r = bounds.toFloat().reduced(0.5f);
    g.setColour(silent ? bgInput : levelBoxBg);
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(borderSubtle);
    g.drawRoundedRectangle(r, 3.0f, 1.0f);

    g.setColour(silent ? textDisabled : (db > -0.5f ? accentDanger.brighter(0.4f) : levelBoxText));
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText(silent ? juce::String("-inf") : juce::String(juce::roundToInt(db)),
               bounds, juce::Justification::centred, false);
}

class CustomLookAndFeel : public juce::LookAndFeel_V4 {
public:
    CustomLookAndFeel() {
        setDefaultSansSerifTypefaceName("Segoe UI");

        // Window
        setColour(juce::ResizableWindow::backgroundColourId, bgBase);
        setColour(juce::DocumentWindow::backgroundColourId, bgBase);

        // Labels
        setColour(juce::Label::textColourId, textPrimary);
        setColour(juce::Label::backgroundColourId, juce::Colour(0x00000000));
        setColour(juce::Label::outlineWhenEditingColourId, accentPrimary);
        setColour(juce::Label::backgroundWhenEditingColourId, bgInput);
        setColour(juce::Label::textWhenEditingColourId, textPrimary);

        // TextButtons
        setColour(juce::TextButton::buttonColourId, bgOverlay);
        setColour(juce::TextButton::buttonOnColourId, accentPrimary);
        setColour(juce::TextButton::textColourOffId, textPrimary);
        setColour(juce::TextButton::textColourOnId, textPrimary);

        // ToggleButtons
        setColour(juce::ToggleButton::textColourId, textPrimary);
        setColour(juce::ToggleButton::tickColourId, accentPrimary);
        setColour(juce::ToggleButton::tickDisabledColourId, textDisabled);

        // ListBox
        setColour(juce::ListBox::backgroundColourId, bgSurface);
        setColour(juce::ListBox::textColourId, textPrimary);
        setColour(juce::ListBox::outlineColourId, juce::Colour(0x00000000));

        // Sliders
        setColour(juce::Slider::thumbColourId, accentPrimary);
        setColour(juce::Slider::trackColourId, accentPrimary);
        setColour(juce::Slider::backgroundColourId, borderSubtle);
        setColour(juce::Slider::rotarySliderFillColourId, accentPrimary);
        setColour(juce::Slider::rotarySliderOutlineColourId, bgOverlay);
        setColour(juce::Slider::textBoxTextColourId, textPrimary);
        setColour(juce::Slider::textBoxBackgroundColourId, bgInput);
        setColour(juce::Slider::textBoxOutlineColourId, borderSubtle);
        setColour(juce::Slider::textBoxHighlightColourId, accentPrimary.withAlpha(0.35f));

        // TextEditor
        setColour(juce::TextEditor::backgroundColourId, bgInput);
        setColour(juce::TextEditor::textColourId, textPrimary);
        setColour(juce::TextEditor::outlineColourId, borderSubtle);
        setColour(juce::TextEditor::focusedOutlineColourId, accentPrimary);
        setColour(juce::TextEditor::highlightColourId, accentPrimary.withAlpha(0.35f));

        // ComboBox
        setColour(juce::ComboBox::backgroundColourId, bgOverlay);
        setColour(juce::ComboBox::textColourId, textPrimary);
        setColour(juce::ComboBox::arrowColourId, textSecondary);
        setColour(juce::ComboBox::outlineColourId, borderStrong);
        setColour(juce::ComboBox::focusedOutlineColourId, accentPrimary);

        // PopupMenu
        setColour(juce::PopupMenu::backgroundColourId, bgToolbar);
        setColour(juce::PopupMenu::textColourId, textPrimary);
        setColour(juce::PopupMenu::headerTextColourId, textSecondary);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accentPrimary.withAlpha(0.35f));
        setColour(juce::PopupMenu::highlightedTextColourId, textPrimary);

        // AlertWindow
        setColour(juce::AlertWindow::backgroundColourId, bgToolbar);
        setColour(juce::AlertWindow::textColourId, textPrimary);
        setColour(juce::AlertWindow::outlineColourId, borderSubtle);

        // ScrollBar
        setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xFF4D5257));
        setColour(juce::ScrollBar::trackColourId, juce::Colour(0x00000000));

        // Make the V4 colour scheme (used by JUCE's built-in dialogs) match too.
        getCurrentColourScheme().setUIColour(ColourScheme::windowBackground, bgBase);
        getCurrentColourScheme().setUIColour(ColourScheme::widgetBackground, bgOverlay);
        getCurrentColourScheme().setUIColour(ColourScheme::menuBackground, bgToolbar);
        getCurrentColourScheme().setUIColour(ColourScheme::outline, borderSubtle);
        getCurrentColourScheme().setUIColour(ColourScheme::defaultText, textPrimary);
        getCurrentColourScheme().setUIColour(ColourScheme::defaultFill, accentPrimary);
        getCurrentColourScheme().setUIColour(ColourScheme::highlightedText, textPrimary);
        getCurrentColourScheme().setUIColour(ColourScheme::highlightedFill, accentPrimary.withAlpha(0.35f));
        getCurrentColourScheme().setUIColour(ColourScheme::menuText, textPrimary);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override {
        return juce::Font(juce::FontOptions(juce::jmin(15.0f, (float)buttonHeight * 0.55f)));
    }

    juce::Font getLabelFont(juce::Label& label) override {
        return label.getFont().withHeight(juce::jmax(13.0f, label.getFont().getHeight()));
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override {
        return juce::Font(juce::FontOptions(14.0f));
    }

    juce::Font getPopupMenuFont() override {
        return juce::Font(juce::FontOptions(14.0f));
    }

    // Breeze push button: flat fill, 1px outline that turns blue on hover,
    // tinted blue fill when toggled on.
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        const bool on = button.getToggleState();
        const bool flat = button.getProperties().getWithDefault("flat", false);
        // Toggled colour comes from the button (red Bypass, green Monitor, blue default).
        const auto onColour = button.findColour(juce::TextButton::buttonOnColourId);

        juce::Colour fill = on ? onColour.withAlpha(0.28f) : backgroundColour;
        if (flat && !on)
            fill = juce::Colours::transparentBlack;
        if (shouldDrawButtonAsDown)
            fill = onColour.withAlpha(0.40f);
        else if (shouldDrawButtonAsHighlighted && flat && !on)
            fill = bgOverlay;

        g.setColour(fill);
        g.fillRoundedRectangle(bounds, radius);

        juce::Colour outline = on ? onColour : borderStrong;
        if (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown)
            outline = on ? onColour : accentPrimary;
        if (flat && !on && !shouldDrawButtonAsHighlighted && !shouldDrawButtonAsDown)
            outline = juce::Colours::transparentBlack;

        g.setColour(outline);
        g.drawRoundedRectangle(bounds, radius, 1.0f);
    }

    // Breeze combo box: same frame as a button plus a chevron.
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int, int, int, int, juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<float>(0, 0, (float)width, (float)height).reduced(0.5f);
        g.setColour(isButtonDown ? accentPrimary.withAlpha(0.25f) : bgOverlay);
        g.fillRoundedRectangle(bounds, radius);
        g.setColour(box.hasKeyboardFocus(true) || box.isMouseOver(true) ? accentPrimary : borderStrong);
        g.drawRoundedRectangle(bounds, radius, 1.0f);

        juce::Path chevron;
        const float cx = (float)width - 14.0f, cy = (float)height * 0.5f;
        chevron.startNewSubPath(cx - 4.0f, cy - 2.0f);
        chevron.lineTo(cx, cy + 2.0f);
        chevron.lineTo(cx + 4.0f, cy - 2.0f);
        g.setColour(textSecondary);
        g.strokePath(chevron, juce::PathStrokeType(1.5f));
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        label.setBounds(8, 1, box.getWidth() - 30, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }

    // Breeze slider: thin groove, blue fill, round handle with outline.
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        const bool hover = slider.isMouseOverOrDragging();

        if (style == juce::Slider::LinearHorizontal)
        {
            const float cy = (float)y + (float)height * 0.5f;
            const float h = 4.0f;
            g.setColour(borderSubtle);
            g.fillRoundedRectangle((float)x, cy - h * 0.5f, (float)width, h, 2.0f);

            if (sliderPos > (float)x) {
                g.setColour(accentPrimary);
                g.fillRoundedRectangle((float)x, cy - h * 0.5f, sliderPos - (float)x, h, 2.0f);
            }

            const float s = 18.0f;
            auto knob = juce::Rectangle<float>(sliderPos - s * 0.5f, cy - s * 0.5f, s, s);
            g.setColour(bgOverlay);
            g.fillEllipse(knob);
            g.setColour(hover ? accentPrimary : borderStrong);
            g.drawEllipse(knob.reduced(0.5f), 1.0f);
        }
        else if (style == juce::Slider::LinearVertical)
        {
            const float cx = (float)x + (float)width * 0.5f;
            const float w = 4.0f;
            g.setColour(borderSubtle);
            g.fillRoundedRectangle(cx - w * 0.5f, (float)y, w, (float)height, 2.0f);

            const float bottom = (float)(y + height);
            if (sliderPos < bottom) {
                g.setColour(accentPrimary);
                g.fillRoundedRectangle(cx - w * 0.5f, sliderPos, w, bottom - sliderPos, 2.0f);
            }

            const float s = 16.0f;
            auto knob = juce::Rectangle<float>(cx - s * 0.5f, sliderPos - s * 0.5f, s, s);
            g.setColour(bgOverlay);
            g.fillEllipse(knob);
            g.setColour(hover ? accentPrimary : borderStrong);
            g.drawEllipse(knob.reduced(0.5f), 1.0f);
        }
        else
        {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                             minSliderPos, maxSliderPos, style, slider);
        }
    }

    // Spin-box style text field next to sliders (EasyEffects uses spin boxes).
    juce::Label* createSliderTextBox(juce::Slider& slider) override {
        auto* l = LookAndFeel_V4::createSliderTextBox(slider);
        l->setColour(juce::Label::backgroundColourId, bgInput);
        l->setColour(juce::Label::outlineColourId, borderSubtle);
        l->setColour(juce::Label::textColourId, textPrimary);
        l->setJustificationType(juce::Justification::centredRight);
        l->setFont(juce::Font(juce::FontOptions(14.0f)));
        return l;
    }

    // Small up/down buttons for IncDecButtons sliders (spin boxes).
    juce::Button* createSliderButton(juce::Slider&, bool isIncrement) override {
        return new SpinArrowButton(isIncrement);
    }

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override {
        g.fillAll(bgToolbar);
        g.setColour(borderSubtle);
        g.drawRect(0, 0, width, height, 1);
    }

private:
    class SpinArrowButton : public juce::Button {
    public:
        explicit SpinArrowButton(bool up) : juce::Button(up ? "+" : "-"), isUp(up) {}
        void paintButton(juce::Graphics& g, bool over, bool down) override {
            auto r = getLocalBounds().toFloat();
            g.setColour(down ? accentPrimary.withAlpha(0.35f) : (over ? bgOverlay.brighter(0.08f) : bgOverlay));
            g.fillRect(r);
            g.setColour(borderSubtle);
            g.drawRect(r, 1.0f);

            juce::Path p;
            const float cx = r.getCentreX(), cy = r.getCentreY();
            if (isUp) { p.startNewSubPath(cx - 3.5f, cy + 1.5f); p.lineTo(cx, cy - 2.0f); p.lineTo(cx + 3.5f, cy + 1.5f); }
            else      { p.startNewSubPath(cx - 3.5f, cy - 1.5f); p.lineTo(cx, cy + 2.0f); p.lineTo(cx + 3.5f, cy - 1.5f); }
            g.setColour(over ? textPrimary : textSecondary);
            g.strokePath(p, juce::PathStrokeType(1.4f));
        }
    private:
        bool isUp;
    };
};

} // namespace theme
} // namespace eeval
