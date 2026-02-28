#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace
{
constexpr auto kPatchRoot = "AudiocityPatch";
constexpr auto kSamplePath = "samplePath";
constexpr auto kRootMidiNote = "rootMidiNote";

constexpr auto kAmpAttack = "ampAttack";
constexpr auto kAmpDecay = "ampDecay";
constexpr auto kAmpSustain = "ampSustain";
constexpr auto kAmpRelease = "ampRelease";

constexpr auto kFilterAttack = "filterAttack";
constexpr auto kFilterDecay = "filterDecay";
constexpr auto kFilterSustain = "filterSustain";
constexpr auto kFilterRelease = "filterRelease";
constexpr auto kFilterBaseCutoff = "filterBaseCutoff";
constexpr auto kFilterEnvAmount = "filterEnvAmount";
constexpr auto kFilterResonance = "filterResonance";
constexpr auto kPlaybackMode = "playbackMode";
constexpr auto kQualityTier = "qualityTier";
constexpr auto kPreloadSamples = "preloadSamples";
constexpr auto kMonoMode = "monoMode";
constexpr auto kLegatoMode = "legatoMode";
constexpr auto kGlideSeconds = "glideSeconds";
constexpr auto kChokeGroup = "chokeGroup";
constexpr auto kSampleWindowStart = "sampleWindowStart";
constexpr auto kSampleWindowEnd = "sampleWindowEnd";
constexpr auto kLoopStart = "loopStart";
constexpr auto kLoopEnd = "loopEnd";
constexpr auto kFadeInSamples = "fadeInSamples";
constexpr auto kFadeOutSamples = "fadeOutSamples";
constexpr auto kReversePlayback = "reversePlayback";
constexpr auto kCcMappings = "ccMappings";
constexpr auto kCcEntry = "cc";
constexpr auto kCcNumber = "ccNum";
constexpr auto kCcParam = "ccParam";
}

AudiocityAudioProcessor::AudiocityAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void AudiocityAudioProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    engine_.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
}

void AudiocityAudioProcessor::releaseResources()
{
    engine_.release();
}

bool AudiocityAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

void AudiocityAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numInputChannels = getTotalNumInputChannels();
    const auto numOutputChannels = getTotalNumOutputChannels();

    for (auto channel = numInputChannels; channel < numOutputChannels; ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    // Extract CC messages and push to FIFO for the editor to consume
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        if (msg.isController())
            pushCcEvent(msg.getControllerNumber(), msg.getControllerValue());
    }

    engine_.render(buffer, midiMessages);
}

juce::AudioProcessorEditor* AudiocityAudioProcessor::createEditor()
{
    return new AudiocityAudioProcessorEditor(*this);
}

void AudiocityAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = juce::ValueTree(kPatchRoot);

    state.setProperty(kSamplePath, engine_.getSamplePath(), nullptr);
    state.setProperty(kRootMidiNote, engine_.getRootMidiNote(), nullptr);

    const auto amp = engine_.getAmpEnvelope();
    state.setProperty(kAmpAttack, amp.attackSeconds, nullptr);
    state.setProperty(kAmpDecay, amp.decaySeconds, nullptr);
    state.setProperty(kAmpSustain, amp.sustainLevel, nullptr);
    state.setProperty(kAmpRelease, amp.releaseSeconds, nullptr);

    const auto filterAdsr = engine_.getFilterEnvelope();
    state.setProperty(kFilterAttack, filterAdsr.attackSeconds, nullptr);
    state.setProperty(kFilterDecay, filterAdsr.decaySeconds, nullptr);
    state.setProperty(kFilterSustain, filterAdsr.sustainLevel, nullptr);
    state.setProperty(kFilterRelease, filterAdsr.releaseSeconds, nullptr);

    const auto filter = engine_.getFilterSettings();
    state.setProperty(kFilterBaseCutoff, filter.baseCutoffHz, nullptr);
    state.setProperty(kFilterEnvAmount, filter.envAmountHz, nullptr);
    state.setProperty(kFilterResonance, filter.resonance, nullptr);
    state.setProperty(kPlaybackMode,
        getPlaybackMode() == PlaybackMode::oneShot ? 1 : (getPlaybackMode() == PlaybackMode::loop ? 2 : 0),
        nullptr);
    state.setProperty(kQualityTier, getQualityTier() == QualityTier::cpu ? 0 : 1, nullptr);
    state.setProperty(kPreloadSamples, getPreloadSamples(), nullptr);
    state.setProperty(kMonoMode, getMonoMode() ? 1 : 0, nullptr);
    state.setProperty(kLegatoMode, getLegatoMode() ? 1 : 0, nullptr);
    state.setProperty(kGlideSeconds, getGlideSeconds(), nullptr);
    state.setProperty(kChokeGroup, getChokeGroup(), nullptr);
    state.setProperty(kSampleWindowStart, getSampleWindowStart(), nullptr);
    state.setProperty(kSampleWindowEnd, getSampleWindowEnd(), nullptr);
    state.setProperty(kLoopStart, getLoopStart(), nullptr);
    state.setProperty(kLoopEnd, getLoopEnd(), nullptr);
    state.setProperty(kFadeInSamples, getFadeInSamples(), nullptr);
    state.setProperty(kFadeOutSamples, getFadeOutSamples(), nullptr);
    state.setProperty(kReversePlayback, getReversePlayback() ? 1 : 0, nullptr);

    // Save CC mappings
    {
        auto mappingsNode = juce::ValueTree(kCcMappings);
        const auto mappings = getAllCcMappings();
        for (const auto& [ccNum, paramId] : mappings)
        {
            auto entry = juce::ValueTree(kCcEntry);
            entry.setProperty(kCcNumber, ccNum, nullptr);
            entry.setProperty(kCcParam, paramId, nullptr);
            mappingsNode.appendChild(entry, nullptr);
        }
        state.appendChild(mappingsNode, nullptr);
    }

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void AudiocityAudioProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr)
        return;

    const auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid() || !state.hasType(kPatchRoot))
        return;

    const auto samplePath = state.getProperty(kSamplePath).toString();
    if (samplePath.isNotEmpty())
        loadSampleFromFile(juce::File(samplePath));

    engine_.setRootMidiNote(static_cast<int>(state.getProperty(kRootMidiNote, engine_.getRootMidiNote())));

    auto amp = engine_.getAmpEnvelope();
    amp.attackSeconds = static_cast<float>(state.getProperty(kAmpAttack, amp.attackSeconds));
    amp.decaySeconds = static_cast<float>(state.getProperty(kAmpDecay, amp.decaySeconds));
    amp.sustainLevel = static_cast<float>(state.getProperty(kAmpSustain, amp.sustainLevel));
    amp.releaseSeconds = static_cast<float>(state.getProperty(kAmpRelease, amp.releaseSeconds));
    engine_.setAmpEnvelope(amp);

    auto filterAdsr = engine_.getFilterEnvelope();
    filterAdsr.attackSeconds = static_cast<float>(state.getProperty(kFilterAttack, filterAdsr.attackSeconds));
    filterAdsr.decaySeconds = static_cast<float>(state.getProperty(kFilterDecay, filterAdsr.decaySeconds));
    filterAdsr.sustainLevel = static_cast<float>(state.getProperty(kFilterSustain, filterAdsr.sustainLevel));
    filterAdsr.releaseSeconds = static_cast<float>(state.getProperty(kFilterRelease, filterAdsr.releaseSeconds));
    engine_.setFilterEnvelope(filterAdsr);

    auto filter = engine_.getFilterSettings();
    filter.baseCutoffHz = static_cast<float>(state.getProperty(kFilterBaseCutoff, filter.baseCutoffHz));
    filter.envAmountHz = static_cast<float>(state.getProperty(kFilterEnvAmount, filter.envAmountHz));
    filter.resonance = static_cast<float>(state.getProperty(kFilterResonance, filter.resonance));
    engine_.setFilterSettings(filter);

    const auto playbackMode = static_cast<int>(state.getProperty(kPlaybackMode, 0));
    setPlaybackMode(playbackMode == 1 ? PlaybackMode::oneShot : (playbackMode == 2 ? PlaybackMode::loop : PlaybackMode::gate));
    setQualityTier(static_cast<int>(state.getProperty(kQualityTier, 1)) == 0 ? QualityTier::cpu : QualityTier::fidelity);
    setPreloadSamples(static_cast<int>(state.getProperty(kPreloadSamples, getPreloadSamples())));
    setMonoMode(static_cast<int>(state.getProperty(kMonoMode, getMonoMode() ? 1 : 0)) == 1);
    setLegatoMode(static_cast<int>(state.getProperty(kLegatoMode, getLegatoMode() ? 1 : 0)) == 1);
    setGlideSeconds(static_cast<float>(state.getProperty(kGlideSeconds, getGlideSeconds())));
    setChokeGroup(static_cast<int>(state.getProperty(kChokeGroup, getChokeGroup())));
    setSampleWindow(
        static_cast<int>(state.getProperty(kSampleWindowStart, getSampleWindowStart())),
        static_cast<int>(state.getProperty(kSampleWindowEnd, getSampleWindowEnd())));
    setLoopPoints(
        static_cast<int>(state.getProperty(kLoopStart, getLoopStart())),
        static_cast<int>(state.getProperty(kLoopEnd, getLoopEnd())));
    setFadeSamples(
        static_cast<int>(state.getProperty(kFadeInSamples, getFadeInSamples())),
        static_cast<int>(state.getProperty(kFadeOutSamples, getFadeOutSamples())));
    setReversePlayback(static_cast<int>(state.getProperty(kReversePlayback, getReversePlayback() ? 1 : 0)) == 1);

    // Restore CC mappings
    {
        std::lock_guard<std::mutex> lock(ccMappingMutex_);
        ccToParam_.clear();
        const auto mappingsNode = state.getChildWithName(kCcMappings);
        for (int i = 0; i < mappingsNode.getNumChildren(); ++i)
        {
            const auto entry = mappingsNode.getChild(i);
            if (entry.hasType(kCcEntry))
            {
                const auto ccNum = static_cast<int>(entry.getProperty(kCcNumber, -1));
                const auto paramId = entry.getProperty(kCcParam).toString();
                if (ccNum >= 0 && ccNum <= 127 && paramId.isNotEmpty())
                    ccToParam_[ccNum] = paramId;
            }
        }
    }
}

