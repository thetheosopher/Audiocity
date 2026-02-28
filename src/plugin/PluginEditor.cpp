#include "PluginEditor.h"

#include "PluginProcessor.h"

#include <algorithm>

void AudiocityAudioProcessorEditor::BrowserPanel::WaveformView::setPeaks(std::vector<float> peaks)
{
    peaks_ = std::move(peaks);
    repaint();
}

void AudiocityAudioProcessorEditor::BrowserPanel::WaveformView::setPlayheadProgress(const float progress)
{
    playheadProgress_ = juce::jlimit(0.0f, 1.0f, progress);
    repaint();
}

void AudiocityAudioProcessorEditor::BrowserPanel::WaveformView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.85f));
    g.setColour(juce::Colours::white.withAlpha(0.2f));
    g.drawRect(getLocalBounds(), 1);

    if (peaks_.empty())
        return;

    const auto bounds = getLocalBounds().toFloat();
    const auto centreY = bounds.getCentreY();
    const auto width = bounds.getWidth();

    g.setColour(juce::Colours::deepskyblue.withAlpha(0.75f));
    for (int i = 0; i < static_cast<int>(peaks_.size()); ++i)
    {
        const auto x = bounds.getX() + width * (static_cast<float>(i) / static_cast<float>(peaks_.size()));
        const auto amplitude = peaks_[static_cast<std::size_t>(i)] * bounds.getHeight() * 0.45f;
        g.drawLine(x, centreY - amplitude, x, centreY + amplitude, 1.0f);
    }

    g.setColour(juce::Colours::yellow.withAlpha(0.9f));
    const auto playheadX = bounds.getX() + bounds.getWidth() * playheadProgress_;
    g.drawLine(playheadX, bounds.getY(), playheadX, bounds.getBottom(), 1.5f);
}

AudiocityAudioProcessorEditor::BrowserPanel::BrowserPanel()
{
    addAndMakeVisible(addFolderButton_);
    addAndMakeVisible(rescanButton_);
    addAndMakeVisible(loadButton_);
    addAndMakeVisible(importSfzButton_);
    addAndMakeVisible(favoriteButton_);
    addAndMakeVisible(previewButton_);

    addAndMakeVisible(searchLabel_);
    addAndMakeVisible(searchEditor_);
    searchEditor_.setMultiLine(false);

    addAndMakeVisible(resultsLabel_);
    addAndMakeVisible(resultCombo_);

    addAndMakeVisible(watchedLabel_);
    addAndMakeVisible(watchedView_);
    watchedView_.setMultiLine(true);
    watchedView_.setReadOnly(true);

    addAndMakeVisible(favoritesLabel_);
    addAndMakeVisible(favoritesView_);
    favoritesView_.setMultiLine(true);
    favoritesView_.setReadOnly(true);

    addAndMakeVisible(recentLabel_);
    addAndMakeVisible(recentView_);
    recentView_.setMultiLine(true);
    recentView_.setReadOnly(true);

    addAndMakeVisible(waveformView_);
    addAndMakeVisible(samplePathLabel_);

    samplePathLabel_.setJustificationType(juce::Justification::centredLeft);
    samplePathLabel_.setText("No sample loaded", juce::dontSendNotification);

    addFolderButton_.onClick = [this]
    {
        if (onAddWatchedFolder)
            onAddWatchedFolder();
    };

    rescanButton_.onClick = [this]
    {
        if (onRescan)
            onRescan();
    };

    loadButton_.onClick = [this]
    {
        if (onLoadSample && selectedPath_.isNotEmpty())
            onLoadSample(selectedPath_);
    };

    importSfzButton_.onClick = [this]
    {
        if (onImportSfz)
            onImportSfz();
    };

    favoriteButton_.onClick = [this]
    {
        if (onToggleFavorite && selectedPath_.isNotEmpty())
            onToggleFavorite(selectedPath_);
    };

    previewButton_.onClick = [this]
    {
        if (onPreviewToggle && selectedPath_.isNotEmpty())
            onPreviewToggle(selectedPath_, !previewPlaying_);
    };

    searchEditor_.onTextChange = [this]
    {
        if (onSearchChanged)
            onSearchChanged(searchEditor_.getText());
    };

    resultCombo_.onChange = [this]
    {
        const auto id = resultCombo_.getSelectedId();
        if (id <= 0 || id > static_cast<int>(results_.size()))
            return;

        selectedPath_ = results_[static_cast<std::size_t>(id - 1)].path;
        if (onSelectionChanged)
            onSelectionChanged(selectedPath_);
    };
}

void AudiocityAudioProcessorEditor::BrowserPanel::setSamplePath(const juce::String& path)
{
    samplePathLabel_.setText(path.isNotEmpty() ? path : "No sample loaded", juce::dontSendNotification);
}

