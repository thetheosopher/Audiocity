#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <cmath>

namespace
{
constexpr auto kPatchRoot = "AudiocityPatch";
constexpr auto kSamplePath = "samplePath";
constexpr auto kSampleBrowserRootFolder = "sampleBrowserRootFolder";
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
constexpr auto kFilterMode = "filterMode";
constexpr auto kFilterKeyTracking = "filterKeyTracking";
constexpr auto kFilterVelocityAmount = "filterVelocityAmount";
constexpr auto kFilterLfoRate = "filterLfoRate";
constexpr auto kFilterLfoAmount = "filterLfoAmount";
constexpr auto kFilterLfoShape = "filterLfoShape";
constexpr auto kPlaybackMode = "playbackMode";
constexpr auto kQualityTier = "qualityTier";
constexpr auto kVelocityCurve = "velocityCurve";
constexpr auto kReverbMix = "reverbMix";
constexpr auto kPreloadSamples = "preloadSamples";
constexpr auto kMonoMode = "monoMode";
constexpr auto kLegatoMode = "legatoMode";
constexpr auto kGlideSeconds = "glideSeconds";
constexpr auto kChokeGroup = "chokeGroup";
constexpr auto kSampleWindowStart = "sampleWindowStart";
constexpr auto kSampleWindowEnd = "sampleWindowEnd";
constexpr auto kLoopStart = "loopStart";
constexpr auto kLoopEnd = "loopEnd";
constexpr auto kLoopCrossfadeSamples = "loopCrossfadeSamples";
constexpr auto kFadeInSamples = "fadeInSamples";
constexpr auto kFadeOutSamples = "fadeOutSamples";
constexpr auto kReversePlayback = "reversePlayback";
constexpr auto kCcMappings = "ccMappings";
constexpr auto kCcEntry = "cc";
constexpr auto kCcNumber = "ccNum";
constexpr auto kCcParam = "ccParam";

constexpr auto kParamFilterCutoff = "p_filterCutoff";
constexpr auto kParamFilterRes = "p_filterRes";
constexpr auto kParamFilterEnvAmt = "p_filterEnvAmt";
constexpr auto kParamFilterMode = "p_filterMode";
constexpr auto kParamFilterAttack = "p_filterAttack";
constexpr auto kParamFilterDecay = "p_filterDecay";
constexpr auto kParamFilterSustain = "p_filterSustain";
constexpr auto kParamFilterRelease = "p_filterRelease";
constexpr auto kParamFilterKeytrack = "p_filterKeytrack";
constexpr auto kParamFilterVel = "p_filterVel";
constexpr auto kParamFilterLfoRate = "p_filterLfoRate";
constexpr auto kParamFilterLfoAmount = "p_filterLfoAmount";
constexpr auto kParamFilterLfoShape = "p_filterLfoShape";
constexpr auto kParamAmpAttack = "p_ampAttack";
constexpr auto kParamAmpDecay = "p_ampDecay";
constexpr auto kParamAmpSustain = "p_ampSustain";
constexpr auto kParamAmpRelease = "p_ampRelease";
constexpr auto kParamPlaybackMode = "p_playbackMode";
constexpr auto kParamMonoMode = "p_mono";
constexpr auto kParamLegatoMode = "p_legato";
constexpr auto kParamGlideSeconds = "p_glideSeconds";
constexpr auto kParamChokeGroup = "p_chokeGroup";
constexpr auto kParamFadeIn = "p_fadeIn";
constexpr auto kParamFadeOut = "p_fadeOut";
constexpr auto kParamReversePlayback = "p_reverse";
constexpr auto kParamRootMidiNote = "p_rootMidiNote";
constexpr auto kParamPlaybackStart = "p_playbackStart";
constexpr auto kParamPlaybackEnd = "p_playbackEnd";
constexpr auto kParamLoopStart = "p_loopStart";
constexpr auto kParamLoopEnd = "p_loopEnd";
constexpr auto kParamLoopCrossfade = "p_loopCrossfade";
constexpr auto kParamVelocityCurve = "p_velocityCurve";
constexpr auto kParamQualityTier = "p_qualityTier";
constexpr auto kParamReverbMix = "p_reverbMix";
}

