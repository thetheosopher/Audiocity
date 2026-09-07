#pragma once

#include "../engine/ProgramModel.h"

#include <juce_data_structures/juce_data_structures.h>

#include <cstdint>
#include <vector>

namespace audiocity::plugin
{
/** A bounded, portable description of one file needed to restore an imported program. */
struct ImportedAssetManifestEntry
{
    enum class Role
    {
        program,
        sample
    };

    Role role = Role::sample;
    int sampleAssetIndex = -1;
    juce::String relativePath;
    juce::String fileName;
    juce::int64 sizeBytes = -1;
    juce::int64 modificationTimeMs = 0;
    std::uint64_t fastHash = 0;
    bool hasFastHash = false;
};

struct ImportedAssetManifest
{
    static constexpr int currentVersion = 1;
    static constexpr std::size_t maximumEntries = 4097;

    int version = currentVersion;
    juce::String originalRoot;
    std::vector<ImportedAssetManifestEntry> entries;

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] int programEntryIndex() const noexcept;
};

struct ImportedAssetSearchLimits
{
    int maximumFiles = 50000;
    int maximumDirectories = 4096;
    int maximumDepth = 12;
};

struct ImportedAssetResolution
{
    bool complete = false;
    bool ambiguous = false;
    bool limitReached = false;
    int scannedFiles = 0;
    int scannedDirectories = 0;
    int resolvedProgramEntryIndex = -1;
    juce::File resolvedRoot;
    std::vector<juce::File> resolvedFiles;
    juce::String diagnostic;

    [[nodiscard]] juce::File resolvedProgramFile() const;
};

/** Builds a manifest without reading more than two small chunks from any file. Embedded
    container assets are omitted because they move with the program file. */
[[nodiscard]] ImportedAssetManifest createImportedAssetManifest(
    const juce::File& programFile,
    const audiocity::engine::Program& program);

[[nodiscard]] juce::ValueTree createImportedAssetManifestState(const ImportedAssetManifest& manifest);
[[nodiscard]] ImportedAssetManifest readImportedAssetManifestState(const juce::ValueTree& parentState);

/** Resolves every entry against one common relocated root. Search is constrained to the
    supplied roots and the explicit limits. The result is all-or-nothing; duplicate complete
    roots are reported as ambiguous and no candidate is selected. */
[[nodiscard]] ImportedAssetResolution resolveImportedAssetManifest(
    const ImportedAssetManifest& manifest,
    const std::vector<juce::File>& searchRoots,
    ImportedAssetSearchLimits limits = {});

/** Legacy states have only the imported program path. This keeps those states repairable
    without pretending that their unrecorded sample dependencies were verified. */
[[nodiscard]] ImportedAssetManifest createLegacyImportedProgramManifest(const juce::File& programFile);
}
