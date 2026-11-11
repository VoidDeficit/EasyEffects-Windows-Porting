#pragma once
#include <JuceHeader.h>
#include "Theme.h"
#include <functional>

namespace eeval {
namespace ui {

/**
 * WinUI NavigationView (left mode): a hamburger button, the app name, the main
 * pages and footer pages. Collapses to an icon-only rail.
 *
 *   =  EasyEffects        <- menu button toggles the compact rail
 *   | Output              <- selected: subtle fill + accent pill
 *     Input
 *    ...
 *     Devices             (footer)
 *     Settings
 */
class NavigationPane : public juce::Component, public juce::TooltipClient {
public:
    struct Item {
        juce::String id;
        juce::String label;
        juce::juce_wchar glyph = 0;
        bool footer = false;
        juce::String tooltip;
    };

    std::function<void(const juce::String&)> onSelect;
    std::function<void()> onToggleCompact;

    void setItems(std::vector<Item> newItems) { items = std::move(newItems); repaint(); }

    void setSelected(const juce::String& id) {
        if (selectedId != id) { selectedId = id; repaint(); }
    }
    const juce::String& getSelected() const { return selectedId; }

    void setCompact(bool c) { if (compact != c) { compact = c; repaint(); } }
    bool isCompact() const { return compact; }

    void setAppTitle(const juce::String& t) { appTitle = t; repaint(); }

    static constexpr int expandedWidth = 240;
    static constexpr int compactWidth  = 48;
    static constexpr int itemHeight    = 40;

    void paint(juce::Graphics& g) override {
        g.fillAll(theme::windowBg);

        // Hamburger + app title
        auto top = menuButtonArea();
        if (hot == -2) {
            g.setColour(pressed ? theme::subtleFillDown : theme::subtleFill);
            g.fillRoundedRectangle(top.toFloat(), theme::radius);
        }
        theme::drawIcon(g, theme::icon::nav, top.toFloat(), theme::textPrimary, 16.0f);
        if (!compact) {
            g.setColour(theme::textPrimary);
            g.setFont(theme::font(13.0f));
            g.drawText(appTitle, juce::Rectangle<int>(top.getRight() + 8, top.getY(), getWidth() - top.getRight() - 16, top.getHeight()),
                       juce::Justification::centredLeft, true);
        }

        for (int i = 0; i < (int)items.size(); ++i) {
            const auto r = itemBounds(i);
            if (r.isEmpty()) continue;
            const auto& it = items[(size_t)i];
            const bool sel = it.id == selectedId;

            auto rf = r.toFloat();
            if (sel || hot == i) {
                g.setColour(hot == i && pressed ? theme::subtleFillDown
                            : (sel && hot == i ? theme::subtleFill.withMultipliedAlpha(1.5f) : theme::subtleFill));
                g.fillRoundedRectangle(rf, theme::radius);
            }
            if (sel) {
                g.setColour(theme::accentPrimary);
                g.fillRoundedRectangle(rf.getX(), rf.getCentreY() - 8.0f, 3.0f, 16.0f, 1.5f);
            }

            auto iconArea = juce::Rectangle<float>(rf.getX() + 4.0f, rf.getY(), 32.0f, rf.getHeight());
            theme::drawIcon(g, it.glyph, iconArea, theme::textPrimary, 16.0f);

            if (!compact) {
                g.setColour(theme::textPrimary);
                g.setFont(theme::body());
                g.drawText(it.label, r.withTrimmedLeft(44).withTrimmedRight(8), juce::Justification::centredLeft, true);
            }
        }

        // Footer separator
        const int firstFooter = firstFooterIndex();
        if (firstFooter >= 0) {
            auto r = itemBounds(firstFooter);
            g.setColour(theme::divider);
            g.fillRect(r.getX(), r.getY() - 5, r.getWidth(), 1);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override { setHot(hitTestItem(e.getPosition())); }
    void mouseExit(const juce::MouseEvent&) override { setHot(-1); }
    void mouseDown(const juce::MouseEvent& e) override {
        setHot(hitTestItem(e.getPosition()));
        pressed = true; repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override {
        pressed = false;
        const int h = hitTestItem(e.getPosition());
        repaint();
        if (h == -2) {
            if (onToggleCompact) onToggleCompact();
        } else if (h >= 0 && h < (int)items.size()) {
            const auto id = items[(size_t)h].id;
            setSelected(id);
            if (onSelect) onSelect(id);
        }
    }

    juce::String getTooltip() override { return getTooltipFor(getMouseXYRelative()); }

    juce::String getTooltipFor(juce::Point<int> p) const {
        const int h = hitTestItem(p);
        if (h >= 0 && h < (int)items.size())
            return compact ? items[(size_t)h].label : items[(size_t)h].tooltip;
        return {};
    }

private:
    juce::Rectangle<int> menuButtonArea() const { return { 4, 6, 40, 36 }; }

    int firstFooterIndex() const {
        for (int i = 0; i < (int)items.size(); ++i)
            if (items[(size_t)i].footer) return i;
        return -1;
    }

    juce::Rectangle<int> itemBounds(int index) const {
        const int w = getWidth() - 8;
        int topIdx = 0, footIdx = 0, footCount = 0;
        for (auto& it : items) if (it.footer) ++footCount;
        for (int i = 0; i < (int)items.size(); ++i) {
            if (!items[(size_t)i].footer) {
                if (i == index) return { 4, 52 + topIdx * (itemHeight + 4), w, itemHeight };
                ++topIdx;
            } else {
                if (i == index) return { 4, getHeight() - 8 - (footCount - footIdx) * (itemHeight + 4) + 4, w, itemHeight };
                ++footIdx;
            }
        }
        return {};
    }

    int hitTestItem(juce::Point<int> p) const {
        if (menuButtonArea().contains(p)) return -2;
        for (int i = 0; i < (int)items.size(); ++i)
            if (itemBounds(i).contains(p)) return i;
        return -1;
    }

    void setHot(int h) {
        if (h != hot) {
            hot = h;
            setMouseCursor(juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    std::vector<Item> items;
    juce::String selectedId;
    juce::String appTitle { "EasyEffects" };
    bool compact = false;
    int hot = -1;
    bool pressed = false;
};

} // namespace ui
} // namespace eeval
