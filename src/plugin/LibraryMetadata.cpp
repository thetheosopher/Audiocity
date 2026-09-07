#include "LibraryMetadata.h"

namespace audiocity::plugin
{
namespace
{
#define AUDIOCITY_LIBRARY_METADATA_IDENTIFIER(name, text) \
    const juce::Identifier& name##Identifier() \
    { \
        static const juce::Identifier identifier{ text }; \
        return identifier; \
    }

AUDIOCITY_LIBRARY_METADATA_IDENTIFIER(kLibraryMetadata, "libraryMetadata")
AUDIOCITY_LIBRARY_METADATA_IDENTIFIER(kFavorite, "favorite")
AUDIOCITY_LIBRARY_METADATA_IDENTIFIER(kRecent, "recent")
AUDIOCITY_LIBRARY_METADATA_IDENTIFIER(kBookmark, "bookmark")
AUDIOCITY_LIBRARY_METADATA_IDENTIFIER(kTagged, "tagged")
AUDIOCITY_LIBRARY_METADATA_IDENTIFIER(kTag, "tag")
AUDIOCITY_LIBRARY_METADATA_IDENTIFIER(kPath, "path")
AUDIOCITY_LIBRARY_METADATA_IDENTIFIER(kName, "name")

#define kLibraryMetadata kLibraryMetadataIdentifier()
#define kFavorite kFavoriteIdentifier()
#define kRecent kRecentIdentifier()
#define kBookmark kBookmarkIdentifier()
#define kTagged kTaggedIdentifier()
#define kTag kTagIdentifier()
#define kPath kPathIdentifier()
#define kName kNameIdentifier()
}

juce::Identifier LibraryMetadata::valueTreeType()
{
    return kLibraryMetadata;
}

void LibraryMetadata::setFavorite(const juce::String& filePath, const bool shouldBeFavorite)
{
    const auto normalized = normalizePath(filePath);
    if (normalized.isEmpty())
        return;

    const auto key = pathKey(normalized);
    const auto isAlreadyFavorite = favoritePathKeys_.find(key) != favoritePathKeys_.end();
    if (shouldBeFavorite)
    {
        if (!isAlreadyFavorite)
        {
            favoritePaths_.add(normalized);
            favoritePathKeys_.insert(key);
        }
        return;
    }

    if (!isAlreadyFavorite)
        return;

    const auto existingIndex = indexOfPath(favoritePaths_, normalized);
    if (existingIndex >= 0)
        favoritePaths_.remove(existingIndex);
    favoritePathKeys_.erase(key);
}

bool LibraryMetadata::isFavorite(const juce::String& filePath) const
{
    const auto key = pathKey(filePath);
    return !key.empty() && favoritePathKeys_.find(key) != favoritePathKeys_.end();
}

void LibraryMetadata::markRecent(const juce::String& filePath)
{
    const auto normalized = normalizePath(filePath);
    if (normalized.isEmpty())
        return;

    const auto existingIndex = indexOfPath(recentPaths_, normalized);
    if (existingIndex >= 0)
        recentPaths_.remove(existingIndex);

    recentPaths_.insert(0, normalized);
    while (recentPaths_.size() > maxRecentItems)
        recentPaths_.remove(recentPaths_.size() - 1);
    rebuildRecentRanks();
}

bool LibraryMetadata::isRecent(const juce::String& filePath) const
{
    return recentRank(filePath) >= 0;
}

int LibraryMetadata::recentRank(const juce::String& filePath) const
{
    const auto key = pathKey(filePath);
    if (const auto found = recentRanksByPath_.find(key); found != recentRanksByPath_.end())
        return found->second;
    return -1;
}

void LibraryMetadata::setTags(const juce::String& filePath, const juce::StringArray& tags)
{
    const auto normalizedPath = normalizePath(filePath);
    if (normalizedPath.isEmpty())
        return;

    const auto normalizedTags = normalizeTags(tags);
    const auto existingIndex = indexOfTaggedPath(normalizedPath);
    if (normalizedTags.isEmpty())
    {
        if (existingIndex >= 0)
        {
            taggedPathIndices_.erase(pathKey(normalizedPath));
            taggedPaths_.erase(taggedPaths_.begin() + existingIndex);
            rebuildTaggedPathIndices(static_cast<std::size_t>(existingIndex));
        }
        return;
    }

    if (existingIndex >= 0)
    {
        taggedPaths_[static_cast<std::size_t>(existingIndex)].tags = normalizedTags;
        return;
    }

    taggedPathIndices_[pathKey(normalizedPath)] = taggedPaths_.size();
    taggedPaths_.push_back({ normalizedPath, normalizedTags });
}

juce::StringArray LibraryMetadata::getTags(const juce::String& filePath) const
{
    const auto taggedIndex = indexOfTaggedPath(filePath);
    if (taggedIndex < 0)
        return {};

    return taggedPaths_[static_cast<std::size_t>(taggedIndex)].tags;
}

juce::StringArray LibraryMetadata::getAllTags() const
{
    juce::StringArray tags;
    std::unordered_set<std::string> knownTags;
    for (const auto& taggedPath : taggedPaths_)
    {
        for (const auto& tag : taggedPath.tags)
        {
            const auto key = tag.toLowerCase().toStdString();
            if (knownTags.insert(key).second)
                tags.add(tag);
        }
    }

    tags.sort(true);
    return tags;
}

bool LibraryMetadata::hasTag(const juce::String& filePath, const juce::String& tag) const
{
    const auto normalizedTag = normalizeTag(tag);
    if (normalizedTag.isEmpty())
        return false;

    const auto tags = getTags(filePath);
    return indexOfPath(tags, normalizedTag) >= 0;
}

void LibraryMetadata::addBookmark(const juce::String& folderPath)
{
    const auto normalized = normalizePath(folderPath);
    if (normalized.isEmpty())
        return;

    const auto existingIndex = indexOfPath(bookmarkPaths_, normalized);
    if (existingIndex >= 0)
        bookmarkPaths_.remove(existingIndex);

    bookmarkPaths_.insert(0, normalized);
    while (bookmarkPaths_.size() > maxBookmarkItems)
        bookmarkPaths_.remove(bookmarkPaths_.size() - 1);
}

void LibraryMetadata::removeBookmark(const juce::String& folderPath)
{
    const auto existingIndex = indexOfPath(bookmarkPaths_, folderPath);
    if (existingIndex >= 0)
        bookmarkPaths_.remove(existingIndex);
}

bool LibraryMetadata::isBookmark(const juce::String& folderPath) const
{
    return indexOfPath(bookmarkPaths_, folderPath) >= 0;
}

juce::StringArray LibraryMetadata::getFavoritePaths() const
{
    return favoritePaths_;
}

juce::StringArray LibraryMetadata::getRecentPaths() const
{
    return recentPaths_;
}

juce::StringArray LibraryMetadata::getBookmarkPaths() const
{
    return bookmarkPaths_;
}

juce::ValueTree LibraryMetadata::toValueTree() const
{
    juce::ValueTree tree(kLibraryMetadata);

    for (const auto& path : favoritePaths_)
    {
        auto child = juce::ValueTree(kFavorite);
        child.setProperty(kPath, path, nullptr);
        tree.appendChild(child, nullptr);
    }

    for (const auto& path : recentPaths_)
    {
        auto child = juce::ValueTree(kRecent);
        child.setProperty(kPath, path, nullptr);
        tree.appendChild(child, nullptr);
    }

    for (const auto& path : bookmarkPaths_)
    {
        auto child = juce::ValueTree(kBookmark);
        child.setProperty(kPath, path, nullptr);
        tree.appendChild(child, nullptr);
    }

    for (const auto& taggedPath : taggedPaths_)
    {
        auto child = juce::ValueTree(kTagged);
        child.setProperty(kPath, taggedPath.path, nullptr);

        for (const auto& tag : taggedPath.tags)
        {
            auto tagChild = juce::ValueTree(kTag);
            tagChild.setProperty(kName, tag, nullptr);
            child.appendChild(tagChild, nullptr);
        }

        tree.appendChild(child, nullptr);
    }

    return tree;
}

LibraryMetadata LibraryMetadata::fromValueTree(const juce::ValueTree& tree)
{
    LibraryMetadata metadata;
    if (!tree.isValid() || !tree.hasType(kLibraryMetadata))
        return metadata;

    for (int childIndex = 0; childIndex < tree.getNumChildren(); ++childIndex)
    {
        const auto child = tree.getChild(childIndex);
        const auto path = child.getProperty(kPath).toString();
        if (child.hasType(kFavorite))
            metadata.setFavorite(path, true);
        else if (child.hasType(kTagged))
        {
            juce::StringArray tags;
            for (int tagIndex = 0; tagIndex < child.getNumChildren(); ++tagIndex)
            {
                const auto tagChild = child.getChild(tagIndex);
                if (tagChild.hasType(kTag))
                    tags.add(tagChild.getProperty(kName).toString());
            }

            metadata.setTags(path, tags);
        }
    }

    for (int childIndex = tree.getNumChildren() - 1; childIndex >= 0; --childIndex)
    {
        const auto child = tree.getChild(childIndex);
        if (child.hasType(kRecent))
        {
            const auto path = child.getProperty(kPath).toString();
            metadata.markRecent(path);
        }
    }

    for (int childIndex = tree.getNumChildren() - 1; childIndex >= 0; --childIndex)
    {
        const auto child = tree.getChild(childIndex);
        if (child.hasType(kBookmark))
        {
            const auto path = child.getProperty(kPath).toString();
            metadata.addBookmark(path);
        }
    }

    return metadata;
}

juce::String LibraryMetadata::normalizePath(const juce::String& filePath)
{
    return filePath.trim().unquoted().replaceCharacter('\\', '/');
}

std::string LibraryMetadata::pathKey(const juce::String& filePath)
{
    return normalizePath(filePath).toLowerCase().toStdString();
}

juce::String LibraryMetadata::normalizeTag(const juce::String& tag)
{
    return tag.trim().unquoted().removeCharacters("#");
}

juce::StringArray LibraryMetadata::normalizeTags(const juce::StringArray& tags)
{
    juce::StringArray normalized;
    for (const auto& tag : tags)
    {
        const auto normalizedTag = normalizeTag(tag);
        if (normalizedTag.isEmpty())
            continue;

        bool alreadyPresent = false;
        for (const auto& existingTag : normalized)
        {
            if (existingTag.equalsIgnoreCase(normalizedTag))
            {
                alreadyPresent = true;
                break;
            }
        }

        if (!alreadyPresent)
            normalized.add(normalizedTag);
    }

    return normalized;
}

int LibraryMetadata::indexOfPath(const juce::StringArray& paths, const juce::String& filePath)
{
    const auto normalized = normalizePath(filePath);
    if (normalized.isEmpty())
        return -1;

    for (int index = 0; index < paths.size(); ++index)
    {
        if (paths[index].equalsIgnoreCase(normalized))
            return index;
    }

    return -1;
}

int LibraryMetadata::indexOfTaggedPath(const juce::String& filePath) const
{
    const auto key = pathKey(filePath);
    if (key.empty())
        return -1;

    if (const auto found = taggedPathIndices_.find(key); found != taggedPathIndices_.end())
        return static_cast<int>(found->second);

    return -1;
}

void LibraryMetadata::rebuildRecentRanks()
{
    recentRanksByPath_.clear();
    recentRanksByPath_.reserve(static_cast<std::size_t>(recentPaths_.size()));
    for (int index = 0; index < recentPaths_.size(); ++index)
        recentRanksByPath_[pathKey(recentPaths_[index])] = index;
}

void LibraryMetadata::rebuildTaggedPathIndices(const std::size_t firstChangedIndex)
{
    for (auto index = firstChangedIndex; index < taggedPaths_.size(); ++index)
        taggedPathIndices_[pathKey(taggedPaths_[index].path)] = index;
}
} // namespace audiocity::plugin