void AudiocityAudioProcessorEditor::BrowserPanel::setWatchedFolders(const juce::StringArray& folders)
{
    watchedView_.setText(folders.joinIntoString("\n"), juce::dontSendNotification);
}

void AudiocityAudioProcessorEditor::BrowserPanel::setSearchText(const juce::String& text)
{
    if (searchEditor_.getText() != text)
        searchEditor_.setText(text, juce::dontSendNotification);
}

void AudiocityAudioProcessorEditor::BrowserPanel::setSearchResults(const std::vector<BrowserIndex::EntrySnapshot>& results)
{
    results_ = results;
    refreshResultCombo();
}

void AudiocityAudioProcessorEditor::BrowserPanel::setFavorites(const std::vector<BrowserIndex::EntrySnapshot>& favorites)
{
    favoritesView_.setText(listToText(favorites), juce::dontSendNotification);
}

void AudiocityAudioProcessorEditor::BrowserPanel::setRecent(const std::vector<BrowserIndex::EntrySnapshot>& recent)
{
    recentView_.setText(listToText(recent), juce::dontSendNotification);
}

void AudiocityAudioProcessorEditor::BrowserPanel::setSelectedPath(const juce::String& path)
{
    selectedPath_ = path;

    int idToSelect = 0;
    for (int i = 0; i < static_cast<int>(results_.size()); ++i)
    {
        if (results_[static_cast<std::size_t>(i)].path == path)
        {
            idToSelect = i + 1;
            break;
        }
    }

    if (idToSelect > 0 && resultCombo_.getSelectedId() != idToSelect)
        resultCombo_.setSelectedId(idToSelect, juce::dontSendNotification);
}

void AudiocityAudioProcessorEditor::BrowserPanel::setWaveformPeaks(std::vector<float> peaks)
{
    waveformView_.setPeaks(std::move(peaks));
}

void AudiocityAudioProcessorEditor::BrowserPanel::setPreviewState(const bool isPlaying, const double durationSeconds)
{
    previewPlaying_ = isPlaying;
    previewDurationSeconds_ = durationSeconds;

    if (isPlaying)
    {
        previewStartedSeconds_ = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        previewButton_.setButtonText("Stop");
        startTimerHz(30);
    }
    else
    {
        previewButton_.setButtonText("Preview");
        stopTimer();
        waveformView_.setPlayheadProgress(0.0f);
    }
}

juce::String AudiocityAudioProcessorEditor::BrowserPanel::getSelectedPath() const
{
    return selectedPath_;
}

void AudiocityAudioProcessorEditor::BrowserPanel::timerCallback()
{
    if (!previewPlaying_ || previewDurationSeconds_ <= 0.0)
    {
        waveformView_.setPlayheadProgress(0.0f);
        return;
    }

    const auto elapsed = (juce::Time::getMillisecondCounterHiRes() / 1000.0) - previewStartedSeconds_;
    waveformView_.setPlayheadProgress(static_cast<float>(juce::jlimit(0.0, 1.0, elapsed / previewDurationSeconds_)));

    if (elapsed >= previewDurationSeconds_)
        setPreviewState(false, previewDurationSeconds_);
}

void AudiocityAudioProcessorEditor::BrowserPanel::refreshResultCombo()
{
    const auto previousSelection = selectedPath_;

    resultCombo_.clear(juce::dontSendNotification);
    int selectedId = 0;

    for (int i = 0; i < static_cast<int>(results_.size()); ++i)
    {
        const auto& entry = results_[static_cast<std::size_t>(i)];
        const auto id = i + 1;

        resultCombo_.addItem(entry.fileName, id);
        if (entry.path == previousSelection)
            selectedId = id;
    }

    if (selectedId == 0 && !results_.empty())
    {
        selectedId = 1;
        selectedPath_ = results_.front().path;
    }

    if (selectedId > 0)
        resultCombo_.setSelectedId(selectedId, juce::dontSendNotification);
}

juce::String AudiocityAudioProcessorEditor::BrowserPanel::listToText(const std::vector<BrowserIndex::EntrySnapshot>& entries)
{
    if (entries.empty())
        return "(none)";

    juce::StringArray lines;
    for (const auto& entry : entries)
        lines.add(entry.fileName);

    return lines.joinIntoString("\n");
}

