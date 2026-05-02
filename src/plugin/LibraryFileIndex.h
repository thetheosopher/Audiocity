#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace audiocity::plugin
{
struct LibraryFileIndexEntry
{
    juce::File file;
    juce::String relativePath;
    juce::String fileName;
    juce::String extensionLower;
    juce::int64 sizeBytes = 0;
    bool isInstrument = false;
};

class LibraryFileIndex
{
public:
    [[nodiscard]] static bool isSupportedExtension(const juce::String& extensionLower, bool includeRex);
    [[nodiscard]] static bool isSupportedFile(const juce::File& file, bool includeRex);
    [[nodiscard]] static std::optional<LibraryFileIndexEntry> createEntryForFile(
        const juce::File& rootFolder,
        const juce::File& file,
        bool includeRex);
    [[nodiscard]] static std::vector<LibraryFileIndexEntry> scanRoot(const juce::File& rootFolder, bool includeRex);
};
} // namespace audiocity::plugin