AudiocityAudioProcessor::AudiocityAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts_(*this, nullptr, "AutomatableParams", createParameterLayout())
{
    playerPadAssignments_ = audiocity::plugin::defaultPlayerPadAssignments();

    setFilterSettings(engine_.getFilterSettings());
    setAmpEnvelope(engine_.getAmpEnvelope());
    setFilterEnvelope(engine_.getFilterEnvelope());
    setPlaybackMode(engine_.getPlaybackMode());
    setMonoMode(engine_.getMonoMode());
    setLegatoMode(engine_.getLegatoMode());
    setGlideSeconds(engine_.getGlideSeconds());
    setChokeGroup(engine_.getChokeGroup());
    setFadeSamples(engine_.getFadeInSamples(), engine_.getFadeOutSamples());
    setReversePlayback(engine_.getReversePlayback());
    setRootMidiNote(engine_.getRootMidiNote());
    setSampleWindow(engine_.getSampleWindowStart(), engine_.getSampleWindowEnd());
    setLoopPoints(engine_.getLoopStart(), engine_.getLoopEnd());
    setLoopCrossfadeSamples(engine_.getLoopCrossfadeSamples());
    setQualityTier(engine_.getQualityTier());
    setVelocityCurve(engine_.getVelocityCurve());
    setReverbMix(engine_.getReverbMix());
}

