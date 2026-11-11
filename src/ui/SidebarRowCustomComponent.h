#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include <functional>

namespace eeval {
namespace ui {

/**
 * One effect in the sidebar, styled after the EasyEffects 8 plugin list:
 *   [↓] Effect name ............................ [⋮] [⇅]
 * - click the row to open the effect
 * - ⋮ opens a menu (enable/disable, move up/down, remove)
 * - drag ⇅ up/down to reorder
 * Bypassed effects are shown dimmed with an "off" tag.
 */
class SidebarRowCustomComponent : public juce::Component {
public:
    SidebarRowCustomComponent(juce::AudioProcessorValueTreeState& apvtsToUse,
                              int slotIdx,
                              const std::string& typeId,
                              const std::string& name,
                              int rowNum,
                              std::function<void(int)> onRemove = nullptr,
                              std::function<void(int)> onMoveUp = nullptr,
                              std::function<void(int)> onMoveDown = nullptr,
                              std::function<void(int, int)> onMoveBy = nullptr)
        : apvts(apvtsToUse), slotIndex(slotIdx), effectTypeId(typeId),
          rowNumber(rowNum), displayName(name),
          removeCallback(std::move(onRemove)), moveUpCallback(std::move(onMoveUp)),
          moveDownCallback(std::move(onMoveDown)), moveByCallback(std::move(onMoveBy))
    {
        setRepaintsOnMouseActivity(true);
    }

    ~SidebarRowCustomComponent() override = default;

    void paint(juce::Graphics& g) override {
        auto row = getLocalBounds().reduced(6, 2).toFloat();
        const bool bypassed = isBypassed();

        // Background: selected = Breeze highlight, hover = subtle
        if (isSelected) {
            g.setColour(theme::accentPrimary.withAlpha(0.22f));
            g.fillRoundedRectangle(row, theme::radius);
            g.setColour(theme::accentPrimary.withAlpha(0.85f));
            g.drawRoundedRectangle(row.reduced(0.5f), theme::radius, 1.0f);
        } else if (isMouseOver(true)) {
            g.setColour(theme::bgOverlay);
            g.fillRoundedRectangle(row, theme::radius);
        }

        // Drag feedback: show where the row will land
        if (dragging && dragDelta != 0) {
            g.setColour(theme::accentPrimary);
            const float y = dragDelta < 0 ? row.getY() : row.getBottom() - 2.0f;
            g.fillRect(row.getX(), y, row.getWidth(), 2.0f);
        }

        auto area = getLocalBounds().reduced(6, 2);
        area.removeFromLeft(8);

        // Flow arrow (↓) like EasyEffects
        auto arrowArea = area.removeFromLeft(16).toFloat();
        drawFlowArrow(g, arrowArea, bypassed ? theme::textDisabled : theme::textSecondary);
        area.removeFromLeft(6);

        // Right-hand controls
        auto handleArea = area.removeFromRight(handleWidth);
        auto menuArea = area.removeFromRight(menuWidth);
        drawMenuDots(g, menuArea.toFloat(), hotZone == Zone::menu ? theme::textPrimary : theme::textSecondary);
        drawDragHandle(g, handleArea.toFloat(), (hotZone == Zone::handle || dragging) ? theme::textPrimary : theme::textSecondary);

        // "off" tag for bypassed effects
        if (bypassed) {
            auto tag = area.removeFromRight(30).withSizeKeepingCentre(28, 16).toFloat();
            g.setColour(theme::bgOverlay);
            g.fillRoundedRectangle(tag, 8.0f);
            g.setColour(theme::textSecondary);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText("off", tag, juce::Justification::centred, false);
            area.removeFromRight(4);
        }

        // Name
        g.setColour(bypassed ? theme::textDisabled
                             : (isSelected ? theme::textPrimary : theme::textPrimary.withAlpha(0.9f)));
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText(displayName, area, juce::Justification::centredLeft, true);
    }

    void mouseMove(const juce::MouseEvent& e) override {
        auto z = zoneAt(e.x);
        if (z != hotZone) { hotZone = z; repaint(); }
        setMouseCursor(z == Zone::handle ? juce::MouseCursor::UpDownResizeCursor
                                         : juce::MouseCursor::NormalCursor);
    }

