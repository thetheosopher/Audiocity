#include "SampleBrowserTooltip.h"

namespace audiocity::plugin
{
juce::String buildSampleBrowserActionText(const bool previewSupported,
                                         const bool mappingDragSupported,
                                         const bool previewing)
{
    if (previewing)
        return mappingDragSupported ? juce::String("Previewing  |  Drag to map")
                                    : juce::String("Previewing  |  Dbl-click load");

    if (previewSupported)
        return mappingDragSupported ? juce::String("Click preview  |  Drag to map")
                                    : juce::String("Click preview  |  Dbl-click load");

    return mappingDragSupported ? juce::String("Drag to map  |  Dbl-click load")
                                : juce::String("Dbl-click load");
}

juce::String buildSampleBrowserTooltipText(const SampleBrowserTooltipData& data)
{
    juce::StringArray lines;

    if (data.fileName.isNotEmpty())
        lines.add(data.fileName);

    if (data.relativePath.isNotEmpty() && !data.relativePath.equalsIgnoreCase(data.fileName))
        lines.add("Path: " + data.relativePath);

    if (data.metadataLine.isNotEmpty())
        lines.add(data.metadataLine);

    if (data.loopFormatBadge.isNotEmpty() || data.loopMetadataLine.isNotEmpty())
    {
        juce::String loopLine;
        if (data.loopFormatBadge.isNotEmpty())
            loopLine = "Format: " + data.loopFormatBadge;
        if (data.loopMetadataLine.isNotEmpty())
            loopLine = loopLine.isNotEmpty() ? loopLine + "  |  " + data.loopMetadataLine
                                             : data.loopMetadataLine;
        if (loopLine.isNotEmpty())
            lines.add(loopLine);
    }

    if (!data.tags.isEmpty())
        lines.add("Tags: " + data.tags.joinIntoString(", "));

    juce::StringArray statusParts;
    if (data.isFavorite)
        statusParts.add("Favorite");
    if (data.isRecent)
        statusParts.add("Recent");
    if (!statusParts.isEmpty())
        lines.add("Status: " + statusParts.joinIntoString(", "));

    return lines.joinIntoString("\n");
}
}