#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <vector>

#include "../engine/sfz/SfzModel.h"

class AudiocityAudioProcessor;

class AudiocityAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AudiocityAudioProcessorEditor(AudiocityAudioProcessor& processor);
    ~AudiocityAudioProcessorEditor() override = default;

    void resized() override;

private:
    class BrowserPanel final : public juce::Component
    {
    public:
        BrowserPanel();

        std::function<void()> onLoadSample;
        std::function<void()> onImportSfz;
        void setSamplePath(const juce::String& path);
        void resized() override;
        void paint(juce::Graphics& g) override;

    private:
        juce::TextButton loadButton_{ "Load Sample (WAV/AIFF)" };
        juce::TextButton importSfzButton_{ "Import SFZ" };
        juce::Label samplePathLabel_;
    };

    class MappingPanel final : public juce::Component,
                               private juce::TableListBoxModel
    {
    public:
        MappingPanel();
        void setZones(const std::vector<audiocity::engine::sfz::Zone>& zones);
        void resized() override;
        void paint(juce::Graphics& g) override;

    private:
        int getNumRows() override;
        void paintRowBackground(juce::Graphics& g,
            int rowNumber,
            int width,
            int height,
            bool rowIsSelected) override;
        void paintCell(juce::Graphics& g,
            int rowNumber,
            int columnId,
            int width,
            int height,
            bool rowIsSelected) override;

        juce::TableListBox table_{ "zones", this };
        std::vector<audiocity::engine::sfz::Zone> zones_;
    };

    class DiagnosticsPanel final : public juce::Component
    {
    public:
        DiagnosticsPanel();
        void setDiagnostics(const std::vector<audiocity::engine::sfz::Diagnostic>& diagnostics);
        void resized() override;
        void paint(juce::Graphics& g) override;

    private:
        juce::TextEditor text_;
    };

    class PlaceholderPanel final : public juce::Component
    {
    public:
        explicit PlaceholderPanel(juce::String title) : title_(std::move(title)) {}

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colours::black.withAlpha(0.9f));
            g.setColour(juce::Colours::white.withAlpha(0.35f));
            g.drawRect(getLocalBounds().reduced(8), 1);
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.setFont(18.0f);
            g.drawText(title_ + " panel (stub)", getLocalBounds(), juce::Justification::centred);
        }

    private:
        juce::String title_;
    };

    AudiocityAudioProcessor& processor_;
    juce::TabbedComponent tabs_{ juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<juce::FileChooser> fileChooser_;

    BrowserPanel browserPanel_{};
    MappingPanel mappingPanel_{};
    PlaceholderPanel editorPanel_{ "Editor" };
    PlaceholderPanel settingsPanel_{ "Settings" };
    DiagnosticsPanel diagnosticsPanel_{};

    void openSampleChooser();
    void openSfzChooser();
    void refreshImportedSfzViews();
    void refreshBrowserPanel();
    void refreshDiagnosticsTabTitle();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudiocityAudioProcessorEditor)
};
