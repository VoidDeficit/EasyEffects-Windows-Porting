#include "GenericModuleEditor.h"
#include "VisualMeterComponent.h"
#include "../PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace eeval {
namespace ui {

GenericModuleEditor::GenericModuleEditor(juce::AudioProcessorValueTreeState& stateToUse,
                                         int slotIdx,
                                         const std::string& typeId,
                                         const std::string& dispName)
    : apvts(stateToUse), slotIndex(slotIdx), effectTypeId(typeId), displayName(dispName)
{
    std::string prefix = "slot" + std::to_string(slotIndex) + "." + effectTypeId;

    const auto* desc = EffectRegistry::findType(effectTypeId);
    if (!desc) return;

    for (const auto& pdesc : desc->params) {
        std::string fullId = prefix + "." + pdesc.suffix;
        auto* slider = new juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 96, rowHeight - 4);
        slider->setTextValueSuffix(pdesc.unit.empty() ? juce::String() : " " + juce::String(pdesc.unit));

        auto* label = new juce::Label(fullId + "_label", pdesc.label);
        label->setJustificationType(juce::Justification::centredLeft);
        label->setColour(juce::Label::textColourId, theme::textPrimary);
        label->setFont(theme::body());

        addAndMakeVisible(slider);
        addAndMakeVisible(label);
        sliders.add(slider);
        labels.add(label);

        attachments.add(new juce::AudioProcessorValueTreeState::SliderAttachment(apvts, fullId, *slider));
    }

    for (const auto& cdesc : desc->choices) {
        std::string fullId = prefix + "." + cdesc.suffix;
        auto* combo = new juce::ComboBox();
        for (int i = 0; i < (int)cdesc.choices.size(); ++i)
            combo->addItem(cdesc.choices[i], i + 1);
        combo->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(combo);

        auto* label = new juce::Label(fullId + "_label", cdesc.label);
        label->setJustificationType(juce::Justification::centredLeft);
        label->setColour(juce::Label::textColourId, theme::textPrimary);
        label->setFont(theme::body());
        addAndMakeVisible(label);

        choiceLabels.add(label);
        comboBoxes.add(combo);
        comboAttachments.add(new juce::AudioProcessorValueTreeState::ComboBoxAttachment(apvts, fullId, *combo));
    }

    // Add Mix slider
    if (effectTypeId != "levelmeter") {
        std::string mixId = "slot" + std::to_string(slotIndex) + ".mix";
        auto* mixSlider = new juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        mixSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 96, rowHeight - 4);
        mixSlider->setTextValueSuffix(" %");
        addAndMakeVisible(mixSlider);
        sliders.add(mixSlider);
        auto* mixLabel = new juce::Label(mixId + "_label", "Dry/Wet Mix");
        mixLabel->setColour(juce::Label::textColourId, theme::textPrimary);
        mixLabel->setFont(theme::body());
        labels.add(mixLabel);
        addAndMakeVisible(mixLabel);
        attachments.add(new juce::AudioProcessorValueTreeState::SliderAttachment(apvts, mixId, *mixSlider));
    }

    // Preset Button
    presetBtn.setButtonText("Presets");
    presetBtn.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible(presetBtn);

    // Determine if we need meters
    showGrMeter = (effectTypeId == "compressor" || effectTypeId == "gate" || effectTypeId == "expander");
    showVadMeter = (effectTypeId == "rnnoise");

    if (showGrMeter) addAndMakeVisible(grMeter);
    if (showVadMeter) addAndMakeVisible(vadMeter);

    if (showGrMeter) {
        showDynamicsViz = true;
        dynamicsViz = std::make_unique<DynamicsVisualizer>(apvts, prefix + ".", effectTypeId);
        addAndMakeVisible(*dynamicsViz);
    }

    if (showDynamicsViz)   sideCardTitle = "Dynamics";
    else if (showVadMeter) sideCardTitle = "Voice activity";

    // Lets the EffectPage / viewport grow to fit every control row.
    getProperties().set("preferredHeight", computePreferredHeight());

    startTimerHz(30); // 30 FPS updates
}

int GenericModuleEditor::computePreferredHeight() const {
    const int rows = comboBoxes.size() + sliders.size();
    const int header = 46;
    const int cardChrome = 8 + 26 + 4 + 8 + 12;   // card padding + title + margins
    int h = header + cardChrome + rows * (rowHeight + rowGap) + 12;
    if (showDynamicsViz)
        h = juce::jmax(h, header + cardChrome + 260);
    return h;
}

void GenericModuleEditor::timerCallback() {
    auto* processor = dynamic_cast<EasyEffectsAudioProcessor*>(&apvts.processor);
    if (!processor) return;

    auto* module = processor->getChain().getSlotModule(slotIndex);
    if (!module) return;

    if (showGrMeter) grMeter.setValue(module->getGainReduction());
    if (showVadMeter) vadMeter.setValue(module->getVADProbability());

    if (showDynamicsViz && dynamicsViz) {
        float inDb = module->getInputLevel();

        float outDb = module->getOutputLevel();      // measured after the effect
        dynamicsViz->setCurrentLevels(inDb, outDb);
    }
}

