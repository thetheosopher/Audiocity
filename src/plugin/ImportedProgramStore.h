#pragma once

#include "../engine/ProgramModel.h"
#include "ImportedProgramState.h"
#include "ProgramMappingModel.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

namespace audiocity::plugin
{
/** Destination for a program that has become current.

    Implementations own whatever the destination needs in order to adopt a program safely;
    the engine adapter silences sounding voices before publishing. Callers cannot get that
    ordering wrong because they never see the steps.
*/
class ProgramSink
{
public:
    virtual ~ProgramSink() = default;

    virtual void republishProgram(const audiocity::engine::Program& program,
                                  const std::vector<juce::AudioBuffer<float>>& sampleDataByAsset) = 0;

    /** Adopts a program whose audio the destination already holds. Returns false if it cannot,
        in which case the store publishes the sample data as well. */
    virtual bool republishProgramMetadata(const audiocity::engine::Program& program) = 0;
};

/** What a mutation reports back to the store.

    `appendedSampleData` is committed only when `ok` is true, which keeps a failed edit from
    leaving half-added audio behind without copying the whole sample vector for rollback.
*/
struct ImportedProgramEditOutcome
{
    bool ok = false;
    int resultIndex = -1;
    juce::String label;
    std::vector<juce::AudioBuffer<float>> appendedSampleData;

    /** Cleared when the committed program has nothing worth sounding, so that the previously
        published program is left alone rather than replaced by one that cannot play. */
    bool publish = true;
};

/** What a mutation is handed: a working copy of the program, and the sample data as it
    stands. Preconditions that depend on either belong in the mutation itself.
*/
struct ImportedProgramEdit
{
    audiocity::engine::Program& program;
    const std::vector<juce::AudioBuffer<float>>& sampleDataByAsset;
};

/** Everything a saved patch needs in order to bring an imported program back. */
struct ImportedProgramPersistentState
{
    juce::String programPath;
    ImportedProgramFormat format = ImportedProgramFormat::unknown;
    int selectionIndex = -1;
    juce::ValueTree mappingState;
};

/** Owns the imported program, its sample data, its derived state, and the publish.

    Every mutation runs the same protocol: take the lock, work on a copy, commit only on
    success, refresh derived state, then publish outside the lock. No caller can skip a step.
*/
class ImportedProgramStore final
{
public:
    using Mutator = std::function<ImportedProgramEditOutcome(ImportedProgramEdit&)>;

    explicit ImportedProgramStore(ProgramSink& sink) noexcept;

    ImportedProgramStore(const ImportedProgramStore&) = delete;
    ImportedProgramStore& operator=(const ImportedProgramStore&) = delete;

    [[nodiscard]] bool isLoaded() const noexcept
    {
        return loaded_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] int getZoneCount() const noexcept
    {
        return zoneCount_.load(std::memory_order_relaxed);
    }

    /** Replaces the imported program wholesale. Does not publish: the load paths publish the
        program and its display sample themselves, in an order this store does not dictate. */
    void loadProgram(const juce::File& programFile,
                     ImportedProgramFormat format,
                     const audiocity::engine::Program& program,
                     const std::vector<juce::AudioBuffer<float>>& sampleDataByAsset,
                     const juce::String& diagnosticSummary,
                     int zoneCount,
                     int selectionIndex);

    void clear();

    /** Runs one mutation through the full protocol. Publishes when the mutation succeeds. */
    ImportedProgramEditOutcome edit(const Mutator& mutator);

    [[nodiscard]] juce::String getProgramPath() const;
    [[nodiscard]] ImportedProgramFormat getFormat() const;
    [[nodiscard]] int getSelectionIndex() const;
    [[nodiscard]] juce::String getProgramName() const;
    [[nodiscard]] juce::String getMapSummary() const;
    [[nodiscard]] juce::String getLastDiagnosticSummary() const;
    void setLastDiagnosticSummary(const juce::String& diagnosticSummary);
    [[nodiscard]] std::vector<ProgramZoneListRow> getZoneRows() const;

    /** Records where the program now lives on disk, after it has been exported. */
    void setSavedLocation(const juce::File& programFile,
                          ImportedProgramFormat format,
                          const juce::String& diagnosticSummary);

    [[nodiscard]] ImportedProgramPersistentState capturePersistentState() const;

    void captureSnapshot(audiocity::engine::Program& programOut,
                         std::vector<juce::AudioBuffer<float>>& sampleDataOut) const;

    /** Reads the loaded program under the store's lock, and reports whether there was one.
        The reader must not call back into the store, and must not retain the references it
        is given. */
    bool read(const std::function<void(const audiocity::engine::Program&,
                                       const std::vector<juce::AudioBuffer<float>>&)>& reader) const;

private:
    void refreshDerivedStateLocked(const juce::String& diagnosticSummary);

    ProgramSink& sink_;

    mutable std::mutex mutex_;
    std::atomic<bool> loaded_{ false };
    std::atomic<int> zoneCount_{ 0 };

    juce::String programPath_;
    ImportedProgramFormat format_ = ImportedProgramFormat::unknown;
    int selectionIndex_ = -1;
    juce::String programName_;
    juce::String mapSummary_;
    juce::String lastDiagnosticSummary_;
    audiocity::engine::Program program_;
    std::vector<juce::AudioBuffer<float>> sampleDataByAsset_;
    std::vector<ProgramZoneListRow> zoneRows_;
};
}
