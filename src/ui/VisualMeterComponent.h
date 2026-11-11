#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

namespace eeval {
namespace ui {

/**
 * VisualMeterComponent - A simple vertical meter for GR or VAD.
 */
class VisualMeterComponent : public juce::Component {
public:
    VisualMeterComponent(bool isInverted = false) : inverted(isInverted) {}

    void setValue(float newValue) {
        value = juce::jlimit(0.0f, 1.0f, newValue);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto area = getLocalBounds().toFloat();
        
        // Background
        g.setColour(theme::plotBg);
        g.fillRoundedRectangle(area, 2.0f);

        // Fill
        g.setColour(inverted ? theme::accentWarning : theme::accentSuccess);
        
        if (inverted) {
            // GR meter (fills from top down)
            g.fillRoundedRectangle(area.removeFromTop(area.getHeight() * value), 2.0f);
        } else {
            // Normal meter (fills from bottom up)
            g.fillRoundedRectangle(area.removeFromBottom(area.getHeight() * value), 2.0f);
        }
    }

private:
    float value = 0.0f;
    bool inverted = false;
};

} // namespace ui
} // namespace eeval
