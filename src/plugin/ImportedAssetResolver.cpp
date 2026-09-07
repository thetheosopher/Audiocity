#include "ImportedAssetResolver.h"

#include <algorithm>
#include <array>
#include <set>
#include <utility>

namespace audiocity::plugin
{
namespace
{
constexpr auto kManifestType = "ImportedAssetManifest";
constexpr auto kManifestEntryType = "ImportedAsset";
constexpr auto kVersionProperty = "version";
constexpr auto kOriginalRootProperty = "originalRoot";
constexpr auto kRoleProperty = "role";
constexpr auto kSampleAssetIndexProperty = "sampleAssetIndex";
constexpr auto kRelativePathProperty = "relativePath";
constexpr auto kFileNameProperty = "fileName";
constexpr auto kSizeProperty = "sizeBytes";
constexpr auto kModificationTimeProperty = "modificationTimeMs";
constexpr auto kFastHashProperty = "fastHash";
constexpr auto kProgramRole = "program";
constexpr auto kSampleRole = "sample";
constexpr std::size_t kHashChunkBytes = 4096;

juce::String pathKey(const juce::File& file)
{
    auto key = file.getFullPathName().replaceCharacter('\\', '/');
#if JUCE_WINDOWS
    key = key.toLowerCase();
#endif
    return key;
}

bool isSameOrChild(const juce::File& file, const juce::File& parent)
{
    return file == parent || file.isAChildOf(parent);
}

bool isFileSystemRoot(const juce::File& directory)
{
    return directory == juce::File{} || directory.getParentDirectory() == directory;
}

bool isSafeRelativePath(const juce::String& path)
{
    if (path.isEmpty() || path.length() > 4096 || juce::File::isAbsolutePath(path))
        return false;

    juce::StringArray components;
    components.addTokens(path.replaceCharacter('\\', '/'), "/", {});
    components.removeEmptyStrings();
    if (components.isEmpty())
        return false;

    for (const auto& component : components)
        if (component == "." || component == "..")
            return false;

    return true;
}

bool pathTraversesSymbolicLink(const juce::File& root, const juce::String& relativePath)
{
    juce::StringArray components;
    components.addTokens(relativePath.replaceCharacter('\\', '/'), "/", {});
    components.removeEmptyStrings();

    auto candidate = root;
    for (const auto& component : components)
    {
        candidate = candidate.getChildFile(component);
        if (candidate.isSymbolicLink())
            return true;
    }

    return false;
}

struct FastHashResult
{
    std::uint64_t value = 0;
    bool ok = false;
};

void mixHashBytes(std::uint64_t& hash, const void* data, const std::size_t bytes)
{
    const auto* source = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < bytes; ++index)
    {
        hash ^= source[index];
        hash *= 1099511628211ull;
    }
}

FastHashResult calculateFastHash(const juce::File& file)
{
    if (!file.existsAsFile())
        return {};

    juce::FileInputStream input(file);
    if (!input.openedOk())
        return {};

    auto hash = std::uint64_t{ 1469598103934665603ull };
    const auto fileSize = juce::jmax<juce::int64>(0, file.getSize());
    mixHashBytes(hash, &fileSize, sizeof(fileSize));

    std::array<std::uint8_t, kHashChunkBytes> buffer{};
    const auto firstBytes = static_cast<int>(juce::jmin<juce::int64>(fileSize,
        static_cast<juce::int64>(buffer.size())));
    if (firstBytes > 0)
    {
        const auto bytesRead = input.read(buffer.data(), firstBytes);
        if (bytesRead != firstBytes)
            return {};
        mixHashBytes(hash, buffer.data(), static_cast<std::size_t>(bytesRead));
    }

    if (fileSize > static_cast<juce::int64>(buffer.size()))
    {
        const auto tailPosition = juce::jmax<juce::int64>(0,
            fileSize - static_cast<juce::int64>(buffer.size()));
        if (!input.setPosition(tailPosition))
            return {};
        const auto bytesRead = input.read(buffer.data(), static_cast<int>(buffer.size()));
        if (bytesRead <= 0)
            return {};
        mixHashBytes(hash, buffer.data(), static_cast<std::size_t>(bytesRead));
    }

    return { hash, true };
}

ImportedAssetManifestEntry createEntry(const ImportedAssetManifestEntry::Role role,
                                       const int sampleAssetIndex,
                                       const juce::File& file,
                                       const juce::File& root)
{
    ImportedAssetManifestEntry entry;
    entry.role = role;
    entry.sampleAssetIndex = sampleAssetIndex;
    entry.fileName = file.getFileName();
    entry.relativePath = isSameOrChild(file, root) ? file.getRelativePathFrom(root) : entry.fileName;
    entry.sizeBytes = file.existsAsFile() ? file.getSize() : -1;
    entry.modificationTimeMs = file.existsAsFile() ? file.getLastModificationTime().toMilliseconds() : 0;
    const auto hash = calculateFastHash(file);
    entry.fastHash = hash.value;
    entry.hasFastHash = hash.ok;
    return entry;
}

bool fileMatchesEntry(const juce::File& file, const ImportedAssetManifestEntry& entry)
{
    if (!file.existsAsFile() || !file.getFileName().equalsIgnoreCase(entry.fileName))
        return false;
    if (entry.sizeBytes >= 0 && file.getSize() != entry.sizeBytes)
        return false;
    if (entry.hasFastHash)
    {
        const auto candidateHash = calculateFastHash(file);
        if (!candidateHash.ok || candidateHash.value != entry.fastHash)
            return false;
    }
    else if (entry.sizeBytes >= 0
             && (entry.modificationTimeMs <= 0
                 || file.getLastModificationTime().toMilliseconds() != entry.modificationTimeMs))
    {
        return false;
    }
    return true;
}

int relativeParentDepth(const juce::String& relativePath)
{
    juce::StringArray components;
    components.addTokens(relativePath.replaceCharacter('\\', '/'), "/", {});
    components.removeEmptyStrings();
    return juce::jmax(0, components.size() - 1);
}

juce::File deriveRootFromProgramCandidate(const juce::File& programFile,
                                          const juce::String& programRelativePath)
{
    auto root = programFile.getParentDirectory();
    for (auto level = 0; level < relativeParentDepth(programRelativePath); ++level)
        root = root.getParentDirectory();
    return root;
}

bool resolveAtRoot(const ImportedAssetManifest& manifest,
                   const juce::File& root,
                   std::vector<juce::File>& files)
{
    files.clear();
    files.reserve(manifest.entries.size());
    for (const auto& entry : manifest.entries)
    {
        const auto candidate = root.getChildFile(entry.relativePath);
        if (!isSameOrChild(candidate, root)
            || pathTraversesSymbolicLink(root, entry.relativePath)
            || !fileMatchesEntry(candidate, entry))
        {
            files.clear();
            return false;
        }
        files.push_back(candidate);
    }
    return true;
}

void addUniqueRoot(std::vector<juce::File>& roots,
                   std::set<juce::String>& keys,
                   const juce::File& root)
{
    if (!root.isDirectory() || isFileSystemRoot(root))
        return;
    const auto key = pathKey(root);
    if (keys.insert(key).second)
        roots.push_back(root);
}

struct SearchCounters
{
    int files = 0;
    int directories = 0;
    bool limitReached = false;
};

void findProgramCandidates(const juce::File& searchRoot,
                           const ImportedAssetManifestEntry& programEntry,
                           const ImportedAssetSearchLimits& limits,
                           SearchCounters& counters,
                           std::vector<juce::File>& candidates)
{
    struct PendingDirectory
    {
        juce::File directory;
        int depth = 0;
    };

    std::vector<PendingDirectory> pending{ { searchRoot, 0 } };
    std::set<juce::String> visited;
    while (!pending.empty())
    {
        auto current = std::move(pending.back());
        pending.pop_back();
        if (!visited.insert(pathKey(current.directory)).second)
            continue;
        if (++counters.directories > limits.maximumDirectories)
        {
            counters.limitReached = true;
            return;
        }

        juce::Array<juce::File> children;
        current.directory.findChildFiles(children, juce::File::findFilesAndDirectories, false);
        std::vector<juce::File> ordered(children.begin(), children.end());
        std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right)
        {
            return pathKey(left) < pathKey(right);
        });

