#include "PluginSampleInspectorCardLayout.h"

PluginSampleInspectorCardLayout::PluginSampleInspectorCardLayout(juce::Component& fadeInDial,
                                                                 juce::Component& fadeOutDial,
                                                                 juce::Component& preloadDial,
                                                                 juce::Component& masterVolumeDial,
                                                                 juce::Component& panDial,
                                                                 juce::Component& outputLevelMeter,
                                                                 juce::Component& qualityLabel,
                                                                 juce::Button& qualityCpuButton,
                                                                 juce::Button& qualityFidelityButton,
                                                                 juce::Button& qualityUltraButton,
                                                                 juce::Component& reverbMixDial,
                                                                 juce::Component& delayTimeDial,
                                                                 juce::Component& delayFeedbackDial,
                                                                 juce::Component& delayMixDial,
                                                                 juce::Button& delayTempoSyncToggle,
                                                                 juce::Button& dcFilterEnabledToggle,
                                                                 juce::Component& dcFilterCutoffDial,
                                                                 juce::Component& autopanRateDial,
                                                                 juce::Component& autopanDepthDial,
                                                                 juce::Component& saturationDriveDial,
                                                                 juce::ComboBox& saturationModeCombo,
                                                                 juce::Component& filterAttackDial,
                                                                 juce::Component& filterDecayDial,
                                                                 juce::Component& filterSustainDial,
                                                                 juce::Component& filterReleaseDial,
                                                                 juce::Component& filterEnvelopeGraph,
                                                                 juce::Component& filterKeytrackDial,
                                                                 juce::Component& filterVelDial,
                                                                 juce::Component& filterLfoRateDial,
                                                                 juce::Component& filterLfoAmtDial,
                                                                 juce::Component& filterLfoShapeLabel,
                                                                 juce::ComboBox& filterLfoShapeCombo,
                                                                 juce::Button& filterLfoRetriggerToggle,
                                                                 juce::Button& filterLfoTempoSyncToggle,
                                                                 juce::Component& filterLfoDivisionLabel,
                                                                 juce::ComboBox& filterLfoDivisionCombo)
    : fadeInDial_(fadeInDial),
      fadeOutDial_(fadeOutDial),
      preloadDial_(preloadDial),
      masterVolumeDial_(masterVolumeDial),
      panDial_(panDial),
      outputLevelMeter_(outputLevelMeter),
      qualityLabel_(qualityLabel),
      qualityCpuButton_(qualityCpuButton),
      qualityFidelityButton_(qualityFidelityButton),
      qualityUltraButton_(qualityUltraButton),
      reverbMixDial_(reverbMixDial),
      delayTimeDial_(delayTimeDial),
      delayFeedbackDial_(delayFeedbackDial),
      delayMixDial_(delayMixDial),
      delayTempoSyncToggle_(delayTempoSyncToggle),
      dcFilterEnabledToggle_(dcFilterEnabledToggle),
      dcFilterCutoffDial_(dcFilterCutoffDial),
      autopanRateDial_(autopanRateDial),
      autopanDepthDial_(autopanDepthDial),
      saturationDriveDial_(saturationDriveDial),
      saturationModeCombo_(saturationModeCombo),
      filterAttackDial_(filterAttackDial),
      filterDecayDial_(filterDecayDial),
      filterSustainDial_(filterSustainDial),
      filterReleaseDial_(filterReleaseDial),
      filterEnvelopeGraph_(filterEnvelopeGraph),
      filterKeytrackDial_(filterKeytrackDial),
      filterVelDial_(filterVelDial),
      filterLfoRateDial_(filterLfoRateDial),
      filterLfoAmtDial_(filterLfoAmtDial),
      filterLfoShapeLabel_(filterLfoShapeLabel),
      filterLfoShapeCombo_(filterLfoShapeCombo),
      filterLfoRetriggerToggle_(filterLfoRetriggerToggle),
      filterLfoTempoSyncToggle_(filterLfoTempoSyncToggle),
      filterLfoDivisionLabel_(filterLfoDivisionLabel),
      filterLfoDivisionCombo_(filterLfoDivisionCombo)
{
}