juce::AudioProcessorValueTreeState::ParameterLayout AudiocityAudioProcessor::createParameterLayout()
{
    using Mode = AudiocityAudioProcessor::FilterSettings::Mode;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterCutoff, "Filter Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 0.01f, 0.35f), 1200.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterRes, "Filter Resonance",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterEnvAmt, "Filter Env Amount",
        juce::NormalisableRange<float>(0.0f, 20000.0f, 0.01f, 0.35f), 2400.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(kParamFilterMode, "Filter Mode",
        juce::StringArray{ "LP12", "LP24", "HP12", "HP24", "BP12", "Notch" }, static_cast<int>(Mode::lowPass12)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterAttack, "Filter Attack",
        juce::NormalisableRange<float>(0.0001f, 5.0f, 0.0001f, 0.4f), 0.001f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterDecay, "Filter Decay",
        juce::NormalisableRange<float>(0.0001f, 5.0f, 0.0001f, 0.4f), 0.120f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterSustain, "Filter Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterRelease, "Filter Release",
        juce::NormalisableRange<float>(0.0001f, 5.0f, 0.0001f, 0.4f), 0.100f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterKeytrack, "Filter Key Tracking",
        juce::NormalisableRange<float>(-1.0f, 2.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterVel, "Filter Velocity Amount",
        juce::NormalisableRange<float>(0.0f, 12000.0f, 0.01f, 0.5f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterLfoRate, "Filter LFO Rate",
        juce::NormalisableRange<float>(0.0f, 40.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFilterLfoAmount, "Filter LFO Amount",
        juce::NormalisableRange<float>(-20000.0f, 20000.0f, 0.01f, 0.35f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(kParamFilterLfoShape, "Filter LFO Shape",
        juce::StringArray{ "Sine", "Triangle", "Square", "Saw Up", "Saw Down" },
        static_cast<int>(FilterSettings::LfoShape::sine)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamAmpAttack, "Amp Attack",
        juce::NormalisableRange<float>(0.0001f, 5.0f, 0.0001f, 0.4f), 0.005f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamAmpDecay, "Amp Decay",
        juce::NormalisableRange<float>(0.0001f, 5.0f, 0.0001f, 0.4f), 0.150f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamAmpSustain, "Amp Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.85f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamAmpRelease, "Amp Release",
        juce::NormalisableRange<float>(0.0001f, 5.0f, 0.0001f, 0.4f), 0.150f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(kParamPlaybackMode, "Playback Mode",
        juce::StringArray{ "Gate", "One-shot", "Loop" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterBool>(kParamMonoMode, "Mono", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(kParamLegatoMode, "Legato", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamGlideSeconds, "Glide Seconds",
        juce::NormalisableRange<float>(0.0f, 2.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamChokeGroup, "Choke Group",
        juce::NormalisableRange<float>(0.0f, 16.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFadeIn, "Fade In",
        juce::NormalisableRange<float>(0.0f, 10000.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamFadeOut, "Fade Out",
        juce::NormalisableRange<float>(0.0f, 10000.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(kParamReversePlayback, "Reverse", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamRootMidiNote, "Root MIDI Note",
        juce::NormalisableRange<float>(0.0f, 127.0f, 1.0f), 60.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamPlaybackStart, "Playback Start",
        juce::NormalisableRange<float>(0.0f, 1000000.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamPlaybackEnd, "Playback End",
        juce::NormalisableRange<float>(0.0f, 1000000.0f, 1.0f), 1000000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamLoopStart, "Loop Start",
        juce::NormalisableRange<float>(0.0f, 1000000.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamLoopEnd, "Loop End",
        juce::NormalisableRange<float>(0.0f, 1000000.0f, 1.0f), 1000000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamLoopCrossfade, "Loop Crossfade",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(kParamVelocityCurve, "Velocity Curve",
        juce::StringArray{ "Linear", "Soft", "Hard" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(kParamQualityTier, "Quality",
        juce::StringArray{ "CPU", "Fidelity", "Ultra" }, 1));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(kParamReverbMix, "Reverb Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    return { params.begin(), params.end() };
}

void AudiocityAudioProcessor::updateParameterFromPlainValue(const juce::String& parameterId, const float plainValue) noexcept
{
    if (auto* parameter = apvts_.getParameter(parameterId))
    {
        const auto normalised = parameter->convertTo0to1(plainValue);
        parameter->setValueNotifyingHost(normalised);
    }
}

void AudiocityAudioProcessor::syncEngineFromAutomatableParameters() noexcept
{
    auto amp = engine_.getAmpEnvelope();
    amp.attackSeconds = apvts_.getRawParameterValue(kParamAmpAttack)->load();
    amp.decaySeconds = apvts_.getRawParameterValue(kParamAmpDecay)->load();
    amp.sustainLevel = apvts_.getRawParameterValue(kParamAmpSustain)->load();
    amp.releaseSeconds = apvts_.getRawParameterValue(kParamAmpRelease)->load();
    engine_.setAmpEnvelope(amp);

    auto filter = engine_.getFilterSettings();
    filter.baseCutoffHz = apvts_.getRawParameterValue(kParamFilterCutoff)->load();
    filter.resonance = apvts_.getRawParameterValue(kParamFilterRes)->load();
    filter.envAmountHz = apvts_.getRawParameterValue(kParamFilterEnvAmt)->load();
    filter.mode = static_cast<FilterSettings::Mode>(juce::jlimit(0, 5,
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamFilterMode)->load()))));
    filter.keyTracking = apvts_.getRawParameterValue(kParamFilterKeytrack)->load();
    filter.velocityAmountHz = apvts_.getRawParameterValue(kParamFilterVel)->load();
    filter.lfoRateHz = apvts_.getRawParameterValue(kParamFilterLfoRate)->load();
    filter.lfoAmountHz = apvts_.getRawParameterValue(kParamFilterLfoAmount)->load();
    filter.lfoShape = static_cast<FilterSettings::LfoShape>(juce::jlimit(0, 4,
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamFilterLfoShape)->load()))));
    engine_.setFilterSettings(filter);

    auto filterEnv = engine_.getFilterEnvelope();
    filterEnv.attackSeconds = apvts_.getRawParameterValue(kParamFilterAttack)->load();
    filterEnv.decaySeconds = apvts_.getRawParameterValue(kParamFilterDecay)->load();
    filterEnv.sustainLevel = apvts_.getRawParameterValue(kParamFilterSustain)->load();
    filterEnv.releaseSeconds = apvts_.getRawParameterValue(kParamFilterRelease)->load();
    engine_.setFilterEnvelope(filterEnv);

    engine_.setPlaybackMode(static_cast<PlaybackMode>(juce::jlimit(0, 2,
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamPlaybackMode)->load())))));
    engine_.setMonoMode(apvts_.getRawParameterValue(kParamMonoMode)->load() >= 0.5f);
    engine_.setLegatoMode(apvts_.getRawParameterValue(kParamLegatoMode)->load() >= 0.5f);
    engine_.setGlideSeconds(apvts_.getRawParameterValue(kParamGlideSeconds)->load());
    engine_.setChokeGroup(static_cast<int>(std::round(apvts_.getRawParameterValue(kParamChokeGroup)->load())));
    engine_.setFadeSamples(
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamFadeIn)->load())),
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamFadeOut)->load())));
    engine_.setReversePlayback(apvts_.getRawParameterValue(kParamReversePlayback)->load() >= 0.5f);
    engine_.setRootMidiNote(static_cast<int>(std::round(apvts_.getRawParameterValue(kParamRootMidiNote)->load())));
    engine_.setSampleWindow(
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamPlaybackStart)->load())),
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamPlaybackEnd)->load())));
    engine_.setLoopPoints(
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamLoopStart)->load())),
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamLoopEnd)->load())));
    engine_.setLoopCrossfadeSamples(
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamLoopCrossfade)->load())));

    engine_.setVelocityCurve(static_cast<VelocityCurve>(juce::jlimit(0, 2,
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamVelocityCurve)->load())))));
    engine_.setQualityTier(static_cast<QualityTier>(juce::jlimit(0, 2,
        static_cast<int>(std::round(apvts_.getRawParameterValue(kParamQualityTier)->load())))));
    engine_.setReverbMix(apvts_.getRawParameterValue(kParamReverbMix)->load());
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

    syncEngineFromAutomatableParameters();

    // Extract CC messages and push to FIFO for the editor to consume
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        if (msg.isController())
            pushCcEvent(msg.getControllerNumber(), msg.getControllerValue());
    }

    UiMidiEvent uiEvent{};
    while (popUiMidiEvent(uiEvent))
    {
        if (uiEvent.isNoteOn)
            engine_.noteOn(uiEvent.noteNumber, static_cast<float>(uiEvent.velocity) / 127.0f, 0);
        else
            engine_.noteOff(uiEvent.noteNumber, 0);
    }

    engine_.render(buffer, midiMessages);
}