void AudiocityAudioProcessorEditor::BrowserPanel::resized()
{
    auto area = getLocalBounds().reduced(12);
    auto topRow = area.removeFromTop(30);
    addFolderButton_.setBounds(topRow.removeFromLeft(170));
    topRow.removeFromLeft(6);
    rescanButton_.setBounds(topRow.removeFromLeft(80));
    topRow.removeFromLeft(6);
    importSfzButton_.setBounds(topRow.removeFromLeft(100));
    topRow.removeFromLeft(6);
    loadButton_.setBounds(topRow.removeFromLeft(180));
    topRow.removeFromLeft(6);
    favoriteButton_.setBounds(topRow.removeFromLeft(130));
    topRow.removeFromLeft(6);
    previewButton_.setBounds(topRow.removeFromLeft(90));

    area.removeFromTop(8);

    auto searchRow = area.removeFromTop(24);
    searchLabel_.setBounds(searchRow.removeFromLeft(52));
    searchEditor_.setBounds(searchRow);

    area.removeFromTop(6);

    auto resultRow = area.removeFromTop(24);
    resultsLabel_.setBounds(resultRow.removeFromLeft(52));
    resultCombo_.setBounds(resultRow);

    area.removeFromTop(8);

    waveformView_.setBounds(area.removeFromTop(130));

    area.removeFromTop(6);
    samplePathLabel_.setBounds(area.removeFromTop(24));

    area.removeFromTop(8);
    auto infoArea = area;
    auto colWidth = infoArea.getWidth() / 3;

    auto watchedArea = infoArea.removeFromLeft(colWidth).reduced(2);
    watchedLabel_.setBounds(watchedArea.removeFromTop(20));
    watchedView_.setBounds(watchedArea);

    auto favoritesArea = infoArea.removeFromLeft(colWidth).reduced(2);
    favoritesLabel_.setBounds(favoritesArea.removeFromTop(20));
    favoritesView_.setBounds(favoritesArea);

    auto recentArea = infoArea.reduced(2);
    recentLabel_.setBounds(recentArea.removeFromTop(20));
    recentView_.setBounds(recentArea);
}

void AudiocityAudioProcessorEditor::BrowserPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.9f));
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.drawRect(getLocalBounds().reduced(8), 1);
}

AudiocityAudioProcessorEditor::MappingPanel::MappingPanel()
{
    addAndMakeVisible(rrModeLabel_);
    addAndMakeVisible(rrModeCombo_);
    addAndMakeVisible(playbackModeLabel_);
    addAndMakeVisible(playbackModeCombo_);
    addAndMakeVisible(monoToggle_);
    addAndMakeVisible(legatoToggle_);
    addAndMakeVisible(glideLabel_);
    addAndMakeVisible(glideEditor_);
    addAndMakeVisible(chokeGroupLabel_);
    addAndMakeVisible(chokeGroupEditor_);
    addAndMakeVisible(zoneSummaryLabel_);
    addAndMakeVisible(loopStartLabel_);
    addAndMakeVisible(loopStartEditor_);
    addAndMakeVisible(loopEndLabel_);
    addAndMakeVisible(loopEndEditor_);
    addAndMakeVisible(applyLoopButton_);
    addAndMakeVisible(table_);

    loopStartEditor_.setInputRestrictions(8, "0123456789");
    loopEndEditor_.setInputRestrictions(8, "0123456789");
    glideEditor_.setInputRestrictions(8, "0123456789.");
    chokeGroupEditor_.setInputRestrictions(3, "0123456789");

    monoToggle_.setTooltip("Monophonic playback: one voice at a time.");
    legatoToggle_.setTooltip("When Mono is enabled, keep envelope running between notes.");
    glideEditor_.setTooltip("Portamento time in milliseconds.");
    chokeGroupEditor_.setTooltip("Group index; values > 0 choke previous sounding voice.");

    applyLoopButton_.onClick = [this]
    {
        if (selectedRow_ < 0 || selectedRow_ >= static_cast<int>(zones_.size()))
            return;

        const auto loopStart = loopStartEditor_.getText().getIntValue();
        const auto loopEnd = loopEndEditor_.getText().getIntValue();

        if (onLoopPointsApply)
            onLoopPointsApply(selectedRow_, loopStart, loopEnd);
    };

    rrModeCombo_.addItem("Ordered", 1);
    rrModeCombo_.addItem("Random", 2);
    rrModeCombo_.setSelectedId(1, juce::dontSendNotification);
    rrModeCombo_.onChange = [this]
    {
        if (onRrModeChanged)
            onRrModeChanged(rrModeCombo_.getSelectedId() - 1);
    };

    playbackModeCombo_.addItem("Gate", 1);
    playbackModeCombo_.addItem("One-shot", 2);
    playbackModeCombo_.addItem("Loop", 3);
    playbackModeCombo_.setSelectedId(1, juce::dontSendNotification);
    playbackModeCombo_.onChange = [this]
    {
        if (onPlaybackModeChanged)
            onPlaybackModeChanged(playbackModeCombo_.getSelectedId() - 1);
    };

    monoToggle_.onClick = [this]
    {
        updatePerformanceControlAvailability();

        if (onMonoModeChanged)
            onMonoModeChanged(monoToggle_.getToggleState());
    };

    legatoToggle_.onClick = [this]
    {
        if (onLegatoModeChanged)
            onLegatoModeChanged(legatoToggle_.getToggleState());
    };

    auto pushGlideValue = [this]
    {
        const auto glideMs = juce::jmax(0.0f, static_cast<float>(glideEditor_.getText().getDoubleValue()));
        glideEditor_.setText(juce::String(glideMs, 1), juce::dontSendNotification);

        if (onGlideSecondsChanged)
            onGlideSecondsChanged(glideMs / 1000.0f);
    };

    glideEditor_.onReturnKey = pushGlideValue;
    glideEditor_.onFocusLost = pushGlideValue;

    auto pushChokeGroupValue = [this]
    {
        const auto chokeGroup = juce::jmax(0, chokeGroupEditor_.getText().getIntValue());
        chokeGroupEditor_.setText(juce::String(chokeGroup), juce::dontSendNotification);

        if (onChokeGroupChanged)
            onChokeGroupChanged(chokeGroup);
    };

    chokeGroupEditor_.onReturnKey = pushChokeGroupValue;
    chokeGroupEditor_.onFocusLost = pushChokeGroupValue;

    auto& header = table_.getHeader();
    header.addColumn("Sample", 1, 420);
    header.addColumn("Key", 2, 120);
    header.addColumn("Vel", 3, 120);
    header.addColumn("RR Group", 4, 110);
    header.addColumn("Loop", 5, 120);

    updatePerformanceControlAvailability();
}