void PluginSampleInspectorCardLayout::layoutOutput(juce::Rectangle<int> inspectorBounds) const
{
    auto outputInner = inspectorBounds.withTrimmedTop(30).reduced(12, 10);
    constexpr int kInspectorDialGap = 6;
    constexpr int kCompactRowHeight = 68;
    constexpr int kSmallDialWidth = 68;

    auto row1 = outputInner.removeFromTop(kCompactRowHeight);
    fadeInDial_.setBounds(row1.removeFromLeft(kSmallDialWidth));
    row1.removeFromLeft(kInspectorDialGap);
    fadeOutDial_.setBounds(row1.removeFromLeft(kSmallDialWidth));
    row1.removeFromLeft(kInspectorDialGap);
    preloadDial_.setBounds(row1);

    outputInner.removeFromTop(6);
    auto row2 = outputInner.removeFromTop(kCompactRowHeight);
    masterVolumeDial_.setBounds(row2.removeFromLeft(kSmallDialWidth));
    row2.removeFromLeft(kInspectorDialGap);
    panDial_.setBounds(row2.removeFromLeft(kSmallDialWidth));
    row2.removeFromLeft(kInspectorDialGap);
    outputLevelMeter_.setBounds(row2.reduced(0, 10));

    outputInner.removeFromTop(8);
    qualityLabel_.setBounds({});
    auto qualityRow = outputInner.removeFromTop(24);
    const int qualityButtonWidth = (qualityRow.getWidth() - kInspectorDialGap * 2) / 3;
    qualityCpuButton_.setBounds(qualityRow.removeFromLeft(qualityButtonWidth));
    qualityRow.removeFromLeft(kInspectorDialGap);
    qualityFidelityButton_.setBounds(qualityRow.removeFromLeft(qualityButtonWidth));
    qualityRow.removeFromLeft(kInspectorDialGap);
    qualityUltraButton_.setBounds(qualityRow);
}

void PluginSampleInspectorCardLayout::layoutEffects(juce::Rectangle<int> inspectorBounds) const
{
    auto effectsInner = inspectorBounds.withTrimmedTop(30).reduced(12, 10);
    constexpr int kInspectorGap = 6;

    auto topRow = effectsInner.removeFromTop(68);
    const int topCellWidth = (topRow.getWidth() - kInspectorGap * 3) / 4;
    reverbMixDial_.setBounds(topRow.removeFromLeft(topCellWidth));
    topRow.removeFromLeft(kInspectorGap);
    delayTimeDial_.setBounds(topRow.removeFromLeft(topCellWidth));
    topRow.removeFromLeft(kInspectorGap);
    delayFeedbackDial_.setBounds(topRow.removeFromLeft(topCellWidth));
    topRow.removeFromLeft(kInspectorGap);
    delayMixDial_.setBounds(topRow);

    effectsInner.removeFromTop(8);
    auto middleRow = effectsInner.removeFromTop(24);
    delayTempoSyncToggle_.setBounds(middleRow.removeFromLeft(132));
    middleRow.removeFromLeft(8);
    saturationModeCombo_.setBounds(middleRow.removeFromRight(96));

    effectsInner.removeFromTop(8);
    auto bottomRow = effectsInner.removeFromTop(68);
    const int bottomCellWidth = (bottomRow.getWidth() - kInspectorGap * 3) / 4;
    dcFilterCutoffDial_.setBounds(bottomRow.removeFromLeft(bottomCellWidth));
    bottomRow.removeFromLeft(kInspectorGap);
    autopanRateDial_.setBounds(bottomRow.removeFromLeft(bottomCellWidth));
    bottomRow.removeFromLeft(kInspectorGap);
    autopanDepthDial_.setBounds(bottomRow.removeFromLeft(bottomCellWidth));
    bottomRow.removeFromLeft(kInspectorGap);
    saturationDriveDial_.setBounds(bottomRow);

    effectsInner.removeFromTop(8);
    dcFilterEnabledToggle_.setBounds(effectsInner.removeFromTop(24));
}