void AudiocityAudioProcessor::setQualityTier(const QualityTier tier) noexcept
{
    engine_.setQualityTier(tier);
    updateParameterFromPlainValue(kParamQualityTier, static_cast<float>(tier));
}

void AudiocityAudioProcessor::setVelocityCurve(const VelocityCurve curve) noexcept
{
    engine_.setVelocityCurve(curve);
    updateParameterFromPlainValue(kParamVelocityCurve, static_cast<float>(curve));
}

void AudiocityAudioProcessor::setReverbMix(const float mix) noexcept
{
    engine_.setReverbMix(mix);
    updateParameterFromPlainValue(kParamReverbMix, mix);
}

void AudiocityAudioProcessor::setFilterEnvelope(const AdsrSettings& settings) noexcept
{
    engine_.setFilterEnvelope(settings);
    updateParameterFromPlainValue(kParamFilterAttack, settings.attackSeconds);
    updateParameterFromPlainValue(kParamFilterDecay, settings.decaySeconds);
    updateParameterFromPlainValue(kParamFilterSustain, settings.sustainLevel);
    updateParameterFromPlainValue(kParamFilterRelease, settings.releaseSeconds);
}

void AudiocityAudioProcessor::setFilterSettings(const FilterSettings& settings) noexcept
{
    engine_.setFilterSettings(settings);

    const auto applied = engine_.getFilterSettings();
    updateParameterFromPlainValue(kParamFilterCutoff, applied.baseCutoffHz);
    updateParameterFromPlainValue(kParamFilterRes, applied.resonance);
    updateParameterFromPlainValue(kParamFilterEnvAmt, applied.envAmountHz);
    updateParameterFromPlainValue(kParamFilterMode, static_cast<float>(applied.mode));
    updateParameterFromPlainValue(kParamFilterKeytrack, applied.keyTracking);
    updateParameterFromPlainValue(kParamFilterVel, applied.velocityAmountHz);
    updateParameterFromPlainValue(kParamFilterLfoRate, applied.lfoRateHz);
    updateParameterFromPlainValue(kParamFilterLfoAmount, applied.lfoAmountHz);
    updateParameterFromPlainValue(kParamFilterLfoShape, static_cast<float>(applied.lfoShape));
}