        for (const auto& child : ordered)
        {
            if (child.isSymbolicLink())
                continue;

            if (child.isDirectory())
            {
                if (current.depth < limits.maximumDepth)
                    pending.push_back({ child, current.depth + 1 });
                continue;
            }

            if (++counters.files > limits.maximumFiles)
            {
                counters.limitReached = true;
                return;
            }
            if (child.getFileName().equalsIgnoreCase(programEntry.fileName)
                && fileMatchesEntry(child, programEntry))
            {
                candidates.push_back(child);
            }
        }
    }
}
}

bool ImportedAssetManifest::isValid() const noexcept
{
    if (version != currentVersion || entries.empty() || entries.size() > maximumEntries)
        return false;

    auto programEntries = 0;
    for (const auto& entry : entries)
    {
        if (!isSafeRelativePath(entry.relativePath)
            || entry.fileName.isEmpty()
            || entry.fileName.length() > 1024
            || entry.sizeBytes < -1
            || (!entry.hasFastHash && entry.sizeBytes >= 0 && entry.modificationTimeMs <= 0))
            return false;
        if (entry.role == ImportedAssetManifestEntry::Role::program)
            ++programEntries;
    }
    return programEntries == 1;
}

int ImportedAssetManifest::programEntryIndex() const noexcept
{
    for (std::size_t index = 0; index < entries.size(); ++index)
        if (entries[index].role == ImportedAssetManifestEntry::Role::program)
            return static_cast<int>(index);
    return -1;
}

