#include "PluginSampleInfoMetricsLayout.h"

#include <vector>

PluginSampleInfoMetricsLayout::PluginSampleInfoMetricsLayout(juce::Label& sampleInfoRateLabel,
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
                                                             juce::Label& sampleInfoMetaRootValue)
    : sampleInfoRateLabel_(sampleInfoRateLabel),
      sampleInfoRateValue_(sampleInfoRateValue),
      sampleInfoBitDepthLabel_(sampleInfoBitDepthLabel),
      sampleInfoBitDepthValue_(sampleInfoBitDepthValue),
      sampleInfoChannelsLabel_(sampleInfoChannelsLabel),
      sampleInfoChannelsValue_(sampleInfoChannelsValue),
      sampleInfoSamplesLabel_(sampleInfoSamplesLabel),
      sampleInfoSamplesValue_(sampleInfoSamplesValue),
      sampleInfoDurationLabel_(sampleInfoDurationLabel),
      sampleInfoDurationValue_(sampleInfoDurationValue),
      sampleInfoFileSizeLabel_(sampleInfoFileSizeLabel),
      sampleInfoFileSizeValue_(sampleInfoFileSizeValue),
      sampleInfoPlaybackLabel_(sampleInfoPlaybackLabel),
      sampleInfoPlaybackValue_(sampleInfoPlaybackValue),
      sampleInfoPlaybackDurationLabel_(sampleInfoPlaybackDurationLabel),
      sampleInfoPlaybackDurationValue_(sampleInfoPlaybackDurationValue),
      sampleInfoLoopLabel_(sampleInfoLoopLabel),
      sampleInfoLoopValue_(sampleInfoLoopValue),
      sampleInfoLoopDurationLabel_(sampleInfoLoopDurationLabel),
      sampleInfoLoopDurationValue_(sampleInfoLoopDurationValue),
      sampleInfoTempoLabel_(sampleInfoTempoLabel),
      sampleInfoTempoValue_(sampleInfoTempoValue),
      sampleInfoMetaRootLabel_(sampleInfoMetaRootLabel),
      sampleInfoMetaRootValue_(sampleInfoMetaRootValue)
{
}

int PluginSampleInfoMetricsLayout::countVisibleMetrics() const
{
    int count = 0;
    const auto countMetric = [&count](const juce::Label& keyLabel, const juce::Label& valueLabel)
    {
        if (keyLabel.isVisible() && valueLabel.isVisible())
            ++count;
    };

    countMetric(sampleInfoRateLabel_, sampleInfoRateValue_);
    countMetric(sampleInfoBitDepthLabel_, sampleInfoBitDepthValue_);
    countMetric(sampleInfoChannelsLabel_, sampleInfoChannelsValue_);
    countMetric(sampleInfoSamplesLabel_, sampleInfoSamplesValue_);
    countMetric(sampleInfoDurationLabel_, sampleInfoDurationValue_);
    countMetric(sampleInfoFileSizeLabel_, sampleInfoFileSizeValue_);
    countMetric(sampleInfoPlaybackLabel_, sampleInfoPlaybackValue_);
    countMetric(sampleInfoPlaybackDurationLabel_, sampleInfoPlaybackDurationValue_);
    countMetric(sampleInfoLoopLabel_, sampleInfoLoopValue_);
    countMetric(sampleInfoLoopDurationLabel_, sampleInfoLoopDurationValue_);
    countMetric(sampleInfoTempoLabel_, sampleInfoTempoValue_);
    countMetric(sampleInfoMetaRootLabel_, sampleInfoMetaRootValue_);

    return count;
}

void PluginSampleInfoMetricsLayout::layoutInlinePair(juce::Rectangle<int>& row,
                                                     juce::Label& keyLabel,
                                                     juce::Label& valueLabel,
                                                     const int keyWidth,
                                                     const int valueWidth)
{
    keyLabel.setBounds(row.removeFromLeft(keyWidth));
    valueLabel.setBounds(row.removeFromLeft(valueWidth));
    row.removeFromLeft(10);
}

