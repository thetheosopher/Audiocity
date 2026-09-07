#include "ImportedProgramStore.h"

#include "../engine/EngineCore.h"

#include <utility>

namespace audiocity::plugin
{
namespace
{
bool describesSameAudio(const audiocity::engine::SampleAsset& first,
                        const audiocity::engine::SampleAsset& second) noexcept
{
    return first.sourcePath == second.sourcePath
        && first.lengthSamples == second.lengthSamples
        && first.numChannels == second.numChannels
        && first.sampleRateHz == second.sampleRateHz
        && first.bitDepth == second.bitDepth
        && first.embeddedInProgram == second.embeddedInProgram;
}

bool describesSameAudio(const std::vector<audiocity::engine::SampleAsset>& first,
                        const std::vector<audiocity::engine::SampleAsset>& second) noexcept
{
    if (first.size() != second.size())
        return false;

    for (std::size_t index = 0; index < first.size(); ++index)
    {
        if (!describesSameAudio(first[index], second[index]))
            return false;
    }

    return true;
}
}

ImportedProgramStore::ImportedProgramStore(ProgramSink& sink) noexcept
    : sink_(sink)
{
}

void ImportedProgramStore::loadProgram(const juce::File& programFile,
                                       const ImportedProgramFormat format,
                                       const audiocity::engine::Program& program,
                                       const std::vector<juce::AudioBuffer<float>>& sampleDataByAsset,
                                       const juce::String& diagnosticSummary,
                                       const int zoneCount,
                                       const int selectionIndex)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        programPath_ = programFile.getFullPathName();
        format_ = format;
        selectionIndex_ = selectionIndex;
        programName_ = juce::String::fromUTF8(program.name.c_str());
        program_ = program;
        sampleDataByAsset_ = sampleDataByAsset;
        refreshDerivedStateLocked(diagnosticSummary);
    }

    zoneCount_.store(juce::jmax(0, zoneCount), std::memory_order_relaxed);
    loaded_.store(true, std::memory_order_relaxed);
}

void ImportedProgramStore::clear()
{
    loaded_.store(false, std::memory_order_relaxed);
    zoneCount_.store(0, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(mutex_);
    programPath_.clear();
    format_ = ImportedProgramFormat::unknown;
    selectionIndex_ = -1;
    programName_.clear();
    mapSummary_.clear();
    lastDiagnosticSummary_.clear();
    program_ = {};
    sampleDataByAsset_.clear();
    zoneRows_.clear();
}

ImportedProgramEditOutcome ImportedProgramStore::edit(const Mutator& mutator)
{
    ImportedProgramEditOutcome outcome;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!loaded_.load(std::memory_order_relaxed) || mutator == nullptr)
        return {};

    auto workingProgram = program_;
    ImportedProgramEdit edit{ workingProgram, sampleDataByAsset_ };
    outcome = mutator(edit);

    if (!outcome.ok)
    {
        outcome.resultIndex = -1;
        outcome.appendedSampleData.clear();
        return outcome;
    }

    const auto capacity = audiocity::engine::EngineCore::validateProgramForPublish(workingProgram);
    if (!capacity)
    {
        outcome.ok = false;
        outcome.resultIndex = -1;
        outcome.appendedSampleData.clear();
        outcome.label = capacity.diagnostic;
        lastDiagnosticSummary_ = capacity.diagnostic;
        return outcome;
    }

    // Most edits only move zones around. Recognising that lets the sink skip re-deriving
    // audio it already holds.
    const auto audioChanged = !outcome.appendedSampleData.empty()
        || !describesSameAudio(program_.sampleAssets, workingProgram.sampleAssets);

    std::vector<juce::AudioBuffer<float>> workingSampleData;
    if (audioChanged || outcome.publish)
    {
        workingSampleData = sampleDataByAsset_;
        for (const auto& appended : outcome.appendedSampleData)
            workingSampleData.push_back(appended);
    }

    if (outcome.publish)
    {
        auto published = !audioChanged && sink_.republishProgramMetadata(workingProgram);
        juce::String publishDiagnostic;
        if (!published)
        {
            published = sink_.republishProgramChecked(
                workingProgram, workingSampleData, publishDiagnostic);
        }

        if (!published)
        {
            outcome.ok = false;
            outcome.resultIndex = -1;
            outcome.appendedSampleData.clear();
            outcome.label = publishDiagnostic.isNotEmpty()
                ? publishDiagnostic
                : "Program publication failed; previous program preserved";
            lastDiagnosticSummary_ = outcome.label;
            return outcome;
        }
    }

    program_ = std::move(workingProgram);
    if (audioChanged)
        sampleDataByAsset_ = std::move(workingSampleData);
    outcome.appendedSampleData.clear();
    refreshDerivedStateLocked(outcome.label);
    return outcome;
}

juce::String ImportedProgramStore::getProgramPath() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return programPath_;
}

ImportedProgramFormat ImportedProgramStore::getFormat() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return format_;
}

int ImportedProgramStore::getSelectionIndex() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return selectionIndex_;
}

juce::String ImportedProgramStore::getProgramName() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return programName_;
}

juce::String ImportedProgramStore::getMapSummary() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return mapSummary_;
}

juce::String ImportedProgramStore::getLastDiagnosticSummary() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastDiagnosticSummary_;
}

void ImportedProgramStore::setLastDiagnosticSummary(const juce::String& diagnosticSummary)
{
    std::lock_guard<std::mutex> lock(mutex_);
    lastDiagnosticSummary_ = diagnosticSummary;
}

std::vector<ProgramZoneListRow> ImportedProgramStore::getZoneRows() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return zoneRows_;
}

void ImportedProgramStore::setSavedLocation(const juce::File& programFile,
                                            const ImportedProgramFormat format,
                                            const juce::String& diagnosticSummary)
{
    std::lock_guard<std::mutex> lock(mutex_);
    programPath_ = programFile.getFullPathName();
    format_ = format;
    lastDiagnosticSummary_ = diagnosticSummary;
}

ImportedProgramPersistentState ImportedProgramStore::capturePersistentState() const
{
    ImportedProgramPersistentState state;
    audiocity::engine::Program program;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state.programPath = programPath_;
        state.format = format_;
        state.selectionIndex = selectionIndex_;
        state.mappingState = createProgramZoneMappingState(program_);
        program = program_;
    }
    if (state.programPath.isNotEmpty())
        state.assetManifest = createImportedAssetManifest(juce::File(state.programPath), program);
    return state;
}

void ImportedProgramStore::captureSnapshot(audiocity::engine::Program& programOut,
                                           std::vector<juce::AudioBuffer<float>>& sampleDataOut) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    programOut = program_;
    sampleDataOut = sampleDataByAsset_;
}

bool ImportedProgramStore::read(
    const std::function<void(const audiocity::engine::Program&,
                             const std::vector<juce::AudioBuffer<float>>&)>& reader) const
{
    if (reader == nullptr)
        return false;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!loaded_.load(std::memory_order_relaxed))
        return false;

    reader(program_, sampleDataByAsset_);
    return true;
}

void ImportedProgramStore::refreshDerivedStateLocked(const juce::String& diagnosticSummary)
{
    const auto derivedState = buildImportedProgramDerivedState(program_);
    mapSummary_ = derivedState.mapSummary;
    zoneRows_ = derivedState.zoneRows;
    zoneCount_.store(static_cast<int>(program_.zones.size()), std::memory_order_relaxed);
    lastDiagnosticSummary_ = diagnosticSummary;
}
}
