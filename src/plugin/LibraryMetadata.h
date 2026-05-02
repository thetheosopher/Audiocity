#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <vector>

namespace audiocity::plugin
{
class LibraryMetadata
{
public:
    static constexpr int maxRecentItems = 64;
    static constexpr int maxBookmarkItems = 32;

    [[nodiscard]] static juce::Identifier valueTreeType();

    void setFavorite(const juce::String& filePath, bool shouldBeFavorite);
    [[nodiscard]] bool isFavorite(const juce::String& filePath) const;

    void markRecent(const juce::String& filePath);
    [[nodiscard]] bool isRecent(const juce::String& filePath) const;
    [[nodiscard]] int recentRank(const juce::String& filePath) const;

    void setTags(const juce::String& filePath, const juce::StringArray& tags);
    [[nodiscard]] juce::StringArray getTags(const juce::String& filePath) const;
    [[nodiscard]] juce::StringArray getAllTags() const;
    [[nodiscard]] bool hasTag(const juce::String& filePath, const juce::String& tag) const;

    void addBookmark(const juce::String& folderPath);
    void removeBookmark(const juce::String& folderPath);
    [[nodiscard]] bool isBookmark(const juce::String& folderPath) const;

    [[nodiscard]] juce::StringArray getFavoritePaths() const;
    [[nodiscard]] juce::StringArray getRecentPaths() const;
    [[nodiscard]] juce::StringArray getBookmarkPaths() const;

    [[nodiscard]] juce::ValueTree toValueTree() const;
    [[nodiscard]] static LibraryMetadata fromValueTree(const juce::ValueTree& tree);

private:
    struct TaggedPath
    {
        juce::String path;
        juce::StringArray tags;
    };

    [[nodiscard]] static juce::String normalizePath(const juce::String& filePath);
    [[nodiscard]] static juce::String normalizeTag(const juce::String& tag);
    [[nodiscard]] static juce::StringArray normalizeTags(const juce::StringArray& tags);
    [[nodiscard]] static int indexOfPath(const juce::StringArray& paths, const juce::String& filePath);
    [[nodiscard]] int indexOfTaggedPath(const juce::String& filePath) const;

    juce::StringArray favoritePaths_;
    juce::StringArray recentPaths_;
    juce::StringArray bookmarkPaths_;
    std::vector<TaggedPath> taggedPaths_;
};
} // namespace audiocity::plugin
