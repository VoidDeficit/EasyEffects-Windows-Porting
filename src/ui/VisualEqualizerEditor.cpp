#include "VisualEqualizerEditor.h"
#include "../PluginProcessor.h"
#include "../dsp/EffectRegistry.h"

namespace eeval {
namespace ui {

VisualEqualizerEditor::VisualEqualizerEditor(EasyEffectsAudioProcessor& p, juce::AudioProcessorValueTreeState& vts, int slotIndex)
    : audioProcessor(p), apvts(vts), slotIdx(slotIndex), plot(p, vts, slotIndex)
{
    addAndMakeVisible(plot);

    titleLabel.setText("Parametric Equalizer", juce::dontSendNotification);
    titleLabel.setFont(theme::subtitle());
    titleLabel.setColour(juce::Label::textColourId, theme::textPrimary);
    addAndMakeVisible(titleLabel);

    getProperties().set("preferredHeight", 470);

    for (int i = 0; i < 10; ++i) {
        auto node = std::make_unique<EQNodeComponent>(apvts, slotIdx, i, plot);
        // Children of the plot: node coordinates are then plot-relative, which is
        // what EQNodeComponent's drag maths and setCentreRelative() assume.
        plot.addAndMakeVisible(*node);
        nodes.push_back(std::move(node));
    }

    addBandBtn.onClick = [this] { addNextBand(); };
    addAndMakeVisible(addBandBtn);

    presetBtn.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible(presetBtn);
}

void VisualEqualizerEditor::paint(juce::Graphics& g) {
    g.fillAll(theme::bgBase);
    theme::drawCard(g, plotCard, "Bands");
}

void VisualEqualizerEditor::resized() {
    auto area = getLocalBounds().reduced(14, 10);

    auto header = area.removeFromTop(34);
    titleLabel.setBounds(header.removeFromLeft(260));
    addBandBtn.setBounds(header.removeFromRight(110).withSizeKeepingCentre(110, 30));
    header.removeFromRight(8);
    presetBtn.setBounds(header.removeFromRight(96).withSizeKeepingCentre(96, 30));
    area.removeFromTop(12);

    // The graph sits inside an EasyEffects-style card.
    plotCard = area;
    auto content = plotCard.reduced(12, 8);
    content.removeFromTop(26 + 4);
    plot.setBounds(content);

    // Position nodes
    for (auto& node : nodes) {
        node->updatePosition();
    }
}

void VisualEqualizerEditor::addNextBand() {
    std::string prefix = eeval::EffectRegistry::slotPrefix(slotIdx) + ".eq.band";
    for (int i = 0; i < 10; ++i) {
        auto* p = apvts.getParameter(prefix + std::to_string(i) + ".enabled");
        if (p && p->getValue() < 0.5f) {
            p->setValueNotifyingHost(1.0f);
            nodes[i]->updatePosition();
            plot.repaint();
            break;
        }
    }
}

void VisualEqualizerEditor::showPresetMenu() {
    auto& pm = audioProcessor.getPresetManager();
    juce::PopupMenu menu;

    menu.addItem(1, "Save As...");
    menu.addSeparator();

    // Load presets
    auto presets = pm.getModulePresetList(slotIdx);
    for (int i = 0; i < presets.size(); ++i) {
        juce::PopupMenu itemMenu;
        itemMenu.addItem(100 + i, "Load");
        itemMenu.addItem(200 + i, "Delete");
        menu.addSubMenu(presets[i], itemMenu);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetBtn),
        [this, &pm, presets](int result) {
            if (result == 1) {
                // Save As
                auto* aw = new juce::AlertWindow("Save EQ Preset", "Enter preset name:", juce::MessageBoxIconType::NoIcon);
                aw->addTextEditor("name", "", "Name:");
                aw->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
                aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                aw->enterModalState(true, juce::ModalCallbackFunction::create([this, aw](int res) {
                    if (res == 1) {
                        auto name = aw->getTextEditorContents("name").toStdString();
                        if (!name.empty())
                            audioProcessor.getPresetManager().saveModulePreset(slotIdx, name);
                    }
                    delete aw;
                }));
            } else if (result >= 200) {
                // Delete
                auto name = presets[result - 200].toStdString();
                auto opts = juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle("Delete Preset")
                    .withMessage("Delete \"" + juce::String(name) + "\"?")
                    .withButton("Delete")
                    .withButton("Cancel");
                juce::AlertWindow::showAsync(opts, [this, name](int r) {
                    if (r == 1) {
                        audioProcessor.getPresetManager().deleteModulePreset(slotIdx, name);
                    }
                });
            } else if (result >= 100) {
                // Load
                pm.loadModulePreset(slotIdx, presets[result - 100].toStdString());
                // Refresh nodes
                for (auto& node : nodes)
                    node->updatePosition();
                plot.repaint();
            }
        });
}

} // namespace ui
} // namespace eeval