void AudiocityAudioProcessorEditor::MappingPanel::setZones(const std::vector<audiocity::engine::sfz::Zone>& zones)
{
    zones_ = zones;
    table_.updateContent();

    if (zones_.empty())
        selectedRow_ = -1;
    else if (selectedRow_ < 0 || selectedRow_ >= static_cast<int>(zones_.size()))
        selectedRow_ = 0;

    if (selectedRow_ >= 0)
        table_.selectRow(selectedRow_);

    updateLoopEditorsFromSelection();
    repaint();
}

void AudiocityAudioProcessorEditor::MappingPanel::setRrMode(const int modeIndex)
{
    const auto id = juce::jlimit(1, 2, modeIndex + 1);
    rrModeCombo_.setSelectedId(id, juce::dontSendNotification);
}

void AudiocityAudioProcessorEditor::MappingPanel::setPlaybackMode(const int modeIndex)
{
    const auto id = juce::jlimit(1, 3, modeIndex + 1);
    playbackModeCombo_.setSelectedId(id, juce::dontSendNotification);
}

void AudiocityAudioProcessorEditor::MappingPanel::setPerformanceControls(
    const bool monoEnabled,
    const bool legatoEnabled,
    const float glideSeconds,
    const int chokeGroup)
{
    monoToggle_.setToggleState(monoEnabled, juce::dontSendNotification);
    legatoToggle_.setToggleState(legatoEnabled, juce::dontSendNotification);
    glideEditor_.setText(juce::String(juce::jmax(0.0f, glideSeconds) * 1000.0f, 1), juce::dontSendNotification);
    chokeGroupEditor_.setText(juce::String(juce::jmax(0, chokeGroup)), juce::dontSendNotification);
    updatePerformanceControlAvailability();
}

void AudiocityAudioProcessorEditor::MappingPanel::updatePerformanceControlAvailability()
{
    const auto monoEnabled = monoToggle_.getToggleState();
    legatoToggle_.setEnabled(monoEnabled);

    if (!monoEnabled && legatoToggle_.getToggleState())
    {
        legatoToggle_.setToggleState(false, juce::dontSendNotification);

        if (onLegatoModeChanged)
            onLegatoModeChanged(false);
    }
}

void AudiocityAudioProcessorEditor::MappingPanel::resized()
{
    auto area = getLocalBounds().reduced(12);
    auto top = area.removeFromTop(24);
    rrModeLabel_.setBounds(top.removeFromLeft(70));
    rrModeCombo_.setBounds(top.removeFromLeft(140));
    top.removeFromLeft(14);
    playbackModeLabel_.setBounds(top.removeFromLeft(70));
    playbackModeCombo_.setBounds(top.removeFromLeft(130));
    top.removeFromLeft(14);
    loopStartLabel_.setBounds(top.removeFromLeft(70));
    loopStartEditor_.setBounds(top.removeFromLeft(70));
    top.removeFromLeft(8);
    loopEndLabel_.setBounds(top.removeFromLeft(60));
    loopEndEditor_.setBounds(top.removeFromLeft(70));
    top.removeFromLeft(8);
    applyLoopButton_.setBounds(top.removeFromLeft(60));

    area.removeFromTop(6);
    auto perfRow = area.removeFromTop(24);
    monoToggle_.setBounds(perfRow.removeFromLeft(72));
    perfRow.removeFromLeft(8);
    legatoToggle_.setBounds(perfRow.removeFromLeft(72));
    perfRow.removeFromLeft(12);
    glideLabel_.setBounds(perfRow.removeFromLeft(74));
    glideEditor_.setBounds(perfRow.removeFromLeft(74));
    perfRow.removeFromLeft(12);
    chokeGroupLabel_.setBounds(perfRow.removeFromLeft(88));
    chokeGroupEditor_.setBounds(perfRow.removeFromLeft(56));

    area.removeFromTop(6);
    zoneSummaryLabel_.setBounds(area.removeFromTop(20));
    area.removeFromTop(6);
    table_.setBounds(area);
}

