#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include <functional>

namespace eeval {
namespace ui {

/**
 * One effect in the effects list, styled as a WinUI ListView item:
 *
 *   | Effect name ....................... [on/off] [...] [drag]
 *
 * - click the row to open the effect (selected: subtle fill + accent pill)
 * - the ToggleSwitch turns the effect on/off (bypass)
 * - the "..." button opens a menu (reset, move up/down, remove)
 * - drag the handle up/down to reorder
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
                              std::function<void(int, int)> onMoveBy = nullptr,
                              std::function<void(int)> onReset = nullptr)
        : apvts(apvtsToUse), slotIndex(slotIdx), effectTypeId(typeId),
          rowNumber(rowNum), displayName(name),
          removeCallback(std::move(onRemove)), moveUpCallback(std::move(onMoveUp)),
          moveDownCallback(std::move(onMoveDown)), moveByCallback(std::move(onMoveBy)),
          resetCallback(std::move(onReset))
    {
        setRepaintsOnMouseActivity(true);
    }

    ~SidebarRowCustomComponent() override = default;

    void paint(juce::Graphics& g) override {
        auto row = getLocalBounds().reduced(4, 2).toFloat();
        const bool bypassed = isBypassed();
        const bool over = isMouseOver(true);

        if (isSelected || over) {
            g.setColour(over && isMouseButtonDown() ? theme::subtleFillDown
                        : (isSelected && over ? theme::subtleFill.withMultipliedAlpha(1.5f) : theme::subtleFill));
            g.fillRoundedRectangle(row, theme::radius);
        }
        if (isSelected) {
            g.setColour(theme::accentPrimary);
            g.fillRoundedRectangle(row.getX(), row.getCentreY() - 8.0f, 3.0f, 16.0f, 1.5f);
        }

        // Drag feedback: show where the row will land
        if (dragging && dragDelta != 0) {
            g.setColour(theme::accentPrimary);
            const float y = dragDelta < 0 ? row.getY() : row.getBottom() - 2.0f;
            g.fillRoundedRectangle(row.getX(), y, row.getWidth(), 2.0f, 1.0f);
        }

        auto area = getLocalBounds().reduced(4, 2);
        area.removeFromLeft(14);

        auto handleArea = area.removeFromRight(handleWidth);
        auto menuArea   = area.removeFromRight(menuWidth);
        auto switchArea = area.removeFromRight(switchWidth);

        if (over || dragging)
            theme::drawIcon(g, theme::icon::gripper, handleArea.toFloat(),
                            (hotZone == Zone::handle || dragging) ? theme::textPrimary : theme::textTertiary, 14.0f);

        if (hotZone == Zone::menu) {
            g.setColour(theme::subtleFill);
            g.fillRoundedRectangle(menuArea.toFloat().withSizeKeepingCentre(28.0f, 28.0f), theme::radius);
        }
        theme::drawIcon(g, theme::icon::more, menuArea.toFloat(),
                        hotZone == Zone::menu ? theme::textPrimary : theme::textSecondary, 14.0f);

        theme::drawSwitch(g, switchArea.toFloat(), !bypassed, hotZone == Zone::toggle);

        area.removeFromRight(6);
        g.setColour(bypassed ? theme::textDisabled : theme::textPrimary);
        g.setFont(isSelected ? theme::bodyStrong() : theme::body());
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
            case Zone::toggle:
                if (auto* p = apvts.getParameter("slot" + std::to_string(slotIndex) + ".bypass"))
                    p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.0f : 1.0f);
                repaint();
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
    enum class Zone { none, body, toggle, menu, handle };

    static constexpr int switchWidth = 48;
    static constexpr int menuWidth = 32;
    static constexpr int handleWidth = 26;

    Zone zoneAt(int x) const {
        const int right = getWidth() - 4;
        if (x >= right - handleWidth) return Zone::handle;
        if (x >= right - handleWidth - menuWidth) return Zone::menu;
        if (x >= right - handleWidth - menuWidth - switchWidth) return Zone::toggle;
        return Zone::body;
    }

    bool isBypassed() const {
        auto* param = apvts.getRawParameterValue("slot" + std::to_string(slotIndex) + ".bypass");
        return param != nullptr && param->load() > 0.5f;
    }

    void showRowMenu() {
        const bool bypassed = isBypassed();

        juce::PopupMenu menu;
        menu.addItem(1, bypassed ? "Turn on" : "Turn off (bypass)");
        menu.addItem(5, "Reset to defaults", resetCallback != nullptr);
        menu.addSeparator();
        menu.addItem(2, "Move up", rowNumber > 0);
        menu.addItem(3, "Move down");
        menu.addSeparator();
        menu.addItem(4, "Remove", true, false);

        // Copy everything the callback needs: the row may be rebuilt meanwhile.
        auto& vts = apvts;
        const int slot = slotIndex;
        auto onUp = moveUpCallback, onDown = moveDownCallback, onRemove = removeCallback, onReset = resetCallback;
        juce::Component::SafePointer<SidebarRowCustomComponent> safeThis(this);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [&vts, slot, onUp, onDown, onRemove, onReset, safeThis](int result) {
                if (result == 1) {
                    if (auto* p = vts.getParameter("slot" + std::to_string(slot) + ".bypass"))
                        p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.0f : 1.0f);
                    if (safeThis != nullptr) safeThis->repaint();
                } else if (result == 2 && onUp) {
                    onUp(slot);
                } else if (result == 3 && onDown) {
                    onDown(slot);
                } else if (result == 5 && onReset) {
                    onReset(slot);
                } else if (result == 4 && onRemove) {
                    onRemove(slot);
                }
            });
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
    std::function<void(int)> resetCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidebarRowCustomComponent)
};

} // namespace ui
} // namespace eeval