juce::AudioProcessorEditor* AudiocityAudioProcessor::createEditor()
{
    return new AudiocityAudioProcessorEditor(*this);
}

void AudiocityAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = juce::ValueTree(kPatchRoot);

    state.setProperty(kSamplePath, engine_.getSamplePath(), nullptr);
    state.setProperty(kSampleBrowserRootFolder, sampleBrowserRootFolderPath_, nullptr);
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
    state.setProperty(kFilterMode, static_cast<int>(filter.mode), nullptr);
    state.setProperty(kFilterKeyTracking, filter.keyTracking, nullptr);
    state.setProperty(kFilterVelocityAmount, filter.velocityAmountHz, nullptr);
    state.setProperty(kFilterLfoRate, filter.lfoRateHz, nullptr);
    state.setProperty(kFilterLfoAmount, filter.lfoAmountHz, nullptr);
    state.setProperty(kFilterLfoShape, static_cast<int>(filter.lfoShape), nullptr);
    state.setProperty(kPlaybackMode,
        getPlaybackMode() == PlaybackMode::oneShot ? 1 : (getPlaybackMode() == PlaybackMode::loop ? 2 : 0),
        nullptr);
    int qualityTierIndex = 1;
    if (getQualityTier() == QualityTier::cpu)
        qualityTierIndex = 0;
    else if (getQualityTier() == QualityTier::ultra)
        qualityTierIndex = 2;

    state.setProperty(kQualityTier, qualityTierIndex, nullptr);
    state.setProperty(kVelocityCurve, static_cast<int>(getVelocityCurve()), nullptr);
    state.setProperty(kReverbMix, getReverbMix(), nullptr);
    state.setProperty(kPreloadSamples, getPreloadSamples(), nullptr);
    state.setProperty(kMonoMode, getMonoMode() ? 1 : 0, nullptr);
    state.setProperty(kLegatoMode, getLegatoMode() ? 1 : 0, nullptr);
    state.setProperty(kGlideSeconds, getGlideSeconds(), nullptr);
    state.setProperty(kChokeGroup, getChokeGroup(), nullptr);
    state.setProperty(kSampleWindowStart, getSampleWindowStart(), nullptr);
    state.setProperty(kSampleWindowEnd, getSampleWindowEnd(), nullptr);
    state.setProperty(kLoopStart, getLoopStart(), nullptr);
    state.setProperty(kLoopEnd, getLoopEnd(), nullptr);
    state.setProperty(kLoopCrossfadeSamples, getLoopCrossfadeSamples(), nullptr);
    state.setProperty(kFadeInSamples, getFadeInSamples(), nullptr);
    state.setProperty(kFadeOutSamples, getFadeOutSamples(), nullptr);
    state.setProperty(kReversePlayback, getReversePlayback() ? 1 : 0, nullptr);

    {
        auto padsNode = juce::ValueTree(audiocity::plugin::kPlayerPads);
        for (int i = 0; i < kPlayerPadCount; ++i)
        {
            const auto assignment = audiocity::plugin::sanitizePlayerPadAssignment(
                playerPadAssignments_[static_cast<std::size_t>(i)]);

            auto entry = juce::ValueTree(audiocity::plugin::kPlayerPad);
            entry.setProperty(audiocity::plugin::kPlayerPadIndex, i, nullptr);
            entry.setProperty(audiocity::plugin::kPlayerPadNote, assignment.noteNumber, nullptr);
            entry.setProperty(audiocity::plugin::kPlayerPadVelocity, assignment.velocity, nullptr);
            padsNode.appendChild(entry, nullptr);
        }
        state.appendChild(padsNode, nullptr);
    }

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

    sampleBrowserRootFolderPath_ = state.getProperty(kSampleBrowserRootFolder, {}).toString();

    setRootMidiNote(static_cast<int>(state.getProperty(kRootMidiNote, engine_.getRootMidiNote())));

    auto amp = engine_.getAmpEnvelope();
    amp.attackSeconds = static_cast<float>(state.getProperty(kAmpAttack, amp.attackSeconds));
    amp.decaySeconds = static_cast<float>(state.getProperty(kAmpDecay, amp.decaySeconds));
    amp.sustainLevel = static_cast<float>(state.getProperty(kAmpSustain, amp.sustainLevel));
    amp.releaseSeconds = static_cast<float>(state.getProperty(kAmpRelease, amp.releaseSeconds));
    setAmpEnvelope(amp);

    auto filterAdsr = engine_.getFilterEnvelope();
    filterAdsr.attackSeconds = static_cast<float>(state.getProperty(kFilterAttack, filterAdsr.attackSeconds));
    filterAdsr.decaySeconds = static_cast<float>(state.getProperty(kFilterDecay, filterAdsr.decaySeconds));
    filterAdsr.sustainLevel = static_cast<float>(state.getProperty(kFilterSustain, filterAdsr.sustainLevel));
    filterAdsr.releaseSeconds = static_cast<float>(state.getProperty(kFilterRelease, filterAdsr.releaseSeconds));
    setFilterEnvelope(filterAdsr);

    auto filter = engine_.getFilterSettings();
    filter.baseCutoffHz = static_cast<float>(state.getProperty(kFilterBaseCutoff, filter.baseCutoffHz));
    filter.envAmountHz = static_cast<float>(state.getProperty(kFilterEnvAmount, filter.envAmountHz));
    filter.resonance = static_cast<float>(state.getProperty(kFilterResonance, filter.resonance));
    filter.mode = static_cast<AudiocityAudioProcessor::FilterSettings::Mode>(
        static_cast<int>(state.getProperty(kFilterMode, static_cast<int>(filter.mode))));
    filter.keyTracking = static_cast<float>(state.getProperty(kFilterKeyTracking, filter.keyTracking));
    filter.velocityAmountHz = static_cast<float>(state.getProperty(kFilterVelocityAmount, filter.velocityAmountHz));
    filter.lfoRateHz = static_cast<float>(state.getProperty(kFilterLfoRate, filter.lfoRateHz));
    filter.lfoAmountHz = static_cast<float>(state.getProperty(kFilterLfoAmount, filter.lfoAmountHz));
    filter.lfoShape = static_cast<FilterSettings::LfoShape>(juce::jlimit(0, 4,
        static_cast<int>(state.getProperty(kFilterLfoShape, static_cast<int>(filter.lfoShape)))));
    setFilterSettings(filter);

    const auto playbackMode = static_cast<int>(state.getProperty(kPlaybackMode, 0));
    setPlaybackMode(playbackMode == 1 ? PlaybackMode::oneShot : (playbackMode == 2 ? PlaybackMode::loop : PlaybackMode::gate));
    const auto qualityTier = static_cast<int>(state.getProperty(kQualityTier, 1));
    setQualityTier(qualityTier == 0 ? QualityTier::cpu : (qualityTier == 2 ? QualityTier::ultra : QualityTier::fidelity));
    setVelocityCurve(static_cast<VelocityCurve>(static_cast<int>(state.getProperty(kVelocityCurve,
        static_cast<int>(getVelocityCurve())))));
    setReverbMix(static_cast<float>(state.getProperty(kReverbMix, getReverbMix())));
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
    setLoopCrossfadeSamples(
        static_cast<int>(state.getProperty(kLoopCrossfadeSamples, getLoopCrossfadeSamples())));
    setFadeSamples(
        static_cast<int>(state.getProperty(kFadeInSamples, getFadeInSamples())),
        static_cast<int>(state.getProperty(kFadeOutSamples, getFadeOutSamples())));
    setReversePlayback(static_cast<int>(state.getProperty(kReversePlayback, getReversePlayback() ? 1 : 0)) == 1);

    playerPadAssignments_ = audiocity::plugin::defaultPlayerPadAssignments();
    {
        const auto padsNode = state.getChildWithName(audiocity::plugin::kPlayerPads);
        for (int i = 0; i < padsNode.getNumChildren(); ++i)
        {
            const auto entry = padsNode.getChild(i);
            if (!entry.hasType(audiocity::plugin::kPlayerPad))
                continue;

            const auto index = static_cast<int>(entry.getProperty(audiocity::plugin::kPlayerPadIndex, -1));
            if (index < 0 || index >= kPlayerPadCount)
                continue;

            const auto note = static_cast<int>(entry.getProperty(audiocity::plugin::kPlayerPadNote,
                playerPadAssignments_[static_cast<std::size_t>(index)].noteNumber));
            const auto velocity = static_cast<int>(entry.getProperty(audiocity::plugin::kPlayerPadVelocity,
                playerPadAssignments_[static_cast<std::size_t>(index)].velocity));

            playerPadAssignments_[static_cast<std::size_t>(index)] =
                audiocity::plugin::sanitizePlayerPadAssignment({ note, velocity });
        }
    }

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
    updateParameterFromPlainValue(kParamPlaybackMode, static_cast<float>(mode));
}