void AudiocityAudioProcessorEditor::MappingPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.9f));
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.drawRect(getLocalBounds().reduced(8), 1);
}

int AudiocityAudioProcessorEditor::MappingPanel::getNumRows()
{
    return static_cast<int>(zones_.size());
}

void AudiocityAudioProcessorEditor::MappingPanel::paintRowBackground(juce::Graphics& g,
    const int rowNumber,
    int,
    int,
    const bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll(juce::Colours::darkslategrey.withAlpha(0.8f));
    else if (rowNumber % 2 == 0)
        g.fillAll(juce::Colours::black.withAlpha(0.3f));
}

void AudiocityAudioProcessorEditor::MappingPanel::paintCell(juce::Graphics& g,
    const int rowNumber,
    const int columnId,
    const int width,
    const int height,
    bool)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int>(zones_.size()))
        return;

    const auto& zone = zones_[static_cast<std::size_t>(rowNumber)];
    juce::String text;

    switch (columnId)
    {
        case 1: text = zone.sourceSample; break;
        case 2: text = juce::String(zone.lowKey) + "-" + juce::String(zone.highKey); break;
        case 3: text = juce::String(zone.lowVelocity) + "-" + juce::String(zone.highVelocity); break;
        case 4: text = juce::String(zone.rrGroup); break;
        case 5: text = zone.loopMode + " (" + juce::String(zone.loopStart) + "," + juce::String(zone.loopEnd) + ")"; break;
        default: break;
    }

    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredLeft, true);
}

void AudiocityAudioProcessorEditor::MappingPanel::selectedRowsChanged(const int lastRowSelected)
{
    selectedRow_ = lastRowSelected;
    updateLoopEditorsFromSelection();
}

void AudiocityAudioProcessorEditor::MappingPanel::updateLoopEditorsFromSelection()
{
    if (selectedRow_ < 0 || selectedRow_ >= static_cast<int>(zones_.size()))
    {
        loopStartEditor_.setText({}, juce::dontSendNotification);
        loopEndEditor_.setText({}, juce::dontSendNotification);
        zoneSummaryLabel_.setText("No zone selected", juce::dontSendNotification);
        return;
    }

    const auto& zone = zones_[static_cast<std::size_t>(selectedRow_)];
    loopStartEditor_.setText(juce::String(zone.loopStart), juce::dontSendNotification);
    loopEndEditor_.setText(juce::String(zone.loopEnd), juce::dontSendNotification);
    zoneSummaryLabel_.setText(
        "Selected: key " + juce::String(zone.lowKey) + "-" + juce::String(zone.highKey)
            + " | vel " + juce::String(zone.lowVelocity) + "-" + juce::String(zone.highVelocity)
            + " | mode " + zone.loopMode,
        juce::dontSendNotification);
}

AudiocityAudioProcessorEditor::DiagnosticsPanel::DiagnosticsPanel()
{
    addAndMakeVisible(text_);
    text_.setMultiLine(true);
    text_.setReadOnly(true);
    text_.setScrollbarsShown(true);
    text_.setText("No diagnostics.", juce::dontSendNotification);
}

void AudiocityAudioProcessorEditor::DiagnosticsPanel::setDiagnostics(
    const std::vector<audiocity::engine::sfz::Diagnostic>& diagnostics,
    const juce::String& streamingInfo,
    const juce::String& streamingTooltip)
{
    juce::StringArray lines;

    if (streamingInfo.isNotEmpty())
        lines.add("[info] " + streamingInfo);

    text_.setTooltip(streamingTooltip);

    for (const auto& diagnostic : diagnostics)
    {
        const auto severity = diagnostic.severity == audiocity::engine::sfz::DiagnosticSeverity::error ? "error" : "warning";
        lines.add("[" + juce::String(severity) + "] " + diagnostic.filePath + ":" + juce::String(diagnostic.line) + " - " + diagnostic.message);
    }

    if (lines.isEmpty())
        lines.add("No diagnostics.");

    text_.setText(lines.joinIntoString("\n"), juce::dontSendNotification);
}

