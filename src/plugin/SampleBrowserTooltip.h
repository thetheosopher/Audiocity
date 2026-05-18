#pragma once

#include <juce_core/juce_core.h>

namespace audiocity::plugin
{
struct SampleBrowserTooltipData
{
    juce::String fileName;
    juce::String relativePath;
    juce::String metadataLine;
    juce::String loopFormatBadge;
    juce::String loopMetadataLine;
    juce::StringArray tags;
    bool isFavorite = false;
    bool isRecent = false;
    bool previewSupported = false;
    bool mappingDragSupported = false;
    bool previewing = false;
};

juce::String buildSampleBrowserActionText(bool previewSupported,
                                         bool mappingDragSupported,
                                         bool previewing);

juce::String buildSampleBrowserTooltipText(const SampleBrowserTooltipData& data);
}