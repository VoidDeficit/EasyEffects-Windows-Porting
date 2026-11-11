#include "WindowsChrome.h"

#if JUCE_WINDOWS
// Declared here rather than pulling <windows.h>/<dwmapi.h> into a JUCE unit.
extern "C" __declspec(dllimport) long __stdcall DwmSetWindowAttribute(void* hwnd, unsigned long attribute,
                                                                      const void* value, unsigned long size);
 #pragma comment(lib, "dwmapi.lib")
#endif

namespace eeval {
namespace ui {
namespace chrome {

theme::AccentPalette readWindowsAccent()
{
    theme::AccentPalette p;
#if JUCE_WINDOWS
    // 8 RGBA entries: Light3, Light2, Light1, Base, Dark1, Dark2, Dark3, (unused)
    juce::MemoryBlock data;
    juce::WindowsRegistry::getBinaryValue(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent\\AccentPalette", data);
    if (data.getSize() >= 28) {
        auto* b = static_cast<const juce::uint8*>(data.getData());
        auto c = [b](int i) { return juce::Colour(b[i * 4], b[i * 4 + 1], b[i * 4 + 2]); };
        p.light3 = c(0); p.light2 = c(1); p.light1 = c(2); p.base = c(3);
        p.dark1 = c(4);  p.dark2 = c(5);  p.dark3 = c(6);
    }
#endif
    return p;
}

bool resolveDark(const juce::String& themeSetting)
{
    if (themeSetting == "dark")  return true;
    if (themeSetting == "light") return false;
    return juce::Desktop::getInstance().isDarkModeActive();
}

void applyTheme(const juce::String& themeSetting)
{
    theme::applyScheme(resolveDark(themeSetting), readWindowsAccent());
}

void styleTopLevelWindow(juce::Component& content)
{
    auto* top = content.getTopLevelComponent();
    auto* window = dynamic_cast<juce::DocumentWindow*>(top);
    if (window == nullptr || window == &content)
        return;   // plug-in hosted in a DAW / OBS: the host owns the window

    window->setColour(juce::DocumentWindow::backgroundColourId, theme::windowBg);
    window->setBackgroundColour(theme::windowBg);

    if (!window->isUsingNativeTitleBar())
        window->setUsingNativeTitleBar(true);

    // The standalone wrapper's "Options" button lives in JUCE's own title bar;
    // its menu is replaced by the Devices and Settings pages.
    for (auto* child : window->getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(child))
            if (b->getButtonText() == "Options")
                b->setVisible(false);

#if JUCE_WINDOWS
    if (auto* peer = window->getPeer()) {
        auto* hwnd = peer->getNativeHandle();
        const int dark = theme::isDark ? 1 : 0;
        DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
        const int round = 2;     /* DWMWCP_ROUND */
        DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &round, sizeof(round));
        // Caption the same colour as the navigation pane, so the title bar and
        // the app read as one surface (as with Mica).
        const auto c = theme::windowBg;
        const unsigned long caption = (unsigned long)c.getRed() | ((unsigned long)c.getGreen() << 8)
                                    | ((unsigned long)c.getBlue() << 16);
        DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
        const auto t = theme::textPrimary;
        const unsigned long text = (unsigned long)t.getRed() | ((unsigned long)t.getGreen() << 8)
                                 | ((unsigned long)t.getBlue() << 16);
        DwmSetWindowAttribute(hwnd, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof(text));
    }
#endif
}

} // namespace chrome
} // namespace ui
} // namespace eeval
