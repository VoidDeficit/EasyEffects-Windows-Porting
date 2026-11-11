#pragma once
#include <JuceHeader.h>
#include "Theme.h"

namespace eeval {
namespace ui {

/** Windows integration for the Fluent look. */
namespace chrome {

/** The user's accent colour shades (Settings > Personalisation > Colours). */
theme::AccentPalette readWindowsAccent();

/** "system", "light" or "dark" -> whether the dark palette should be used. */
bool resolveDark(const juce::String& themeSetting);

/** Loads the palette for a theme setting and the current Windows accent. */
void applyTheme(const juce::String& themeSetting);

/**
 * Gives the top-level window of `content` the native Windows 11 title bar:
 * dark/light caption that matches the app, rounded corners and no JUCE
 * "Options" button. Safe to call repeatedly (e.g. after a theme change).
 */
void styleTopLevelWindow(juce::Component& content);

} // namespace chrome
} // namespace ui
} // namespace eeval