void AudiocityAudioProcessorEditor::DiagnosticsPanel::resized()
{
    text_.setBounds(getLocalBounds().reduced(12));
}

void AudiocityAudioProcessorEditor::DiagnosticsPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.9f));
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.drawRect(getLocalBounds().reduced(8), 1);
}

AudiocityAudioProcessorEditor::SettingsPanel::SettingsPanel()
{
    addAndMakeVisible(preloadLabel_);
    addAndMakeVisible(preloadEditor_);
    addAndMakeVisible(applyButton_);
    addAndMakeVisible(copyDiagnosticsButton_);
    addAndMakeVisible(splitInfoLabel_);

    preloadEditor_.setInputRestrictions(8, "0123456789");
    preloadEditor_.setText("32768", juce::dontSendNotification);
    preloadEditor_.setTooltip("Number of samples preloaded in memory before streamed region.");
    splitInfoLabel_.setJustificationType(juce::Justification::centredLeft);
    splitInfoLabel_.setText("Loaded split: preload 0 | stream 0", juce::dontSendNotification);

    auto pushValue = [this]
    {
        const auto samples = juce::jmax(256, preloadEditor_.getText().getIntValue());
        preloadEditor_.setText(juce::String(samples), juce::dontSendNotification);

        if (onPreloadSamplesChanged)
            onPreloadSamplesChanged(samples);
    };

    applyButton_.onClick = pushValue;
    preloadEditor_.onReturnKey = pushValue;
    preloadEditor_.onFocusLost = pushValue;

    copyDiagnosticsButton_.onClick = [this]
    {
        if (onCopyDiagnostics)
            onCopyDiagnostics();
    };
}

void AudiocityAudioProcessorEditor::SettingsPanel::setPreloadSamples(const int samples)
{
    preloadEditor_.setText(juce::String(juce::jmax(256, samples)), juce::dontSendNotification);
}

void AudiocityAudioProcessorEditor::SettingsPanel::setPreloadSplit(const int preloadSamples, const int streamSamples)
{
    splitInfoLabel_.setText(
        "Loaded split: preload " + juce::String(juce::jmax(0, preloadSamples))
            + " | stream " + juce::String(juce::jmax(0, streamSamples)),
        juce::dontSendNotification);
}

void AudiocityAudioProcessorEditor::SettingsPanel::resized()
{
    auto area = getLocalBounds().reduced(12);
    auto row = area.removeFromTop(28);
    preloadLabel_.setBounds(row.removeFromLeft(120));
    preloadEditor_.setBounds(row.removeFromLeft(120));
    row.removeFromLeft(8);
    applyButton_.setBounds(row.removeFromLeft(70));
    row.removeFromLeft(8);
    copyDiagnosticsButton_.setBounds(row.removeFromLeft(130));
    area.removeFromTop(8);
    splitInfoLabel_.setBounds(area.removeFromTop(24));
}

void AudiocityAudioProcessorEditor::SettingsPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.9f));
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.drawRect(getLocalBounds().reduced(8), 1);
}