bool AudiocityAudioProcessor::loadSampleFromFile(const juce::File& file)
{
    return engine_.loadSampleFromFile(file);
}

juce::String AudiocityAudioProcessor::getLoadedSamplePath() const
{
    return engine_.getSamplePath();
}

void AudiocityAudioProcessor::setPlaybackMode(const PlaybackMode mode) noexcept
{
    engine_.setPlaybackMode(mode);
}

AudiocityAudioProcessor::PlaybackMode AudiocityAudioProcessor::getPlaybackMode() const noexcept
{
    return engine_.getPlaybackMode();
}

// ─── CC FIFO ───────────────────────────────────────────────────────────────────

void AudiocityAudioProcessor::pushCcEvent(const int ccNumber, const int value)
{
    const auto writePos = ccFifoWritePos_.load(std::memory_order_relaxed);
    const auto nextWrite = (writePos + 1) % kCcFifoSize;

    // If full, drop oldest by advancing read pos
    if (nextWrite == ccFifoReadPos_.load(std::memory_order_acquire))
        ccFifoReadPos_.store((ccFifoReadPos_.load(std::memory_order_relaxed) + 1) % kCcFifoSize,
                             std::memory_order_release);

    ccFifo_[static_cast<std::size_t>(writePos)] = { ccNumber, value };
    ccFifoWritePos_.store(nextWrite, std::memory_order_release);
}

bool AudiocityAudioProcessor::popCcEvent(CcEvent& out)
{
    const auto readPos = ccFifoReadPos_.load(std::memory_order_relaxed);
    if (readPos == ccFifoWritePos_.load(std::memory_order_acquire))
        return false;

    out = ccFifo_[static_cast<std::size_t>(readPos)];
    ccFifoReadPos_.store((readPos + 1) % kCcFifoSize, std::memory_order_release);
    return true;
}

// ─── CC Mapping ────────────────────────────────────────────────────────────────

void AudiocityAudioProcessor::setCcMapping(const int ccNumber, const juce::String& paramId)
{
    std::lock_guard<std::mutex> lock(ccMappingMutex_);
    // Remove any existing mapping to this param
    for (auto it = ccToParam_.begin(); it != ccToParam_.end(); )
    {
        if (it->second == paramId)
            it = ccToParam_.erase(it);
        else
            ++it;
    }
    ccToParam_[ccNumber] = paramId;
}

void AudiocityAudioProcessor::clearCcMapping(const int ccNumber)
{
    std::lock_guard<std::mutex> lock(ccMappingMutex_);
    ccToParam_.erase(ccNumber);
}

void AudiocityAudioProcessor::clearCcMappingByParam(const juce::String& paramId)
{
    std::lock_guard<std::mutex> lock(ccMappingMutex_);
    for (auto it = ccToParam_.begin(); it != ccToParam_.end(); )
    {
        if (it->second == paramId)
            it = ccToParam_.erase(it);
        else
            ++it;
    }
}

int AudiocityAudioProcessor::getCcForParam(const juce::String& paramId) const
{
    std::lock_guard<std::mutex> lock(ccMappingMutex_);
    for (const auto& [ccNum, pid] : ccToParam_)
    {
        if (pid == paramId)
            return ccNum;
    }
    return -1;
}

juce::String AudiocityAudioProcessor::getParamForCc(const int ccNumber) const
{
    std::lock_guard<std::mutex> lock(ccMappingMutex_);
    const auto it = ccToParam_.find(ccNumber);
    return it != ccToParam_.end() ? it->second : juce::String{};
}

std::map<int, juce::String> AudiocityAudioProcessor::getAllCcMappings() const
{
    std::lock_guard<std::mutex> lock(ccMappingMutex_);
    return ccToParam_;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudiocityAudioProcessor();
}
