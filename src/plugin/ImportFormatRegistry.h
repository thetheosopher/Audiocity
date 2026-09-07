#pragma once

#include "ImportedProgramState.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace audiocity::plugin
{
enum class ImportFormatAvailability
{
    always,
    rexRuntime,
    ncwConverter,
    diagnosticOnly
};

struct ImportFormatCapabilities
{
    bool rexRuntimeAvailable = false;
    bool ncwConverterAvailable = false;
};

struct ImportFormatDescriptor
{
    ImportedProgramFormat format = ImportedProgramFormat::unknown;
    std::string_view stateToken;
    std::string_view badge;
    std::string_view description;
    std::array<std::string_view, 5> extensions{};
    std::size_t extensionCount = 0;
    bool isInstrument = false;
    ImportFormatAvailability availability = ImportFormatAvailability::always;
    std::string_view unavailableMessage;
    std::string_view requiredFileName;
    bool supportsMappingDrag = false;
    bool supportsBackgroundImport = true;
};

[[nodiscard]] std::span<const ImportFormatDescriptor> importFormatDescriptors() noexcept;
[[nodiscard]] const ImportFormatDescriptor* findImportFormatDescriptorForPath(const juce::String& path) noexcept;
[[nodiscard]] const ImportFormatDescriptor* findImportFormatDescriptor(ImportedProgramFormat format) noexcept;
[[nodiscard]] ImportedProgramFormat parseImportedProgramFormatToken(const juce::String& token) noexcept;
[[nodiscard]] juce::String importedProgramFormatToken(ImportedProgramFormat format);
[[nodiscard]] bool isKnownImportPath(const juce::String& path) noexcept;
[[nodiscard]] bool isMappingImportPath(const juce::String& path) noexcept;
[[nodiscard]] bool supportsBackgroundImport(ImportedProgramFormat format) noexcept;
[[nodiscard]] bool isImportFormatAvailable(const ImportFormatDescriptor& descriptor,
                                           const ImportFormatCapabilities& capabilities) noexcept;
[[nodiscard]] bool isImportPathAvailable(const juce::String& path,
                                         const ImportFormatCapabilities& capabilities) noexcept;
[[nodiscard]] juce::String importPathUnavailableMessage(const juce::String& path,
                                                        const ImportFormatCapabilities& capabilities);
[[nodiscard]] juce::String buildImportChooserWildcard(const ImportFormatCapabilities& capabilities,
                                                      bool includeUnavailable = true);
[[nodiscard]] juce::String buildImportChooserTitle(const ImportFormatCapabilities& capabilities);
[[nodiscard]] ImportFormatCapabilities currentImportFormatCapabilities(bool rexRuntimeAvailable);
} // namespace audiocity::plugin