AudiocityAudioProcessorEditor::AudiocityAudioProcessorEditor(AudiocityAudioProcessor& processor)
    : AudioProcessorEditor(&processor), processor_(processor)
{
    setName("Audiocity");
    setSize(1200, 760);

    addAndMakeVisible(tabs_);

    browserPanel_.onLoadSample = [this](const juce::String& path)
    {
        if (path.isNotEmpty() && processor_.loadSampleFromFile(juce::File(path)))
            refreshBrowserPanel();
    };
    browserPanel_.onImportSfz = [this]
    {
        openSfzChooser();
    };
    settingsPanel_.onPreloadSamplesChanged = [this](const int samples)
    {
        processor_.setPreloadSamples(samples);
    };
    settingsPanel_.onCopyDiagnostics = [this]
    {
        juce::SystemClipboard::copyTextToClipboard(buildStreamingDiagnosticsLine(true));
    };
    mappingPanel_.onRrModeChanged = [this](const int modeIndex)
    {
        if (modeIndex == 1)
            processor_.setRoundRobinMode(AudiocityAudioProcessor::RoundRobinMode::random);
        else
            processor_.setRoundRobinMode(AudiocityAudioProcessor::RoundRobinMode::ordered);
    };
    mappingPanel_.onPlaybackModeChanged = [this](const int modeIndex)
    {
        if (modeIndex == 1)
            processor_.setPlaybackMode(AudiocityAudioProcessor::PlaybackMode::oneShot);
        else if (modeIndex == 2)
            processor_.setPlaybackMode(AudiocityAudioProcessor::PlaybackMode::loop);
        else
            processor_.setPlaybackMode(AudiocityAudioProcessor::PlaybackMode::gate);
    };
    mappingPanel_.onMonoModeChanged = [this](const bool enabled)
    {
        processor_.setMonoMode(enabled);
    };
    mappingPanel_.onLegatoModeChanged = [this](const bool enabled)
    {
        processor_.setLegatoMode(enabled);
    };
    mappingPanel_.onGlideSecondsChanged = [this](const float seconds)
    {
        processor_.setGlideSeconds(seconds);
    };
    mappingPanel_.onChokeGroupChanged = [this](const int chokeGroup)
    {
        processor_.setChokeGroup(chokeGroup);
    };
    mappingPanel_.onLoopPointsApply = [this](const int row, const int loopStart, const int loopEnd)
    {
        if (processor_.updateImportedZoneLoopPoints(row, loopStart, loopEnd))
            refreshImportedSfzViews();
    };
    browserPanel_.onAddWatchedFolder = [this]
    {
        openWatchedFolderChooser();
    };
    browserPanel_.onRescan = [this]
    {
        processor_.browserIndex().rescan();
    };
    browserPanel_.onSearchChanged = [this](const juce::String& text)
    {
        processor_.browserIndex().setSearchText(text);
    };
    browserPanel_.onSelectionChanged = [this](const juce::String& path)
    {
        browserPanel_.setWaveformPeaks(processor_.browserIndex().getPeaks(path));
    };
    browserPanel_.onToggleFavorite = [this](const juce::String& path)
    {
        processor_.browserIndex().toggleFavorite(path);
        refreshBrowserPanel();
    };
    browserPanel_.onPreviewToggle = [this](const juce::String& path, const bool start)
    {
        if (start)
        {
            if (processor_.startPreviewFromPath(path))
            {
                const auto results = processor_.browserIndex().getSearchResults();
                const auto found = std::find_if(results.begin(), results.end(), [&](const BrowserIndex::EntrySnapshot& item)
                {
                    return item.path == path;
                });

                const auto duration = found != results.end() ? found->durationSeconds : 0.0;
                browserPanel_.setPreviewState(true, duration);
                refreshBrowserPanel();
            }
        }
        else
        {
            processor_.stopPreview();
            browserPanel_.setPreviewState(false, 0.0);
        }
    };

    auto safeThis = juce::Component::SafePointer<AudiocityAudioProcessorEditor>(this);
    processor_.browserIndex().setOnUpdated([safeThis]
    {
        if (safeThis != nullptr)
            safeThis->refreshBrowserPanel();
    });

    refreshBrowserPanel();
    refreshImportedSfzViews();
    refreshSettingsPanel();
    refreshDiagnosticsTabTitle();

    tabs_.addTab("Browser", juce::Colours::transparentBlack, &browserPanel_, false);
    tabs_.addTab("Mapping", juce::Colours::transparentBlack, &mappingPanel_, false);
    tabs_.addTab("Editor", juce::Colours::transparentBlack, &editorPanel_, false);
    tabs_.addTab("Settings", juce::Colours::transparentBlack, &settingsPanel_, false);
    tabs_.addTab("Diagnostics", juce::Colours::transparentBlack, &diagnosticsPanel_, false);
}

AudiocityAudioProcessorEditor::~AudiocityAudioProcessorEditor()
{
    processor_.browserIndex().setOnUpdated({});
}

void AudiocityAudioProcessorEditor::resized()
{
    tabs_.setBounds(getLocalBounds().reduced(8));
}

void AudiocityAudioProcessorEditor::openSampleChooser()
{
    fileChooser_ = std::make_unique<juce::FileChooser>("Load sample", juce::File{}, "*.wav;*.aiff;*.aif");

    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser_->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        const auto selected = chooser.getResult();
        if (selected == juce::File{})
            return;

        if (processor_.loadSampleFromFile(selected))
            refreshBrowserPanel();
    });
}

void AudiocityAudioProcessorEditor::openSfzChooser()
{
    fileChooser_ = std::make_unique<juce::FileChooser>("Import SFZ", juce::File{}, "*.sfz");

    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser_->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        const auto selected = chooser.getResult();
        if (selected == juce::File{})
            return;

        processor_.importSfzFile(selected);
        refreshBrowserPanel();
        refreshImportedSfzViews();
    });
}

void AudiocityAudioProcessorEditor::openWatchedFolderChooser()
{
    fileChooser_ = std::make_unique<juce::FileChooser>("Add watched folder", juce::File{}, "*");

    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
    fileChooser_->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        const auto selected = chooser.getResult();
        if (!selected.isDirectory())
            return;

        processor_.browserIndex().addWatchedFolder(selected);
    });
}