juce::File ImportedAssetResolution::resolvedProgramFile() const
{
    return complete
        && resolvedProgramEntryIndex >= 0
        && static_cast<std::size_t>(resolvedProgramEntryIndex) < resolvedFiles.size()
        ? resolvedFiles[static_cast<std::size_t>(resolvedProgramEntryIndex)]
        : juce::File{};
}

ImportedAssetManifest createImportedAssetManifest(const juce::File& programFile,
                                                  const audiocity::engine::Program& program)
{
    if (programFile == juce::File{} || programFile.getFileName().isEmpty())
        return {};

    std::vector<std::pair<int, juce::File>> sampleFiles;
    sampleFiles.reserve(program.sampleAssets.size());
    for (std::size_t index = 0; index < program.sampleAssets.size(); ++index)
    {
        const auto& asset = program.sampleAssets[index];
        if (asset.embeddedInProgram || asset.sourcePath.empty())
            continue;
        const juce::File file(juce::String::fromUTF8(asset.sourcePath.c_str()));
        if (file == programFile)
            continue;
        sampleFiles.emplace_back(static_cast<int>(index), file);
    }

    auto root = programFile.getParentDirectory();
    for (const auto& [assetIndex, file] : sampleFiles)
    {
        juce::ignoreUnused(assetIndex);
        auto candidate = root;
        while (!isSameOrChild(file, candidate) && !isFileSystemRoot(candidate))
            candidate = candidate.getParentDirectory();
        if (isSameOrChild(file, candidate) && !isFileSystemRoot(candidate))
            root = candidate;
    }

    ImportedAssetManifest manifest;
    manifest.originalRoot = root.getFullPathName();
    manifest.entries.reserve(juce::jmin<std::size_t>(ImportedAssetManifest::maximumEntries,
                                                      sampleFiles.size() + 1));
    manifest.entries.push_back(createEntry(ImportedAssetManifestEntry::Role::program,
                                           -1,
                                           programFile,
                                           root));
    for (const auto& [assetIndex, file] : sampleFiles)
    {
        if (manifest.entries.size() >= ImportedAssetManifest::maximumEntries)
            break;
        manifest.entries.push_back(createEntry(ImportedAssetManifestEntry::Role::sample,
                                               assetIndex,
                                               file,
                                               root));
    }
    return manifest;
}

