#include "PluginEditor.h"

#include "PluginProcessor.h"

AudiocityAudioProcessorEditor::BrowserPanel::BrowserPanel()
{
    addAndMakeVisible(loadButton_);
    addAndMakeVisible(importSfzButton_);
    addAndMakeVisible(samplePathLabel_);

    samplePathLabel_.setJustificationType(juce::Justification::centredLeft);
    samplePathLabel_.setText("No sample loaded", juce::dontSendNotification);

    loadButton_.onClick = [this]
    {
        if (onLoadSample)
            onLoadSample();
    };

    importSfzButton_.onClick = [this]
    {
        if (onImportSfz)
            onImportSfz();
    };
}

void AudiocityAudioProcessorEditor::BrowserPanel::setSamplePath(const juce::String& path)
{
    samplePathLabel_.setText(path.isNotEmpty() ? path : "No sample loaded", juce::dontSendNotification);
}

void AudiocityAudioProcessorEditor::BrowserPanel::resized()
{
    auto area = getLocalBounds().reduced(12);
    auto buttonRow = area.removeFromTop(32);
    loadButton_.setBounds(buttonRow.removeFromLeft(240));
    buttonRow.removeFromLeft(8);
    importSfzButton_.setBounds(buttonRow.removeFromLeft(140));
    area.removeFromTop(8);
    samplePathLabel_.setBounds(area.removeFromTop(24));
}

void AudiocityAudioProcessorEditor::BrowserPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.9f));
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.drawRect(getLocalBounds().reduced(8), 1);
}

AudiocityAudioProcessorEditor::MappingPanel::MappingPanel()
{
    addAndMakeVisible(table_);

    auto& header = table_.getHeader();
    header.addColumn("Sample", 1, 420);
    header.addColumn("Key", 2, 120);
    header.addColumn("Vel", 3, 120);
    header.addColumn("RR Group", 4, 110);
    header.addColumn("Loop", 5, 120);
}

void AudiocityAudioProcessorEditor::MappingPanel::setZones(const std::vector<audiocity::engine::sfz::Zone>& zones)
{
    zones_ = zones;
    table_.updateContent();
    repaint();
}

void AudiocityAudioProcessorEditor::MappingPanel::resized()
{
    table_.setBounds(getLocalBounds().reduced(12));
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

AudiocityAudioProcessorEditor::DiagnosticsPanel::DiagnosticsPanel()
{
    addAndMakeVisible(text_);
    text_.setMultiLine(true);
    text_.setReadOnly(true);
    text_.setScrollbarsShown(true);
    text_.setText("No diagnostics.", juce::dontSendNotification);
}

void AudiocityAudioProcessorEditor::DiagnosticsPanel::setDiagnostics(const std::vector<audiocity::engine::sfz::Diagnostic>& diagnostics)
{
    if (diagnostics.empty())
    {
        text_.setText("No diagnostics.", juce::dontSendNotification);
        return;
    }

    juce::StringArray lines;
    for (const auto& diagnostic : diagnostics)
    {
        const auto severity = diagnostic.severity == audiocity::engine::sfz::DiagnosticSeverity::error ? "error" : "warning";
        lines.add("[" + juce::String(severity) + "] " + diagnostic.filePath + ":" + juce::String(diagnostic.line) + " - " + diagnostic.message);
    }

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

AudiocityAudioProcessorEditor::AudiocityAudioProcessorEditor(AudiocityAudioProcessor& processor)
    : AudioProcessorEditor(&processor), processor_(processor)
{
    setName("Audiocity");
    setSize(1200, 760);

    addAndMakeVisible(tabs_);

    browserPanel_.onLoadSample = [this]
    {
        openSampleChooser();
    };
    browserPanel_.onImportSfz = [this]
    {
        openSfzChooser();
    };
    refreshBrowserPanel();
    refreshImportedSfzViews();
    refreshDiagnosticsTabTitle();

    tabs_.addTab("Browser", juce::Colours::transparentBlack, &browserPanel_, false);
    tabs_.addTab("Mapping", juce::Colours::transparentBlack, &mappingPanel_, false);
    tabs_.addTab("Editor", juce::Colours::transparentBlack, &editorPanel_, false);
    tabs_.addTab("Settings", juce::Colours::transparentBlack, &settingsPanel_, false);
    tabs_.addTab("Diagnostics", juce::Colours::transparentBlack, &diagnosticsPanel_, false);
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

void AudiocityAudioProcessorEditor::refreshImportedSfzViews()
{
    mappingPanel_.setZones(processor_.getImportedZones());
    diagnosticsPanel_.setDiagnostics(processor_.getImportDiagnostics());
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
}