void PluginSampleInspectorCardLayout::layoutFilterMod(juce::Rectangle<int> inspectorBounds) const
{
    auto filterModInner = inspectorBounds.withTrimmedTop(30).reduced(12, 10);
    constexpr int kInspectorGap = 6;

    auto graphRow = filterModInner.removeFromTop(40);
    filterEnvelopeGraph_.setBounds(graphRow);

    filterModInner.removeFromTop(6);
    auto envRow = filterModInner.removeFromTop(54);
    const int envCellWidth = (envRow.getWidth() - kInspectorGap * 3) / 4;
    filterAttackDial_.setBounds(envRow.removeFromLeft(envCellWidth));
    envRow.removeFromLeft(kInspectorGap);
    filterDecayDial_.setBounds(envRow.removeFromLeft(envCellWidth));
    envRow.removeFromLeft(kInspectorGap);
    filterSustainDial_.setBounds(envRow.removeFromLeft(envCellWidth));
    envRow.removeFromLeft(kInspectorGap);
    filterReleaseDial_.setBounds(envRow);

    filterModInner.removeFromTop(6);
    auto modRow = filterModInner.removeFromTop(54);
    const int modCellWidth = (modRow.getWidth() - kInspectorGap * 3) / 4;
    filterKeytrackDial_.setBounds(modRow.removeFromLeft(modCellWidth));
    modRow.removeFromLeft(kInspectorGap);
    filterVelDial_.setBounds(modRow.removeFromLeft(modCellWidth));
    modRow.removeFromLeft(kInspectorGap);
    filterLfoRateDial_.setBounds(modRow.removeFromLeft(modCellWidth));
    modRow.removeFromLeft(kInspectorGap);
    filterLfoAmtDial_.setBounds(modRow);

    filterModInner.removeFromTop(6);
    auto comboRow = filterModInner.removeFromTop(40);
    auto shapeArea = comboRow.removeFromLeft((comboRow.getWidth() - 8) / 2);
    filterLfoShapeLabel_.setBounds(shapeArea.removeFromTop(11));
    shapeArea.removeFromTop(2);
    filterLfoShapeCombo_.setBounds(shapeArea.removeFromTop(24));
    comboRow.removeFromLeft(8);
    filterLfoDivisionLabel_.setBounds(comboRow.removeFromTop(11));
    comboRow.removeFromTop(2);
    filterLfoDivisionCombo_.setBounds(comboRow.removeFromTop(24));

    filterModInner.removeFromTop(6);
    auto toggleRow = filterModInner.removeFromTop(24);
    const int toggleWidth = (toggleRow.getWidth() - 8) / 2;
    filterLfoRetriggerToggle_.setBounds(toggleRow.removeFromLeft(toggleWidth));
    toggleRow.removeFromLeft(8);
    filterLfoTempoSyncToggle_.setBounds(toggleRow);
}

void PluginSampleInspectorCardLayout::clearOutput() const
{
    fadeInDial_.setBounds({});
    fadeOutDial_.setBounds({});
    preloadDial_.setBounds({});
    masterVolumeDial_.setBounds({});
    panDial_.setBounds({});
    outputLevelMeter_.setBounds({});
    qualityLabel_.setBounds({});
    qualityCpuButton_.setBounds({});
    qualityFidelityButton_.setBounds({});
    qualityUltraButton_.setBounds({});
}

void PluginSampleInspectorCardLayout::clearEffects() const
{
    reverbMixDial_.setBounds({});
    delayTimeDial_.setBounds({});
    delayFeedbackDial_.setBounds({});
    delayMixDial_.setBounds({});
    delayTempoSyncToggle_.setBounds({});
    dcFilterEnabledToggle_.setBounds({});
    dcFilterCutoffDial_.setBounds({});
    autopanRateDial_.setBounds({});
    autopanDepthDial_.setBounds({});
    saturationDriveDial_.setBounds({});
    saturationModeCombo_.setBounds({});
}

void PluginSampleInspectorCardLayout::clearFilterMod() const
{
    filterAttackDial_.setBounds({});
    filterDecayDial_.setBounds({});
    filterSustainDial_.setBounds({});
    filterReleaseDial_.setBounds({});
    filterEnvelopeGraph_.setBounds({});
    filterKeytrackDial_.setBounds({});
    filterVelDial_.setBounds({});
    filterLfoRateDial_.setBounds({});
    filterLfoAmtDial_.setBounds({});
    filterLfoShapeLabel_.setBounds({});
    filterLfoShapeCombo_.setBounds({});
    filterLfoRetriggerToggle_.setBounds({});
    filterLfoTempoSyncToggle_.setBounds({});
    filterLfoDivisionLabel_.setBounds({});
    filterLfoDivisionCombo_.setBounds({});
}