void GenericModuleEditor::showPresetMenu() {
    auto* processor = dynamic_cast<EasyEffectsAudioProcessor*>(&apvts.processor);
    if (!processor) return;

    juce::PopupMenu menu;
    menu.addItem(1, "Save As...");
    menu.addSeparator();

    auto presets = processor->getPresetManager().getModulePresetList(slotIndex);
    for (int i = 0; i < presets.size(); ++i) {
        juce::PopupMenu itemMenu;
        itemMenu.addItem(100 + i, "Load");
        itemMenu.addItem(200 + i, "Delete");
        menu.addSubMenu(presets[i], itemMenu);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetBtn), [this, processor, presets](int result) {
        if (result == 1) {
            // Save As
            auto* alert = new juce::AlertWindow("Save " + displayName + " Preset", "Enter preset name:", juce::AlertWindow::NoIcon);
            alert->addTextEditor("name", "", "Preset Name");
            alert->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
            alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            
            alert->enterModalState(true, juce::ModalCallbackFunction::create([this, processor, alert](int res) {
                if (res == 1) {
                    auto name = alert->getTextEditorContents("name").toStdString();
                    if (!name.empty())
                        processor->getPresetManager().saveModulePreset(slotIndex, name);
                }
                delete alert;
            }));
        } else if (result >= 200) {
            // Delete with confirmation
            auto name = presets[result - 200].toStdString();
            auto opts = juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Delete Preset")
                .withMessage("Delete \"" + juce::String(name) + "\"?")
                .withButton("Delete")
                .withButton("Cancel");
            juce::AlertWindow::showAsync(opts, [this, processor, name](int r) {
                if (r == 1) {
                    processor->getPresetManager().deleteModulePreset(slotIndex, name);
                }
            });
        } else if (result >= 100) {
            // Load
            auto name = presets[result - 100].toStdString();
            processor->getPresetManager().loadModulePreset(slotIndex, name);
        }
    });
}

void GenericModuleEditor::paint(juce::Graphics& g) {
    g.fillAll(theme::bgBase);

    // Page title (effect name), like the plugin header in EasyEffects
    g.setColour(theme::textPrimary);
    g.setFont(theme::subtitle());
    g.drawText(displayName, headerArea, juce::Justification::centredLeft, true);

    theme::drawCard(g, controlsCard, "Controls");
    if (!sideCard.isEmpty())
        theme::drawCard(g, sideCard, sideCardTitle);
}

void GenericModuleEditor::resized() {
    auto area = getLocalBounds().reduced(14, 10);

    // Header: title + Presets button
    headerArea = area.removeFromTop(34);
    presetBtn.setBounds(headerArea.removeFromRight(96).withSizeKeepingCentre(96, 30));
    area.removeFromTop(12);

    // Cards: "Controls" on the left, optional side card on the right
    const bool hasSide = showDynamicsViz || showVadMeter;
    if (hasSide) {
        const int sideW = showDynamicsViz ? juce::jlimit(240, 420, area.getWidth() * 2 / 5) : 150;
        sideCard = area.removeFromRight(sideW);
        area.removeFromRight(12);
    } else {
        sideCard = {};
    }
    controlsCard = area;

    // --- Controls card content (mirror of theme::drawCard's content area) ---
    auto inner = controlsCard.reduced(12, 8);
    inner.removeFromTop(26 + 4);
    inner.removeFromTop(4);

    auto placeRow = [&inner](juce::Component& label, juce::Component& control, int maxControlWidth) {
        auto row = inner.removeFromTop(rowHeight);
        inner.removeFromTop(rowGap);
        label.setBounds(row.removeFromLeft(labelWidth));
        row.removeFromLeft(8);
        control.setBounds(maxControlWidth > 0 ? row.removeFromLeft(juce::jmin(row.getWidth(), maxControlWidth)) : row);
    };

    for (int i = 0; i < comboBoxes.size(); ++i)
        placeRow(*choiceLabels[i], *comboBoxes[i], 240);

    for (int i = 0; i < sliders.size(); ++i)
        placeRow(*labels[i], *sliders[i], 0);

    // --- Side card content ---
    if (hasSide) {
        auto side = sideCard.reduced(12, 8);
        side.removeFromTop(26 + 4 + 4);

        if (showGrMeter || showVadMeter) {
            auto meterArea = side.removeFromRight(24);
            if (showGrMeter)  grMeter.setBounds(meterArea);
            if (showVadMeter) vadMeter.setBounds(meterArea.withSizeKeepingCentre(24, juce::jmin(meterArea.getHeight(), 220)));
            side.removeFromRight(8);
        }

        if (showDynamicsViz && dynamicsViz)
            dynamicsViz->setBounds(side.withHeight(juce::jmin(side.getHeight(), side.getWidth())));
    }
}

} // namespace ui
} // namespace eeval