ImportedAssetManifest createLegacyImportedProgramManifest(const juce::File& programFile)
{
    audiocity::engine::Program emptyProgram;
    return createImportedAssetManifest(programFile, emptyProgram);
}

juce::ValueTree createImportedAssetManifestState(const ImportedAssetManifest& manifest)
{
    if (!manifest.isValid())
        return {};

    juce::ValueTree state(kManifestType);
    state.setProperty(kVersionProperty, manifest.version, nullptr);
    state.setProperty(kOriginalRootProperty, manifest.originalRoot, nullptr);
    for (const auto& entry : manifest.entries)
    {
        juce::ValueTree child(kManifestEntryType);
        child.setProperty(kRoleProperty,
                          entry.role == ImportedAssetManifestEntry::Role::program ? kProgramRole : kSampleRole,
                          nullptr);
        child.setProperty(kSampleAssetIndexProperty, entry.sampleAssetIndex, nullptr);
        child.setProperty(kRelativePathProperty, entry.relativePath, nullptr);
        child.setProperty(kFileNameProperty, entry.fileName, nullptr);
        child.setProperty(kSizeProperty, entry.sizeBytes, nullptr);
        child.setProperty(kModificationTimeProperty, entry.modificationTimeMs, nullptr);
        if (entry.hasFastHash)
            child.setProperty(kFastHashProperty, juce::String::toHexString(static_cast<juce::int64>(entry.fastHash)), nullptr);
        state.appendChild(child, nullptr);
    }
    return state;
}

ImportedAssetManifest readImportedAssetManifestState(const juce::ValueTree& parentState)
{
    const auto state = parentState.hasType(kManifestType)
        ? parentState
        : parentState.getChildWithName(kManifestType);
    ImportedAssetManifest manifest;
    manifest.entries.clear();
    if (!state.isValid())
        return manifest;

    manifest.version = static_cast<int>(state.getProperty(kVersionProperty, 0));
    manifest.originalRoot = state.getProperty(kOriginalRootProperty).toString();
    const auto entryCount = state.getNumChildren();
    if (entryCount <= 0 || static_cast<std::size_t>(entryCount) > ImportedAssetManifest::maximumEntries)
        return {};

    manifest.entries.reserve(static_cast<std::size_t>(entryCount));
    for (auto index = 0; index < entryCount; ++index)
    {
        const auto child = state.getChild(index);
        if (!child.hasType(kManifestEntryType))
            return {};

        ImportedAssetManifestEntry entry;
        const auto role = child.getProperty(kRoleProperty).toString();
        if (role == kProgramRole)
            entry.role = ImportedAssetManifestEntry::Role::program;
        else if (role == kSampleRole)
            entry.role = ImportedAssetManifestEntry::Role::sample;
        else
            return {};
        entry.sampleAssetIndex = static_cast<int>(child.getProperty(kSampleAssetIndexProperty, -1));
        entry.relativePath = child.getProperty(kRelativePathProperty).toString();
        entry.fileName = child.getProperty(kFileNameProperty).toString();
        entry.sizeBytes = static_cast<juce::int64>(child.getProperty(kSizeProperty, static_cast<juce::int64>(-1)));
        entry.modificationTimeMs = static_cast<juce::int64>(child.getProperty(kModificationTimeProperty, static_cast<juce::int64>(0)));
        const auto hash = child.getProperty(kFastHashProperty).toString();
        if (hash.isNotEmpty())
        {
            entry.fastHash = static_cast<std::uint64_t>(hash.getHexValue64());
            entry.hasFastHash = true;
        }
        manifest.entries.push_back(std::move(entry));
    }

    return manifest.isValid() ? manifest : ImportedAssetManifest{};
}