void PluginSampleInfoMetricsLayout::layoutInline(juce::Rectangle<int>& infoInner) const
{
    auto row2 = infoInner.removeFromTop(22);
    layoutInlinePair(row2, sampleInfoRateLabel_, sampleInfoRateValue_, 72, 98);
    layoutInlinePair(row2, sampleInfoBitDepthLabel_, sampleInfoBitDepthValue_, 60, 68);
    layoutInlinePair(row2, sampleInfoChannelsLabel_, sampleInfoChannelsValue_, 56, 30);
    layoutInlinePair(row2, sampleInfoSamplesLabel_, sampleInfoSamplesValue_, 66, 86);
    layoutInlinePair(row2, sampleInfoDurationLabel_, sampleInfoDurationValue_, 56, 72);
    layoutInlinePair(row2, sampleInfoFileSizeLabel_, sampleInfoFileSizeValue_, 48, 86);

    infoInner.removeFromTop(2);
    auto row3 = infoInner.removeFromTop(22);

    const auto layoutInlinePairIfVisible = [&row3](juce::Label& keyLabel,
                                                   juce::Label& valueLabel,
                                                   const int keyWidth,
                                                   const int valueWidth)
    {
        if (keyLabel.isVisible() && valueLabel.isVisible())
            layoutInlinePair(row3, keyLabel, valueLabel, keyWidth, valueWidth);
        else
        {
            keyLabel.setBounds({});
            valueLabel.setBounds({});
        }
    };

    layoutInlinePairIfVisible(sampleInfoPlaybackLabel_, sampleInfoPlaybackValue_, 120, 150);
    layoutInlinePairIfVisible(sampleInfoPlaybackDurationLabel_, sampleInfoPlaybackDurationValue_, 124, 90);
    layoutInlinePairIfVisible(sampleInfoLoopLabel_, sampleInfoLoopValue_, 74, 150);
    layoutInlinePairIfVisible(sampleInfoLoopDurationLabel_, sampleInfoLoopDurationValue_, 94, 90);
    if (sampleInfoTempoLabel_.isVisible())
        layoutInlinePair(row3, sampleInfoTempoLabel_, sampleInfoTempoValue_, 52, 72);
    if (sampleInfoMetaRootLabel_.isVisible())
        layoutInlinePair(row3, sampleInfoMetaRootLabel_, sampleInfoMetaRootValue_, 68, 110);
}

void PluginSampleInfoMetricsLayout::layoutInspector(juce::Rectangle<int>& infoInner) const
{
    const auto layoutMetricCell = [](juce::Rectangle<int> cell,
                                     juce::Label& keyLabel,
                                     juce::Label& valueLabel)
    {
        keyLabel.setBounds(cell.removeFromTop(11));
        cell.removeFromTop(2);
        valueLabel.setBounds(cell.removeFromTop(16));
    };

    std::vector<std::pair<juce::Label*, juce::Label*>> metrics;
    metrics.reserve(12);
    const auto addMetric = [&metrics](juce::Label& keyLabel, juce::Label& valueLabel)
    {
        if (keyLabel.isVisible() && valueLabel.isVisible())
            metrics.push_back({ &keyLabel, &valueLabel });
        else
        {
            keyLabel.setBounds({});
            valueLabel.setBounds({});
        }
    };

    addMetric(sampleInfoRateLabel_, sampleInfoRateValue_);
    addMetric(sampleInfoBitDepthLabel_, sampleInfoBitDepthValue_);
    addMetric(sampleInfoChannelsLabel_, sampleInfoChannelsValue_);
    addMetric(sampleInfoSamplesLabel_, sampleInfoSamplesValue_);
    addMetric(sampleInfoDurationLabel_, sampleInfoDurationValue_);
    addMetric(sampleInfoFileSizeLabel_, sampleInfoFileSizeValue_);
    addMetric(sampleInfoPlaybackLabel_, sampleInfoPlaybackValue_);
    addMetric(sampleInfoPlaybackDurationLabel_, sampleInfoPlaybackDurationValue_);
    addMetric(sampleInfoLoopLabel_, sampleInfoLoopValue_);
    addMetric(sampleInfoLoopDurationLabel_, sampleInfoLoopDurationValue_);
    addMetric(sampleInfoTempoLabel_, sampleInfoTempoValue_);
    addMetric(sampleInfoMetaRootLabel_, sampleInfoMetaRootValue_);

    constexpr int kMetricGap = 8;
    const int columnWidth = (infoInner.getWidth() - kMetricGap) / 2;
    for (std::size_t index = 0; index < metrics.size(); index += 2)
    {
        auto row = infoInner.removeFromTop(30);
        auto leftCell = row.removeFromLeft(columnWidth);
        layoutMetricCell(leftCell, *metrics[index].first, *metrics[index].second);

        if (index + 1 < metrics.size())
        {
            row.removeFromLeft(kMetricGap);
            layoutMetricCell(row, *metrics[index + 1].first, *metrics[index + 1].second);
        }

        infoInner.removeFromTop(4);
    }
}
