#include "LibraryFileIndex.h"

namespace audiocity::plugin
{
bool LibraryFileIndex::isSupportedExtension(const juce::String& extensionLower, const bool includeRex)
{
    const auto extension = extensionLower.toLowerCase();
    if (extension == ".wav" || extension == ".aif" || extension == ".aiff"
        || extension == ".ncw"
        || extension == ".sfz" || extension == ".nki"
        || extension == ".sf2" || extension == ".dspreset" || extension == ".multisample"
        || extension == ".xpm" || extension == ".talsmpl" || extension == ".txprog"
        || extension == ".korgmultisample"
        || extension == ".adv" || extension == ".adg" || extension == ".dexpreset"
        || extension == ".kmp" || extension == ".exs" || extension == ".sxt")
        return true;

    return includeRex && (extension == ".rex" || extension == ".rx2");
}

bool LibraryFileIndex::isSupportedFile(const juce::File& file, const bool includeRex)
{
    return file.existsAsFile() && isSupportedExtension(file.getFileExtension(), includeRex);
}

std::optional<LibraryFileIndexEntry> LibraryFileIndex::createEntryForFile(
    const juce::File& rootFolder,
    const juce::File& file,
    const bool includeRex)
{
    if (!isSupportedFile(file, includeRex))
        return std::nullopt;

    LibraryFileIndexEntry entry;
    entry.file = file;
    entry.relativePath = file.getRelativePathFrom(rootFolder).replaceCharacter('\\', '/');
    entry.fileName = file.getFileName();
    entry.extensionLower = file.getFileExtension().toLowerCase();
    entry.sizeBytes = file.getSize();
    entry.isInstrument = entry.extensionLower == ".sfz" || entry.extensionLower == ".nki"
        || entry.extensionLower == ".sf2" || entry.extensionLower == ".dspreset"
        || entry.extensionLower == ".multisample"
        || entry.extensionLower == ".xpm" || entry.extensionLower == ".talsmpl"
        || entry.extensionLower == ".txprog" || entry.extensionLower == ".korgmultisample"
        || entry.extensionLower == ".adv" || entry.extensionLower == ".adg"
        || entry.extensionLower == ".dexpreset" || entry.extensionLower == ".kmp"
        || entry.extensionLower == ".exs" || entry.extensionLower == ".sxt";
    return entry;
}

std::vector<LibraryFileIndexEntry> LibraryFileIndex::scanRoot(const juce::File& rootFolder, const bool includeRex)
{
    std::vector<LibraryFileIndexEntry> entries;
    if (!rootFolder.isDirectory())
        return entries;

    for (const auto& item : juce::RangedDirectoryIterator(rootFolder, true, "*", juce::File::findFiles))
    {
        if (auto entry = createEntryForFile(rootFolder, item.getFile(), includeRex); entry.has_value())
            entries.push_back(std::move(*entry));
    }

    return entries;
}
} // namespace audiocity::plugin