ImportedAssetResolution resolveImportedAssetManifest(const ImportedAssetManifest& manifest,
                                                     const std::vector<juce::File>& searchRoots,
                                                     ImportedAssetSearchLimits limits)
{
    ImportedAssetResolution result;
    if (!manifest.isValid())
    {
        result.diagnostic = "Imported asset manifest is missing or invalid";
        return result;
    }

    limits.maximumFiles = juce::jlimit(1, 250000, limits.maximumFiles);
    limits.maximumDirectories = juce::jlimit(1, 50000, limits.maximumDirectories);
    limits.maximumDepth = juce::jlimit(0, 32, limits.maximumDepth);

    std::vector<juce::File> roots;
    std::set<juce::String> rootKeys;
    addUniqueRoot(roots, rootKeys, juce::File(manifest.originalRoot));
    for (const auto& root : searchRoots)
        addUniqueRoot(roots, rootKeys, root);

    if (roots.empty())
    {
        result.diagnostic = "No bounded library folders are available for asset recovery";
        return result;
    }

    const auto programIndex = manifest.programEntryIndex();
    const auto& programEntry = manifest.entries[static_cast<std::size_t>(programIndex)];
    SearchCounters counters;
    std::vector<juce::File> candidateRoots;
    std::set<juce::String> candidateRootKeys;

    for (const auto& searchRoot : roots)
    {
        const auto directProgram = searchRoot.getChildFile(programEntry.relativePath);
        if (isSameOrChild(directProgram, searchRoot)
            && !pathTraversesSymbolicLink(searchRoot, programEntry.relativePath)
            && fileMatchesEntry(directProgram, programEntry))
            addUniqueRoot(candidateRoots, candidateRootKeys, searchRoot);

        std::vector<juce::File> programCandidates;
        findProgramCandidates(searchRoot, programEntry, limits, counters, programCandidates);
        for (const auto& candidate : programCandidates)
        {
            const auto derivedRoot = deriveRootFromProgramCandidate(candidate, programEntry.relativePath);
            if (isSameOrChild(derivedRoot, searchRoot))
                addUniqueRoot(candidateRoots, candidateRootKeys, derivedRoot);
        }
        if (counters.limitReached)
            break;
    }

    struct CompleteCandidate
    {
        juce::File root;
        std::vector<juce::File> files;
    };
    std::vector<CompleteCandidate> completeCandidates;
    std::set<juce::String> solutionKeys;
    for (const auto& candidateRoot : candidateRoots)
    {
        std::vector<juce::File> files;
        if (!resolveAtRoot(manifest, candidateRoot, files))
            continue;
        juce::String key;
        for (const auto& file : files)
            key += pathKey(file) + "\n";
        if (solutionKeys.insert(key).second)
            completeCandidates.push_back({ candidateRoot, std::move(files) });
    }

    result.scannedFiles = counters.files;
    result.scannedDirectories = counters.directories;
    result.limitReached = counters.limitReached;
    result.resolvedProgramEntryIndex = programIndex;
    if (completeCandidates.size() > 1)
    {
        result.ambiguous = true;
        result.diagnostic = "Found " + juce::String(static_cast<int>(completeCandidates.size()))
            + " complete matching libraries; choose a narrower folder to avoid an ambiguous relink";
        return result;
    }

    if (completeCandidates.size() == 1 && result.limitReached)
    {
        result.diagnostic = "Found a complete matching library, but the bounded search limit was reached "
            "before uniqueness could be established; choose a narrower folder";
        return result;
    }

    if (completeCandidates.size() == 1)
    {
        result.complete = true;
        result.resolvedRoot = completeCandidates.front().root;
        result.resolvedFiles = std::move(completeCandidates.front().files);
        result.diagnostic = "Resolved all " + juce::String(static_cast<int>(result.resolvedFiles.size()))
            + " imported assets under " + result.resolvedRoot.getFullPathName();
        return result;
    }

    result.diagnostic = "Could not resolve all " + juce::String(static_cast<int>(manifest.entries.size()))
        + " imported assets within the selected folders (scanned " + juce::String(result.scannedFiles)
        + " files in " + juce::String(result.scannedDirectories) + " folders)";
    if (result.limitReached)
        result.diagnostic += "; bounded search limit reached";
    return result;
}
} // namespace audiocity::plugin
