#pragma once
#include <JuceHeader.h>

namespace eeval {
namespace theme {

// ============================================================================
// Windows 11 "Fluent" look.
//
// Colours follow the WinUI 3 design tokens (Mica window, layer, card, control
// fills and strokes) for both the dark and the light theme, and the accent is
// read from the Windows personalisation settings (see WindowsChrome.cpp).
//
// The token names from the earlier Breeze theme (bgBase, bgSurface, ...) are
// kept so every editor in the project picks up the new look automatically.
// They are variables, not constants: applyScheme() swaps the palette when the
// user (or Windows) switches between light and dark.
// ============================================================================

inline bool isDark = true;

inline juce::Colour windowBg      { 0xFF202020 }; // Mica fallback: title bar + navigation pane
inline juce::Colour bgBase        { 0xFF272727 }; // content layer (pages)
inline juce::Colour bgSurface     { 0xFF2B2B2B }; // lists inside cards
inline juce::Colour bgToolbar     { 0xFF2C2C2C }; // flyouts, menus, dialogs
inline juce::Colour bgOverlay     { 0xFF2D2D2D }; // control fill (buttons, combo boxes)
inline juce::Colour bgOverlayHover{ 0xFF323232 }; // control fill, pointer over
inline juce::Colour bgOverlayDown { 0xFF292929 }; // control fill, pressed
inline juce::Colour bgCard        { 0xFF2B2B2B }; // cards
inline juce::Colour bgCardHover   { 0xFF323232 }; // clickable cards, pointer over
inline juce::Colour bgInput       { 0xFF1E1E1E }; // text fields / spin boxes
inline juce::Colour subtleFill    { 0x0FFFFFFF }; // list item hover / selection
inline juce::Colour subtleFillDown{ 0x0AFFFFFF };
inline juce::Colour borderSubtle  { 0xFF1D1D1D }; // card stroke
inline juce::Colour borderStrong  { 0xFF3A3A3A }; // control stroke
inline juce::Colour strokeStrong  { 0xFF9A9A9A }; // slider rails, switch outlines
inline juce::Colour divider       { 0xFF333333 };

inline juce::Colour textPrimary   { 0xFFFFFFFF };
inline juce::Colour textSecondary { 0xFFC5C5C5 };
inline juce::Colour textTertiary  { 0xFF9D9D9D };
inline juce::Colour textDisabled  { 0xFF717171 };

inline juce::Colour accentPrimary { 0xFF60CDFF }; // AccentFillColorDefault
inline juce::Colour accentHover   { 0xFF5BBCEB };
inline juce::Colour textOnAccent  { 0xFF000000 };
inline juce::Colour accentText    { 0xFF99EBFF }; // accent-coloured text / links
inline juce::Colour accentSuccess { 0xFF6CCB5F };
inline juce::Colour accentDanger  { 0xFFFF99A4 };
inline juce::Colour accentWarning { 0xFFFCE100 };

inline juce::Colour spectrumBg    { 0xFF2B2B2B };
inline juce::Colour spectrumBar   { 0xFF60CDFF };
inline juce::Colour plotBg        { 0xFF1C1C1C }; // graph areas (EQ, dynamics)
inline juce::Colour gridLine      { 0x14FFFFFF };
inline juce::Colour gridText      { 0x80FFFFFF };

inline juce::Colour levelBoxBg    { 0xFF1E1E1E };
inline juce::Colour levelBoxText  { 0xFFC5C5C5 };

constexpr float radius     = 4.0f;  // ControlCornerRadius
constexpr float cardRadius = 8.0f;  // OverlayCornerRadius

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------

struct AccentPalette {
    // Windows' accent shades: Light3, Light2, Light1, Base, Dark1, Dark2, Dark3
    juce::Colour light3 { 0xFF99EBFF }, light2 { 0xFF60CDFF }, light1 { 0xFF4CC2FF },
                 base   { 0xFF0078D4 }, dark1  { 0xFF005FB8 }, dark2  { 0xFF004275 },
                 dark3  { 0xFF003A70 };
};

inline void applyScheme(bool dark, const AccentPalette& a)
{
    isDark = dark;
    if (dark) {
        windowBg = juce::Colour(0xFF202020);  bgBase = juce::Colour(0xFF272727);
        bgSurface = juce::Colour(0xFF2B2B2B); bgToolbar = juce::Colour(0xFF2C2C2C);
        bgOverlay = juce::Colour(0xFF2D2D2D); bgOverlayHover = juce::Colour(0xFF323232);
        bgOverlayDown = juce::Colour(0xFF292929);
        bgCard = juce::Colour(0xFF2B2B2B);    bgCardHover = juce::Colour(0xFF323232);
        bgInput = juce::Colour(0xFF1E1E1E);
        subtleFill = juce::Colour(0x0FFFFFFF); subtleFillDown = juce::Colour(0x0AFFFFFF);
        borderSubtle = juce::Colour(0xFF1D1D1D); borderStrong = juce::Colour(0xFF3A3A3A);
        strokeStrong = juce::Colour(0xFF9A9A9A); divider = juce::Colour(0xFF333333);
        textPrimary = juce::Colour(0xFFFFFFFF); textSecondary = juce::Colour(0xFFC5C5C5);
        textTertiary = juce::Colour(0xFF9D9D9D); textDisabled = juce::Colour(0xFF717171);
        accentPrimary = a.light2; accentHover = a.light2.withMultipliedAlpha(0.9f);
        accentText = a.light3;    textOnAccent = juce::Colour(0xFF000000);
        accentSuccess = juce::Colour(0xFF6CCB5F); accentDanger = juce::Colour(0xFFFF99A4);
        accentWarning = juce::Colour(0xFFFCE100);
        spectrumBg = bgCard; plotBg = juce::Colour(0xFF1C1C1C);
        gridLine = juce::Colour(0x14FFFFFF); gridText = juce::Colour(0x80FFFFFF);
        levelBoxBg = juce::Colour(0xFF1E1E1E); levelBoxText = textSecondary;
    } else {
        windowBg = juce::Colour(0xFFF3F3F3);  bgBase = juce::Colour(0xFFF9F9F9);
        bgSurface = juce::Colour(0xFFFFFFFF); bgToolbar = juce::Colour(0xFFF9F9F9);
        bgOverlay = juce::Colour(0xFFFDFDFD); bgOverlayHover = juce::Colour(0xFFF6F6F6);
        bgOverlayDown = juce::Colour(0xFFF2F2F2);
        bgCard = juce::Colour(0xFFFFFFFF);    bgCardHover = juce::Colour(0xFFF6F6F6);
        bgInput = juce::Colour(0xFFFFFFFF);
        subtleFill = juce::Colour(0x09000000); subtleFillDown = juce::Colour(0x06000000);
        borderSubtle = juce::Colour(0xFFE5E5E5); borderStrong = juce::Colour(0xFFD9D9D9);
        strokeStrong = juce::Colour(0xFF8A8A8A); divider = juce::Colour(0xFFEAEAEA);
        textPrimary = juce::Colour(0xFF1B1B1B); textSecondary = juce::Colour(0xFF5F5F5F);
        textTertiary = juce::Colour(0xFF8A8A8A); textDisabled = juce::Colour(0xFFA0A0A0);
        accentPrimary = a.dark1; accentHover = a.dark1.withMultipliedAlpha(0.9f);
        accentText = a.dark2;    textOnAccent = juce::Colour(0xFFFFFFFF);
        accentSuccess = juce::Colour(0xFF0F7B0F); accentDanger = juce::Colour(0xFFC42B1C);
        accentWarning = juce::Colour(0xFF9D5D00);
        spectrumBg = bgCard; plotBg = juce::Colour(0xFFFBFBFB);
        gridLine = juce::Colour(0x14000000); gridText = juce::Colour(0x99000000);
        levelBoxBg = juce::Colour(0xFFF6F6F6); levelBoxText = textSecondary;
    }
    spectrumBar = accentPrimary;
}

// ---------------------------------------------------------------------------
// Typography (Windows 11 type ramp) and Fluent icons
// ---------------------------------------------------------------------------

namespace detail {
    inline const juce::StringArray& installedFaces() {
        static const juce::StringArray faces = juce::Font::findAllTypefaceNames();
        return faces;
    }
    inline juce::String firstInstalled(std::initializer_list<const char*> names) {
        for (auto* n : names)
            if (installedFaces().contains(n, true))
                return n;
        return {};
    }
}

inline const juce::String& textFace() {
    static const juce::String f = [] {
        auto s = detail::firstInstalled({ "Segoe UI Variable Text", "Segoe UI Variable", "Segoe UI" });
        return s.isNotEmpty() ? s : juce::String("Segoe UI");
    }();
    return f;
}

inline const juce::String& displayFace() {
    static const juce::String f = [] {
        auto s = detail::firstInstalled({ "Segoe UI Variable Display", "Segoe UI Variable", "Segoe UI" });
        return s.isNotEmpty() ? s : textFace();
    }();
    return f;
}

// Segoe Fluent Icons ships with Windows 11; Segoe MDL2 Assets (same code
// points) with Windows 10.
inline const juce::String& iconFace() {
    static const juce::String f = [] {
        auto s = detail::firstInstalled({ "Segoe Fluent Icons", "Segoe MDL2 Assets" });
        return s.isNotEmpty() ? s : juce::String("Segoe MDL2 Assets");
    }();
    return f;
}

inline juce::Font font(float px, bool semibold = false, bool display = false) {
    return juce::Font(juce::FontOptions()
                          .withName(display ? displayFace() : textFace())
                          .withStyle(semibold ? "Semibold" : "Regular")
                          .withPointHeight(px));
}

inline juce::Font caption()    { return font(12.0f); }
inline juce::Font body()       { return font(14.0f); }
inline juce::Font bodyStrong() { return font(14.0f, true); }
inline juce::Font subtitle()   { return font(20.0f, true, true); }
inline juce::Font title()      { return font(28.0f, true, true); }

namespace icon {
    constexpr juce::juce_wchar nav        = 0xE700;
    constexpr juce::juce_wchar speakers   = 0xE7F5;
    constexpr juce::juce_wchar volume     = 0xE767;
    constexpr juce::juce_wchar microphone = 0xE720;
    constexpr juce::juce_wchar settings   = 0xE713;
    constexpr juce::juce_wchar devices    = 0xE772;
    constexpr juce::juce_wchar add        = 0xE710;
    constexpr juce::juce_wchar remove     = 0xE74D;
    constexpr juce::juce_wchar more       = 0xE712;
    constexpr juce::juce_wchar chevronDown  = 0xE70D;
    constexpr juce::juce_wchar chevronUp    = 0xE70E;
    constexpr juce::juce_wchar chevronRight = 0xE76C;
    constexpr juce::juce_wchar headphones = 0xE7F6;
    constexpr juce::juce_wchar equalizer  = 0xE9E9;
    constexpr juce::juce_wchar power      = 0xE7E8;
    constexpr juce::juce_wchar gripper    = 0xE76F;
    constexpr juce::juce_wchar info       = 0xE946;
    constexpr juce::juce_wchar library    = 0xE8F1;
    constexpr juce::juce_wchar check      = 0xE73E;
    constexpr juce::juce_wchar admin      = 0xE7EF;
    constexpr juce::juce_wchar sync       = 0xE895;
    constexpr juce::juce_wchar colour     = 0xE790;
    constexpr juce::juce_wchar openLink   = 0xE8A7;
    constexpr juce::juce_wchar apps       = 0xECAA;
    constexpr juce::juce_wchar up         = 0xE74A;
    constexpr juce::juce_wchar down       = 0xE74B;
    constexpr juce::juce_wchar sliders    = 0xE9E9;
    constexpr juce::juce_wchar plug       = 0xE703;
    constexpr juce::juce_wchar reset      = 0xE72C;
}

inline juce::Font iconFont(float px) {
    return juce::Font(juce::FontOptions().withName(iconFace()).withPointHeight(px));
}

inline void drawIcon(juce::Graphics& g, juce::juce_wchar glyph, juce::Rectangle<float> area,
                     juce::Colour c, float px = 16.0f)
{
    g.setColour(c);
    g.setFont(iconFont(px));
    g.drawText(juce::String::charToString(glyph), area, juce::Justification::centred, false);
}

// ---------------------------------------------------------------------------
// Drawing helpers shared by the editors
// ---------------------------------------------------------------------------

// Fluent card: filled rounded rectangle with a hairline stroke.
inline void fillCard(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill = bgCard)
{
    r = r.reduced(0.5f);
    g.setColour(fill);
    g.fillRoundedRectangle(r, cardRadius);
    g.setColour(borderSubtle);
    g.drawRoundedRectangle(r, cardRadius, 1.0f);
}

// Titled card ("Controls", "Bands", ...) — returns the content area.
inline juce::Rectangle<int> drawCard(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title)
{
    fillCard(g, bounds.toFloat());

    auto content = bounds.reduced(16, 10);
    if (title.isNotEmpty()) {
        auto titleArea = content.removeFromTop(26);
        g.setColour(textPrimary);
        g.setFont(bodyStrong());
        g.drawText(title, titleArea, juce::Justification::centredLeft, true);
        content.removeFromTop(4);
    }
    return content;
}

// Small level read-out showing a dB value ("-17", "-inf").
inline void drawLevelBox(juce::Graphics& g, juce::Rectangle<int> bounds, float db)
{
    const bool silent = db <= -99.0f;
    auto r = bounds.toFloat().reduced(0.5f);
    g.setColour(levelBoxBg);
    g.fillRoundedRectangle(r, radius);
    g.setColour(borderStrong);
    g.drawRoundedRectangle(r, radius, 1.0f);

    // A thin level bar along the bottom edge, like the Windows volume flyout.
    if (!silent) {
        const float level = juce::jlimit(0.0f, 1.0f, juce::jmap(db, -60.0f, 0.0f, 0.0f, 1.0f));
        auto bar = r.reduced(3.0f, 0.0f).removeFromBottom(3.0f).translated(0.0f, -2.0f);
        g.setColour(db > -0.5f ? accentDanger : accentPrimary);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * level), 1.5f);
    }