AudiocityAudioProcessor::PlaybackMode AudiocityAudioProcessor::getPlaybackMode() const noexcept
{
    return engine_.getPlaybackMode();
}

void AudiocityAudioProcessor::setMonoMode(const bool enabled) noexcept
{
    engine_.setMonoMode(enabled);
    updateParameterFromPlainValue(kParamMonoMode, enabled ? 1.0f : 0.0f);
}

void AudiocityAudioProcessor::setLegatoMode(const bool enabled) noexcept
{
    engine_.setLegatoMode(enabled);
    updateParameterFromPlainValue(kParamLegatoMode, enabled ? 1.0f : 0.0f);
}

void AudiocityAudioProcessor::setGlideSeconds(const float seconds) noexcept
{
    engine_.setGlideSeconds(seconds);
    updateParameterFromPlainValue(kParamGlideSeconds, engine_.getGlideSeconds());
}

void AudiocityAudioProcessor::setChokeGroup(const int chokeGroup) noexcept
{
    engine_.setChokeGroup(chokeGroup);
    updateParameterFromPlainValue(kParamChokeGroup, static_cast<float>(engine_.getChokeGroup()));
}

void AudiocityAudioProcessor::setFadeSamples(const int fadeInSamples, const int fadeOutSamples) noexcept
{
    engine_.setFadeSamples(fadeInSamples, fadeOutSamples);
    updateParameterFromPlainValue(kParamFadeIn, static_cast<float>(engine_.getFadeInSamples()));
    updateParameterFromPlainValue(kParamFadeOut, static_cast<float>(engine_.getFadeOutSamples()));
}