    void mouseExit(const juce::MouseEvent&) override {
        hotZone = Zone::none;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override {
        switch (zoneAt(e.x)) {
            case Zone::menu:
                showRowMenu();
                return;
            case Zone::handle:
                dragging = true;
                dragDelta = 0;
                return;
            default:
                if (auto* listBox = findParentComponentOfClass<juce::ListBox>())
                    listBox->selectRow(rowNumber);
                return;
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (!dragging) return;
        const int rowH = juce::jmax(1, getHeight());
        const int d = juce::roundToInt((float)e.getDistanceFromDragStartY() / (float)rowH);
        if (d != dragDelta) { dragDelta = d; repaint(); }
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (!dragging) return;
        dragging = false;
        const int d = dragDelta;
        dragDelta = 0;
        repaint();

        // The callback rebuilds the list (and deletes this row) — do nothing after it.
        if (d != 0 && moveByCallback) {
            auto cb = moveByCallback;
            const int slot = slotIndex;
            cb(slot, d);
        }
    }

    void resized() override {}

    void update(bool selected) {
        isSelected = selected;
        repaint();
    }

    int getRowNumber() const { return rowNumber; }

private:
    enum class Zone { none, body, menu, handle };

    static constexpr int menuWidth = 26;
    static constexpr int handleWidth = 26;

    Zone zoneAt(int x) const {
        const int right = getWidth() - 6;
        if (x >= right - handleWidth) return Zone::handle;
        if (x >= right - handleWidth - menuWidth) return Zone::menu;
        return Zone::body;
    }

    bool isBypassed() const {
        auto* param = apvts.getRawParameterValue("slot" + std::to_string(slotIndex) + ".bypass");
        return param != nullptr && param->load() > 0.5f;
    }

    void showRowMenu() {
        const bool bypassed = isBypassed();

        juce::PopupMenu menu;
        menu.addItem(1, bypassed ? "Enable" : "Disable (bypass)");
        menu.addSeparator();
        menu.addItem(2, "Move up");
        menu.addItem(3, "Move down");
        menu.addSeparator();
        menu.addItem(4, "Remove");

        // Copy everything the callback needs: the row may be rebuilt meanwhile.
        auto& vts = apvts;
        const int slot = slotIndex;
        auto onUp = moveUpCallback, onDown = moveDownCallback, onRemove = removeCallback;
        juce::Component::SafePointer<SidebarRowCustomComponent> safeThis(this);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [&vts, slot, onUp, onDown, onRemove, safeThis](int result) {
                if (result == 1) {
                    if (auto* p = vts.getParameter("slot" + std::to_string(slot) + ".bypass"))
                        p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.0f : 1.0f);
                    if (safeThis != nullptr) safeThis->repaint();
                } else if (result == 2 && onUp) {
                    onUp(slot);
                } else if (result == 3 && onDown) {
                    onDown(slot);
                } else if (result == 4 && onRemove) {
                    onRemove(slot);
                }
            });
    }

    static void drawFlowArrow(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) {
        const float cx = r.getCentreX(), cy = r.getCentreY();
        juce::Path p;
        p.startNewSubPath(cx, cy - 6.0f);
        p.lineTo(cx, cy + 5.0f);
        p.startNewSubPath(cx - 4.0f, cy + 1.0f);
        p.lineTo(cx, cy + 5.0f);
        p.lineTo(cx + 4.0f, cy + 1.0f);
        g.setColour(c);
        g.strokePath(p, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    static void drawMenuDots(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) {
        const float cx = r.getCentreX(), cy = r.getCentreY();
        g.setColour(c);
        for (int i = -1; i <= 1; ++i)
            g.fillEllipse(cx - 1.6f, cy + (float)i * 5.0f - 1.6f, 3.2f, 3.2f);
    }

    static void drawDragHandle(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) {
        const float cx = r.getCentreX(), cy = r.getCentreY();
        juce::Path p;
        // up chevron
        p.startNewSubPath(cx - 4.0f, cy - 3.0f); p.lineTo(cx, cy - 7.0f); p.lineTo(cx + 4.0f, cy - 3.0f);
        // down chevron
        p.startNewSubPath(cx - 4.0f, cy + 3.0f); p.lineTo(cx, cy + 7.0f); p.lineTo(cx + 4.0f, cy + 3.0f);
        g.setColour(c);
        g.strokePath(p, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::AudioProcessorValueTreeState& apvts;
    int slotIndex;
    std::string effectTypeId;
    int rowNumber;
    juce::String displayName;
    bool isSelected = false;

    Zone hotZone = Zone::none;
    bool dragging = false;
    int dragDelta = 0;

    std::function<void(int)> removeCallback;
    std::function<void(int)> moveUpCallback;
    std::function<void(int)> moveDownCallback;
    std::function<void(int, int)> moveByCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidebarRowCustomComponent)
};

} // namespace ui
} // namespace eeval