void AudiocityAudioProcessorEditor::refreshImportedSfzViews()
{
    mappingPanel_.setZones(processor_.getImportedZones());

    const auto rrMode = processor_.getRoundRobinMode() == AudiocityAudioProcessor::RoundRobinMode::random ? 1 : 0;
    mappingPanel_.setRrMode(rrMode);

    int playbackMode = 0;
    if (processor_.getPlaybackMode() == AudiocityAudioProcessor::PlaybackMode::oneShot)
        playbackMode = 1;
    else if (processor_.getPlaybackMode() == AudiocityAudioProcessor::PlaybackMode::loop)
        playbackMode = 2;
    mappingPanel_.setPlaybackMode(playbackMode);
    mappingPanel_.setPerformanceControls(
        processor_.getMonoMode(),
        processor_.getLegatoMode(),
        processor_.getGlideSeconds(),
        processor_.getChokeGroup());

    diagnosticsPanel_.setDiagnostics(
        processor_.getImportDiagnostics(),
        buildStreamingDiagnosticsLine(false),
        buildStreamingDiagnosticsTooltip());
    refreshDiagnosticsTabTitle();
}

void AudiocityAudioProcessorEditor::refreshDiagnosticsTabTitle()
{
    const auto& diagnostics = processor_.getImportDiagnostics();

    int errorCount = 0;
    int warningCount = 0;

    for (const auto& diagnostic : diagnostics)
    {
        if (diagnostic.severity == audiocity::engine::sfz::DiagnosticSeverity::error)
            ++errorCount;
        else
            ++warningCount;
    }

    auto label = juce::String("Diagnostics");
    if (errorCount > 0 || warningCount > 0)
        label << " (" << errorCount << "/" << warningCount << ")";

    tabs_.getTabbedButtonBar().setTabName(4, label);
}

void AudiocityAudioProcessorEditor::refreshBrowserPanel()
{
    browserPanel_.setSamplePath(processor_.getLoadedSamplePath());

    auto& index = processor_.browserIndex();
    browserPanel_.setWatchedFolders(index.getWatchedFolders());
    browserPanel_.setSearchText(index.getSearchText());

    const auto results = index.getSearchResults();
    browserPanel_.setSearchResults(results);
    browserPanel_.setFavorites(index.getFavorites());
    browserPanel_.setRecent(index.getRecent(12));

    auto selectedPath = browserPanel_.getSelectedPath();
    if (selectedPath.isEmpty() && !results.empty())
        selectedPath = results.front().path;

    browserPanel_.setSelectedPath(selectedPath);
    browserPanel_.setWaveformPeaks(index.getPeaks(selectedPath));
    refreshSettingsPanel();
    diagnosticsPanel_.setDiagnostics(
        processor_.getImportDiagnostics(),
        buildStreamingDiagnosticsLine(false),
        buildStreamingDiagnosticsTooltip());
}

void AudiocityAudioProcessorEditor::refreshSettingsPanel()
{
    settingsPanel_.setPreloadSamples(processor_.getPreloadSamples());
    settingsPanel_.setPreloadSplit(processor_.getLoadedPreloadSamples(), processor_.getLoadedStreamSamples());
}

juce::String AudiocityAudioProcessorEditor::buildStreamingDiagnosticsLine(const bool includeFullSamplePath) const
{
    int errorCount = 0;
    int warningCount = 0;

    for (const auto& diagnostic : processor_.getImportDiagnostics())
    {
        if (diagnostic.severity == audiocity::engine::sfz::DiagnosticSeverity::error)
            ++errorCount;
        else
            ++warningCount;
    }

    const auto samplePath = processor_.getLoadedSamplePath();
    juce::String sampleToken("(none)");

    if (samplePath.isNotEmpty())
    {
        sampleToken = includeFullSamplePath ? samplePath : juce::File(samplePath).getFileName();

        if (!includeFullSamplePath)
        {
            constexpr int maxSampleTokenLength = 40;
            if (sampleToken.length() > maxSampleTokenLength)
                sampleToken = sampleToken.substring(0, maxSampleTokenLength - 1) + "…";
        }
    }

    return "Streaming readiness: rebuilds=" + juce::String(processor_.getSegmentRebuildCount())
        + " | preload=" + juce::String(processor_.getLoadedPreloadSamples())
        + " | stream=" + juce::String(processor_.getLoadedStreamSamples())
        + " | sfz(e/w)=" + juce::String(errorCount) + "/" + juce::String(warningCount)
        + " | sample=" + sampleToken;
}

juce::String AudiocityAudioProcessorEditor::buildStreamingDiagnosticsTooltip() const
{
    return "[info] " + buildStreamingDiagnosticsLine(true);
}