void AudiocityAudioProcessor::setReversePlayback(const bool enabled) noexcept
{
    engine_.setReversePlayback(enabled);
    updateParameterFromPlainValue(kParamReversePlayback, enabled ? 1.0f : 0.0f);
}

void AudiocityAudioProcessor::setAmpEnvelope(const AdsrSettings& settings) noexcept
{
    engine_.setAmpEnvelope(settings);
    updateParameterFromPlainValue(kParamAmpAttack, settings.attackSeconds);
    updateParameterFromPlainValue(kParamAmpDecay, settings.decaySeconds);
    updateParameterFromPlainValue(kParamAmpSustain, settings.sustainLevel);
    updateParameterFromPlainValue(kParamAmpRelease, settings.releaseSeconds);
}

void AudiocityAudioProcessor::setRootMidiNote(const int rootNote) noexcept
{
    engine_.setRootMidiNote(rootNote);
    updateParameterFromPlainValue(kParamRootMidiNote, static_cast<float>(engine_.getRootMidiNote()));
}

void AudiocityAudioProcessor::setSampleWindow(const int startSample, const int endSample) noexcept
{
    engine_.setSampleWindow(startSample, endSample);
    updateParameterFromPlainValue(kParamPlaybackStart, static_cast<float>(engine_.getSampleWindowStart()));
    updateParameterFromPlainValue(kParamPlaybackEnd, static_cast<float>(engine_.getSampleWindowEnd()));
}

