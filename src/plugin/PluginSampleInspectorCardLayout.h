#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class PluginSampleInspectorCardLayout final
{
public:
    PluginSampleInspectorCardLayout(juce::Component& fadeInDial,
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
                                    juce::ComboBox& filterLfoDivisionCombo);

    void layoutOutput(juce::Rectangle<int> inspectorBounds) const;
    void layoutEffects(juce::Rectangle<int> inspectorBounds) const;
    void layoutFilterMod(juce::Rectangle<int> inspectorBounds) const;
    void clearOutput() const;
    void clearEffects() const;
    void clearFilterMod() const;

private:
    juce::Component& fadeInDial_;
    juce::Component& fadeOutDial_;
    juce::Component& preloadDial_;
    juce::Component& masterVolumeDial_;
    juce::Component& panDial_;
    juce::Component& outputLevelMeter_;
    juce::Component& qualityLabel_;
    juce::Button& qualityCpuButton_;
    juce::Button& qualityFidelityButton_;
    juce::Button& qualityUltraButton_;
    juce::Component& reverbMixDial_;
    juce::Component& delayTimeDial_;
    juce::Component& delayFeedbackDial_;
    juce::Component& delayMixDial_;
    juce::Button& delayTempoSyncToggle_;
    juce::Button& dcFilterEnabledToggle_;
    juce::Component& dcFilterCutoffDial_;
    juce::Component& autopanRateDial_;
    juce::Component& autopanDepthDial_;
    juce::Component& saturationDriveDial_;
    juce::ComboBox& saturationModeCombo_;
    juce::Component& filterAttackDial_;
    juce::Component& filterDecayDial_;
    juce::Component& filterSustainDial_;
    juce::Component& filterReleaseDial_;
    juce::Component& filterEnvelopeGraph_;
    juce::Component& filterKeytrackDial_;
    juce::Component& filterVelDial_;
    juce::Component& filterLfoRateDial_;
    juce::Component& filterLfoAmtDial_;
    juce::Component& filterLfoShapeLabel_;
    juce::ComboBox& filterLfoShapeCombo_;
    juce::Button& filterLfoRetriggerToggle_;
    juce::Button& filterLfoTempoSyncToggle_;
    juce::Component& filterLfoDivisionLabel_;
    juce::ComboBox& filterLfoDivisionCombo_;
};