    g.setColour(silent ? textDisabled : (db > -0.5f ? accentDanger : levelBoxText));
    g.setFont(caption());
    g.drawText(silent ? juce::String("-inf") : juce::String(juce::roundToInt(db)),
               bounds.withTrimmedBottom(3), juce::Justification::centred, false);
}

// WinUI ToggleSwitch (40 x 20).
inline void drawSwitch(juce::Graphics& g, juce::Rectangle<float> area, bool on, bool hover, bool enabled = true)
{
    auto r = area.withSizeKeepingCentre(40.0f, 20.0f);
    const float alpha = enabled ? 1.0f : 0.4f;
    if (on) {
        g.setColour((hover ? accentHover : accentPrimary).withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(r, 10.0f);
        const float d = hover ? 14.0f : 12.0f;
        g.setColour(textOnAccent.withMultipliedAlpha(alpha));
        g.fillEllipse(r.getRight() - 10.0f - d * 0.5f, r.getCentreY() - d * 0.5f, d, d);
    } else {
        g.setColour((hover ? subtleFill : juce::Colours::transparentBlack));
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(strokeStrong.withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(r.reduced(0.5f), 9.5f, 1.0f);
        const float d = hover ? 14.0f : 12.0f;
        g.fillEllipse(r.getX() + 10.0f - d * 0.5f, r.getCentreY() - d * 0.5f, d, d);
    }
}

// ============================================================================
// LookAndFeel
// ============================================================================

class CustomLookAndFeel : public juce::LookAndFeel_V4 {
public:
    CustomLookAndFeel() { refreshColours(); }

    // Re-reads the palette (call after applyScheme()).
    void refreshColours() {
        setDefaultSansSerifTypefaceName(textFace());

        setColour(juce::ResizableWindow::backgroundColourId, bgBase);
        setColour(juce::DocumentWindow::backgroundColourId, windowBg);
        setColour(juce::DocumentWindow::textColourId, textPrimary);

        setColour(juce::Label::textColourId, textPrimary);
        setColour(juce::Label::backgroundColourId, juce::Colour(0x00000000));
        setColour(juce::Label::outlineWhenEditingColourId, accentPrimary);
        setColour(juce::Label::backgroundWhenEditingColourId, bgInput);
        setColour(juce::Label::textWhenEditingColourId, textPrimary);

        setColour(juce::TextButton::buttonColourId, bgOverlay);
        setColour(juce::TextButton::buttonOnColourId, accentPrimary);
        setColour(juce::TextButton::textColourOffId, textPrimary);
        setColour(juce::TextButton::textColourOnId, textOnAccent);

        setColour(juce::ToggleButton::textColourId, textPrimary);
        setColour(juce::ToggleButton::tickColourId, accentPrimary);
        setColour(juce::ToggleButton::tickDisabledColourId, textDisabled);

        setColour(juce::ListBox::backgroundColourId, juce::Colour(0x00000000));
        setColour(juce::ListBox::textColourId, textPrimary);
        setColour(juce::ListBox::outlineColourId, juce::Colour(0x00000000));

        setColour(juce::Slider::thumbColourId, accentPrimary);
        setColour(juce::Slider::trackColourId, accentPrimary);
        setColour(juce::Slider::backgroundColourId, strokeStrong);
        setColour(juce::Slider::rotarySliderFillColourId, accentPrimary);
        setColour(juce::Slider::rotarySliderOutlineColourId, borderStrong);
        setColour(juce::Slider::textBoxTextColourId, textPrimary);
        setColour(juce::Slider::textBoxBackgroundColourId, bgInput);
        setColour(juce::Slider::textBoxOutlineColourId, borderStrong);
        setColour(juce::Slider::textBoxHighlightColourId, accentPrimary.withAlpha(0.35f));

        setColour(juce::TextEditor::backgroundColourId, bgInput);
        setColour(juce::TextEditor::textColourId, textPrimary);
        setColour(juce::TextEditor::outlineColourId, borderStrong);
        setColour(juce::TextEditor::focusedOutlineColourId, accentPrimary);
        setColour(juce::TextEditor::highlightColourId, accentPrimary.withAlpha(0.35f));
        setColour(juce::TextEditor::highlightedTextColourId, textPrimary);
        setColour(juce::CaretComponent::caretColourId, textPrimary);

        setColour(juce::ComboBox::backgroundColourId, bgOverlay);
        setColour(juce::ComboBox::textColourId, textPrimary);
        setColour(juce::ComboBox::arrowColourId, textSecondary);
        setColour(juce::ComboBox::outlineColourId, borderStrong);
        setColour(juce::ComboBox::focusedOutlineColourId, accentPrimary);

        // Slightly translucent so the menu window is non-opaque and can have
        // Windows 11's rounded flyout corners.
        setColour(juce::PopupMenu::backgroundColourId, bgToolbar.withAlpha(0.995f));
        setColour(juce::PopupMenu::textColourId, textPrimary);
        setColour(juce::PopupMenu::headerTextColourId, textSecondary);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, subtleFill);
        setColour(juce::PopupMenu::highlightedTextColourId, textPrimary);

        setColour(juce::AlertWindow::backgroundColourId, bgToolbar);
        setColour(juce::AlertWindow::textColourId, textPrimary);
        setColour(juce::AlertWindow::outlineColourId, borderStrong);

        setColour(juce::ScrollBar::thumbColourId, textTertiary.withAlpha(0.6f));
        setColour(juce::ScrollBar::trackColourId, juce::Colour(0x00000000));

        setColour(juce::TooltipWindow::backgroundColourId, bgToolbar);
        setColour(juce::TooltipWindow::textColourId, textPrimary);
        setColour(juce::TooltipWindow::outlineColourId, borderStrong);

        setColour(juce::GroupComponent::outlineColourId, borderStrong);
        setColour(juce::GroupComponent::textColourId, textSecondary);

        auto& cs = getCurrentColourScheme();
        cs.setUIColour(ColourScheme::windowBackground, bgBase);
        cs.setUIColour(ColourScheme::widgetBackground, bgOverlay);
        cs.setUIColour(ColourScheme::menuBackground, bgToolbar);
        cs.setUIColour(ColourScheme::outline, borderStrong);
        cs.setUIColour(ColourScheme::defaultText, textPrimary);
        cs.setUIColour(ColourScheme::defaultFill, accentPrimary);
        cs.setUIColour(ColourScheme::highlightedText, textOnAccent);
        cs.setUIColour(ColourScheme::highlightedFill, accentPrimary);
        cs.setUIColour(ColourScheme::menuText, textPrimary);
    }

    // --- Fonts ---------------------------------------------------------------
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return body(); }
    juce::Font getLabelFont(juce::Label& label) override { return label.getFont(); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return body(); }
    juce::Font getPopupMenuFont() override { return body(); }
    juce::Font getAlertWindowTitleFont() override { return subtitle(); }
    juce::Font getAlertWindowMessageFont() override { return body(); }
    juce::Font getAlertWindowFont() override { return body(); }

    // --- Buttons -------------------------------------------------------------
    // Properties understood on a TextButton:
    //   "flat"     : subtle button (no fill until hovered) — command bar style
    //   "accent"   : accent-filled primary button
    //   "dropdown" : shows a chevron on the right (DropDownButton)
    //   "icon"     : Segoe Fluent Icons code point drawn before the text
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                              bool over, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        const bool on = button.getToggleState();
        const bool flat = button.getProperties().getWithDefault("flat", false);
        const bool accent = button.getProperties().getWithDefault("accent", false);
        const bool enabled = button.isEnabled();

        if (on || accent) {
            auto c = button.findColour(juce::TextButton::buttonOnColourId);
            if (accent && !on) c = accentPrimary;
            if (!enabled) c = c.withMultipliedAlpha(0.4f);
            else if (down) c = c.withMultipliedAlpha(0.8f);
            else if (over) c = c.withMultipliedAlpha(0.9f);
            g.setColour(c);
            g.fillRoundedRectangle(bounds, radius);
            return;
        }

        if (flat) {
            if (over || down) {
                g.setColour(down ? subtleFillDown : subtleFill);
                g.fillRoundedRectangle(bounds, radius);
            }
            return;
        }

        g.setColour(down ? bgOverlayDown : (over ? bgOverlayHover : bgOverlay));
        g.fillRoundedRectangle(bounds, radius);
        g.setColour(borderStrong);
        g.drawRoundedRectangle(bounds, radius, 1.0f);
        if (!down && isDark) {   // WinUI's lighter top edge on dark controls
            g.setColour(juce::Colours::white.withAlpha(0.03f));
            g.drawHorizontalLine((int)bounds.getY() + 1, bounds.getX() + radius, bounds.getRight() - radius);
        }
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool down) override
    {
        const bool on = button.getToggleState() || (bool)button.getProperties().getWithDefault("accent", false);
        auto colour = on ? button.findColour(juce::TextButton::textColourOnId)
                         : button.findColour(juce::TextButton::textColourOffId);
        if (!button.isEnabled()) colour = on ? colour.withMultipliedAlpha(0.7f) : textDisabled;
        else if (down && !on)    colour = textSecondary;

        auto area = button.getLocalBounds().reduced(10, 0);
        const auto text = button.getButtonText();

        if (button.getProperties().getWithDefault("dropdown", false)) {
            auto chev = area.removeFromRight(14).toFloat();
            drawIcon(g, icon::chevronDown, chev, on ? colour : textSecondary, 10.0f);
            area.removeFromRight(6);
        }

        const int glyph = (int)button.getProperties().getWithDefault("icon", 0);
        if (glyph != 0) {
            if (text.isEmpty()) {
                drawIcon(g, (juce::juce_wchar)glyph, button.getLocalBounds().toFloat(), colour, 16.0f);
                return;
            }
            auto f = body();
            const int textW = juce::roundToInt(juce::GlyphArrangement::getStringWidth(f, text));
            const int total = 16 + 8 + textW;
            const bool leftAlign = button.getProperties().getWithDefault("dropdown", false);
            // Centre icon + label as a group, but let the label use all the
            // remaining width (string-width estimates can run a little short).
            auto content = area;
            if (!leftAlign)
                content.removeFromLeft(juce::jmax(0, (area.getWidth() - total) / 2));
            drawIcon(g, (juce::juce_wchar)glyph, content.removeFromLeft(16).toFloat(), colour, 16.0f);
            content.removeFromLeft(8);
            g.setColour(colour);
            g.setFont(f);
            g.drawText(text, content, juce::Justification::centredLeft, true);
            return;
        }

        g.setColour(colour);
        g.setFont(body());
        const bool leftAlign = button.getProperties().getWithDefault("dropdown", false);
        g.drawText(text, area, leftAlign ? juce::Justification::centredLeft : juce::Justification::centred, true);
    }

    // ToggleButton = WinUI ToggleSwitch when the "switch" property is set,
    // otherwise a WinUI CheckBox.
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool over, bool down) override
    {
        juce::ignoreUnused(down);
        auto area = button.getLocalBounds();
        const bool on = button.getToggleState();
        const bool asSwitch = button.getProperties().getWithDefault("switch", false);

        if (asSwitch) {
            auto sw = area.removeFromRight(40).toFloat();
            drawSwitch(g, sw, on, over, button.isEnabled());
            area.removeFromRight(12);
            g.setColour(button.isEnabled() ? textPrimary : textDisabled);
            g.setFont(body());
            g.drawText(button.getButtonText(), area, juce::Justification::centredRight, true);
            return;
        }

        auto box = area.removeFromLeft(20).withSizeKeepingCentre(20, 20).toFloat();
        if (on) {
            g.setColour(button.isEnabled() ? accentPrimary : textDisabled);
            g.fillRoundedRectangle(box, radius);
            drawIcon(g, icon::check, box, textOnAccent, 12.0f);
        } else {
            g.setColour(over ? subtleFill : bgInput);
            g.fillRoundedRectangle(box, radius);
            g.setColour(strokeStrong);
            g.drawRoundedRectangle(box.reduced(0.5f), radius, 1.0f);
        }
        area.removeFromLeft(8);
        g.setColour(button.isEnabled() ? textPrimary : textDisabled);
        g.setFont(body());
        g.drawText(button.getButtonText(), area, juce::Justification::centredLeft, true);
    }

    // --- ComboBox -------------------------------------------------------------
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int, int, int, int, juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<float>(0, 0, (float)width, (float)height).reduced(0.5f);
        const bool over = box.isMouseOver(true);
        g.setColour(isButtonDown ? bgOverlayDown : (over ? bgOverlayHover : bgOverlay));
        g.fillRoundedRectangle(bounds, radius);
        g.setColour(borderStrong);
        g.drawRoundedRectangle(bounds, radius, 1.0f);
        if (box.hasKeyboardFocus(true)) {
            g.setColour(accentPrimary);
            g.fillRoundedRectangle(bounds.removeFromBottom(2.0f).reduced(radius * 0.5f, 0.0f), 1.0f);
        }
        drawIcon(g, icon::chevronDown, { (float)width - 30.0f, 0.0f, 20.0f, (float)height }, textSecondary, 10.0f);
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        label.setBounds(4, 1, box.getWidth() - 34, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }

    // --- Sliders ----------------------------------------------------------------
    // WinUI Slider: 4 px rail, accent fill, round thumb with an accent centre.
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        const bool hover = slider.isMouseOverOrDragging();
        const bool dragging = slider.isMouseButtonDown();

        auto drawThumb = [&](float cx, float cy) {
            const float outer = 20.0f;
            g.setColour(isDark ? juce::Colour(0xFF454545) : juce::Colours::white);
            g.fillEllipse(cx - outer * 0.5f, cy - outer * 0.5f, outer, outer);
            g.setColour(isDark ? juce::Colour(0x24FFFFFF) : juce::Colour(0x29000000));
            g.drawEllipse(cx - outer * 0.5f + 0.5f, cy - outer * 0.5f + 0.5f, outer - 1.0f, outer - 1.0f, 1.0f);
            const float inner = dragging ? 10.0f : (hover ? 14.0f : 12.0f);
            g.setColour(accentPrimary);
            g.fillEllipse(cx - inner * 0.5f, cy - inner * 0.5f, inner, inner);
        };

        if (style == juce::Slider::LinearHorizontal)
        {
            const float cy = (float)y + (float)height * 0.5f;
            const float h = 4.0f;
            g.setColour(strokeStrong);
            g.fillRoundedRectangle((float)x, cy - h * 0.5f, (float)width, h, 2.0f);
            if (sliderPos > (float)x) {
                g.setColour(accentPrimary);
                g.fillRoundedRectangle((float)x, cy - h * 0.5f, sliderPos - (float)x, h, 2.0f);
            }
            drawThumb(sliderPos, cy);
        }
        else if (style == juce::Slider::LinearVertical)
        {
            const float cx = (float)x + (float)width * 0.5f;
            const float w = 4.0f;
            g.setColour(strokeStrong);
            g.fillRoundedRectangle(cx - w * 0.5f, (float)y, w, (float)height, 2.0f);
            const float bottom = (float)(y + height);
            if (sliderPos < bottom) {
                g.setColour(accentPrimary);
                g.fillRoundedRectangle(cx - w * 0.5f, sliderPos, w, bottom - sliderPos, 2.0f);
            }
            drawThumb(cx, sliderPos);
        }
        else
        {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                             minSliderPos, maxSliderPos, style, slider);
        }
    }

    int getSliderThumbRadius(juce::Slider&) override { return 10; }

    // NumberBox-style text field next to sliders.
    juce::Label* createSliderTextBox(juce::Slider& slider) override {
        auto* l = LookAndFeel_V4::createSliderTextBox(slider);
        l->setColour(juce::Label::backgroundColourId, bgInput);
        l->setColour(juce::Label::outlineColourId, borderStrong);
        l->setColour(juce::Label::textColourId, textPrimary);
        l->setJustificationType(juce::Justification::centredRight);
        l->setFont(body());
        return l;
    }

    juce::Button* createSliderButton(juce::Slider&, bool isIncrement) override {
        return new SpinArrowButton(isIncrement);
    }

    // --- Popup menus (WinUI MenuFlyout) ----------------------------------------
    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override {
        auto r = juce::Rectangle<float>(0, 0, (float)width, (float)height).reduced(0.5f);
        g.setColour(bgToolbar);
        g.fillRoundedRectangle(r, cardRadius);
        g.setColour(isDark ? juce::Colour(0x33000000).interpolatedWith(borderStrong, 0.5f) : borderStrong);
        g.drawRoundedRectangle(r, cardRadius, 1.0f);
    }

    void drawPopupMenuBackgroundWithOptions(juce::Graphics& g, int width, int height,
                                            const juce::PopupMenu::Options&) override {
        drawPopupMenuBackground(g, width, height);
    }

    int getPopupMenuBorderSize() override { return 4; }
    int getPopupMenuBorderSizeWithOptions(const juce::PopupMenu::Options&) override { return 4; }

    void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int standardHeight,
                                   int& idealWidth, int& idealHeight) override
    {
        if (isSeparator) { idealWidth = 50; idealHeight = 9; return; }
        idealHeight = standardHeight > 0 ? standardHeight : 32;
        idealWidth = juce::roundToInt(juce::GlyphArrangement::getStringWidth(body(), text)) + 12 + 28 + 24 + 16;
    }

    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText, const juce::Drawable* iconDrawable,
                           const juce::Colour* textColourToUse) override
    {
        juce::ignoreUnused(iconDrawable);
        if (isSeparator) {
            g.setColour(divider);
            g.fillRect(area.getX() - 4, area.getCentreY(), area.getWidth() + 8, 1);
            return;
        }

        auto r = area.reduced(0, 1).toFloat();
        if (isHighlighted && isActive) {
            g.setColour(subtleFill);
            g.fillRoundedRectangle(r, radius);
        }

        auto textColour = textColourToUse != nullptr ? *textColourToUse : textPrimary;
        if (!isActive) textColour = textDisabled;

        auto row = area.reduced(12, 0);
        auto tickArea = row.removeFromLeft(20).toFloat();
        if (isTicked)
            drawIcon(g, icon::check, tickArea, isActive ? accentPrimary : textDisabled, 12.0f);
        row.removeFromLeft(8);

        if (hasSubMenu) {
            drawIcon(g, icon::chevronRight, row.removeFromRight(16).toFloat(), textSecondary, 10.0f);
            row.removeFromRight(4);
        }
        if (shortcutKeyText.isNotEmpty()) {
            g.setColour(textSecondary);
            g.setFont(caption());
            g.drawText(shortcutKeyText, row, juce::Justification::centredRight, false);
        }

        g.setColour(textColour);
        g.setFont(body());
        g.drawText(text, row, juce::Justification::centredLeft, true);
    }

    void drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area,
                                    const juce::String& sectionName) override
    {
        g.setColour(textSecondary);
        g.setFont(font(12.0f, true));
        g.drawText(sectionName, area.reduced(12, 0).withTrimmedTop(4), juce::Justification::centredLeft, true);
    }

    void drawPopupMenuSectionHeaderWithOptions(juce::Graphics& g, const juce::Rectangle<int>& area,
                                               const juce::String& sectionName,
                                               const juce::PopupMenu::Options&) override
    {
        drawPopupMenuSectionHeader(g, area, sectionName);
    }

    // --- Scroll bars (thin, rounded) ------------------------------------------
    int getDefaultScrollbarWidth() override { return 12; }

    void drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                       bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                       bool isMouseOver, bool isMouseDown) override
    {
        juce::Rectangle<float> thumb;
        const float t = (isMouseOver || isMouseDown) ? 6.0f : 3.0f;
        if (isScrollbarVertical)
            thumb = { (float)x + (float)width - t - 3.0f, (float)thumbStartPosition + 2.0f, t, (float)thumbSize - 4.0f };
        else
            thumb = { (float)thumbStartPosition + 2.0f, (float)y + (float)height - t - 3.0f, (float)thumbSize - 4.0f, t };
        g.setColour(textTertiary.withAlpha(isMouseOver || isMouseDown ? 0.8f : 0.5f));
        g.fillRoundedRectangle(thumb, t * 0.5f);
    }

    // --- Dialogs ----------------------------------------------------------------
    void drawAlertBox(juce::Graphics& g, juce::AlertWindow& alert,
                      const juce::Rectangle<int>& textArea, juce::TextLayout& textLayout) override
    {
        g.fillAll(bgToolbar);
        g.setColour(borderStrong);
        g.drawRect(alert.getLocalBounds(), 1);


        g.setColour(alert.findColour(juce::AlertWindow::textColourId));
        textLayout.draw(g, textArea.withTrimmedLeft(8).toFloat());
    }

    int getAlertWindowButtonHeight() override { return 32; }

    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override {
        auto r = juce::Rectangle<float>(0, 0, (float)width, (float)height).reduced(0.5f);
        g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle(r, radius);
    }

    void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override {
        auto r = juce::Rectangle<float>(0, 0, (float)width, (float)height).reduced(0.5f);
        g.setColour(borderStrong);
        g.drawRoundedRectangle(r, radius, 1.0f);
        g.setColour(editor.hasKeyboardFocus(true) ? accentPrimary : strokeStrong);
        g.fillRoundedRectangle(r.removeFromBottom(editor.hasKeyboardFocus(true) ? 2.0f : 1.0f).reduced(radius * 0.5f, 0.0f), 1.0f);
    }

    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override {
        auto r = juce::Rectangle<float>(0, 0, (float)width, (float)height).reduced(0.5f);
        g.setColour(bgToolbar);
        g.fillRoundedRectangle(r, radius);
        g.setColour(borderStrong);
        g.drawRoundedRectangle(r, radius, 1.0f);
        g.setColour(textPrimary);
        g.setFont(caption());
        g.drawFittedText(text, juce::Rectangle<int>(width, height).reduced(8, 4), juce::Justification::centredLeft, 4);
    }

private:
    class SpinArrowButton : public juce::Button {
    public:
        explicit SpinArrowButton(bool up) : juce::Button(up ? "+" : "-"), isUp(up) {}
        void paintButton(juce::Graphics& g, bool over, bool down) override {
            auto r = getLocalBounds().toFloat().reduced(2.0f);
            if (over || down) {
                g.setColour(down ? subtleFillDown : subtleFill);
                g.fillRoundedRectangle(r, radius);
            }
            drawIcon(g, isUp ? icon::chevronUp : icon::chevronDown, r,
                     over ? textPrimary : textSecondary, 10.0f);
        }
    private:
        bool isUp;
    };
};

} // namespace theme
} // namespace eeval
