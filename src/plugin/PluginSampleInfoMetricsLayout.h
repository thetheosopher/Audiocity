#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class PluginSampleInfoMetricsLayout final
{
public:
    PluginSampleInfoMetricsLayout(juce::Label& sampleInfoRateLabel,
                                  juce::Label& sampleInfoRateValue,
                                  juce::Label& sampleInfoBitDepthLabel,
                                  juce::Label& sampleInfoBitDepthValue,
                                  juce::Label& sampleInfoChannelsLabel,
                                  juce::Label& sampleInfoChannelsValue,
                                  juce::Label& sampleInfoSamplesLabel,
                                  juce::Label& sampleInfoSamplesValue,
                                  juce::Label& sampleInfoDurationLabel,
                                  juce::Label& sampleInfoDurationValue,
                                  juce::Label& sampleInfoFileSizeLabel,
                                  juce::Label& sampleInfoFileSizeValue,
                                  juce::Label& sampleInfoPlaybackLabel,
                                  juce::Label& sampleInfoPlaybackValue,
                                  juce::Label& sampleInfoPlaybackDurationLabel,
                                  juce::Label& sampleInfoPlaybackDurationValue,
                                  juce::Label& sampleInfoLoopLabel,
                                  juce::Label& sampleInfoLoopValue,
                                  juce::Label& sampleInfoLoopDurationLabel,
                                  juce::Label& sampleInfoLoopDurationValue,
                                  juce::Label& sampleInfoTempoLabel,
                                  juce::Label& sampleInfoTempoValue,
                                  juce::Label& sampleInfoMetaRootLabel,
                                  juce::Label& sampleInfoMetaRootValue);

    int countVisibleMetrics() const;
    void layoutInline(juce::Rectangle<int>& infoInner) const;
    void layoutInspector(juce::Rectangle<int>& infoInner) const;

private:
    static void layoutInlinePair(juce::Rectangle<int>& row,
                                 juce::Label& keyLabel,
                                 juce::Label& valueLabel,
                                 int keyWidth,
                                 int valueWidth);

    juce::Label& sampleInfoRateLabel_;
    juce::Label& sampleInfoRateValue_;
    juce::Label& sampleInfoBitDepthLabel_;
    juce::Label& sampleInfoBitDepthValue_;
    juce::Label& sampleInfoChannelsLabel_;
    juce::Label& sampleInfoChannelsValue_;
    juce::Label& sampleInfoSamplesLabel_;
    juce::Label& sampleInfoSamplesValue_;
    juce::Label& sampleInfoDurationLabel_;
    juce::Label& sampleInfoDurationValue_;
    juce::Label& sampleInfoFileSizeLabel_;
    juce::Label& sampleInfoFileSizeValue_;
    juce::Label& sampleInfoPlaybackLabel_;
    juce::Label& sampleInfoPlaybackValue_;
    juce::Label& sampleInfoPlaybackDurationLabel_;
    juce::Label& sampleInfoPlaybackDurationValue_;
    juce::Label& sampleInfoLoopLabel_;
    juce::Label& sampleInfoLoopValue_;
    juce::Label& sampleInfoLoopDurationLabel_;
    juce::Label& sampleInfoLoopDurationValue_;
    juce::Label& sampleInfoTempoLabel_;
    juce::Label& sampleInfoTempoValue_;
    juce::Label& sampleInfoMetaRootLabel_;
    juce::Label& sampleInfoMetaRootValue_;
};