void AudiocityAudioProcessor::setLoopPoints(const int loopStart, const int loopEnd) noexcept
{
    engine_.setLoopPoints(loopStart, loopEnd);
    updateParameterFromPlainValue(kParamLoopStart, static_cast<float>(engine_.getLoopStart()));
    updateParameterFromPlainValue(kParamLoopEnd, static_cast<float>(engine_.getLoopEnd()));
}

void AudiocityAudioProcessor::setLoopCrossfadeSamples(const int crossfadeSamples) noexcept
{
    engine_.setLoopCrossfadeSamples(crossfadeSamples);
    updateParameterFromPlainValue(kParamLoopCrossfade, static_cast<float>(engine_.getLoopCrossfadeSamples()));
}

void AudiocityAudioProcessor::setPlayerPadAssignment(const int padIndex, const int noteNumber, const int velocity) noexcept
{
    if (padIndex < 0 || padIndex >= kPlayerPadCount)
        return;

    playerPadAssignments_[static_cast<std::size_t>(padIndex)] =
        audiocity::plugin::sanitizePlayerPadAssignment({ noteNumber, velocity });
}

AudiocityAudioProcessor::PlayerPadAssignment AudiocityAudioProcessor::getPlayerPadAssignment(const int padIndex) const noexcept
{
    if (padIndex < 0 || padIndex >= kPlayerPadCount)
        return {};

    return playerPadAssignments_[static_cast<std::size_t>(padIndex)];
}

void AudiocityAudioProcessor::enqueueUiMidiNoteOn(const int noteNumber, const int velocity) noexcept
{
    pushUiMidiEvent(noteNumber, velocity, true);
}

void AudiocityAudioProcessor::enqueueUiMidiNoteOff(const int noteNumber) noexcept
{
    pushUiMidiEvent(noteNumber, 0, false);
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

void AudiocityAudioProcessor::pushUiMidiEvent(const int noteNumber, const int velocity, const bool isNoteOn) noexcept
{
    const auto writePos = uiMidiWritePos_.load(std::memory_order_relaxed);
    const auto nextWrite = (writePos + 1) % kUiMidiFifoSize;

    if (nextWrite == uiMidiReadPos_.load(std::memory_order_acquire))
        uiMidiReadPos_.store((uiMidiReadPos_.load(std::memory_order_relaxed) + 1) % kUiMidiFifoSize,
                             std::memory_order_release);

    uiMidiFifo_[static_cast<std::size_t>(writePos)] = {
        juce::jlimit(0, 127, noteNumber),
        juce::jlimit(1, 127, velocity),
        isNoteOn
    };
    uiMidiWritePos_.store(nextWrite, std::memory_order_release);
}

bool AudiocityAudioProcessor::popUiMidiEvent(UiMidiEvent& out) noexcept
{
    const auto readPos = uiMidiReadPos_.load(std::memory_order_relaxed);
    if (readPos == uiMidiWritePos_.load(std::memory_order_acquire))
        return false;

    out = uiMidiFifo_[static_cast<std::size_t>(readPos)];
    uiMidiReadPos_.store((readPos + 1) % kUiMidiFifoSize, std::memory_order_release);
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
