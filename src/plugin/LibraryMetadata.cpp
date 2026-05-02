#include "LibraryMetadata.h"

namespace audiocity::plugin
{
namespace
{
const juce::Identifier kLibraryMetadata{ "libraryMetadata" };
const juce::Identifier kFavorite{ "favorite" };
const juce::Identifier kRecent{ "recent" };
const juce::Identifier kBookmark{ "bookmark" };
const juce::Identifier kTagged{ "tagged" };
const juce::Identifier kTag{ "tag" };
const juce::Identifier kPath{ "path" };
const juce::Identifier kName{ "name" };
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

    const auto existingIndex = indexOfPath(favoritePaths_, normalized);
    if (shouldBeFavorite)
    {
        if (existingIndex < 0)
            favoritePaths_.add(normalized);
        return;
    }

    if (existingIndex >= 0)
        favoritePaths_.remove(existingIndex);
}

bool LibraryMetadata::isFavorite(const juce::String& filePath) const
{
    return indexOfPath(favoritePaths_, filePath) >= 0;
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
}

bool LibraryMetadata::isRecent(const juce::String& filePath) const
{
    return recentRank(filePath) >= 0;
}

int LibraryMetadata::recentRank(const juce::String& filePath) const
{
    return indexOfPath(recentPaths_, filePath);
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
            taggedPaths_.erase(taggedPaths_.begin() + existingIndex);
        return;
    }

    if (existingIndex >= 0)
    {
        taggedPaths_[static_cast<std::size_t>(existingIndex)].tags = normalizedTags;
        return;
    }

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
    for (const auto& taggedPath : taggedPaths_)
    {
        for (const auto& tag : taggedPath.tags)
        {
            if (indexOfPath(tags, tag) < 0)
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
    const auto normalized = normalizePath(filePath);
    if (normalized.isEmpty())
        return -1;

    for (int index = 0; index < static_cast<int>(taggedPaths_.size()); ++index)
    {
        if (taggedPaths_[static_cast<std::size_t>(index)].path.equalsIgnoreCase(normalized))
            return index;
    }

    return -1;
}
} // namespace audiocity::plugin
