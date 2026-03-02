#include "PluginEditor.h"

#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <thread>

#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
class TabTextLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawTabButton(juce::TabBarButton& button, juce::Graphics& g,
                       bool isMouseOver, bool isMouseDown) override
    {
        auto area = button.getActiveArea().toFloat().reduced(0.5f, 0.5f);
        const bool isFront = button.isFrontTab();

        auto fill = isFront ? juce::Colour(0xff2f3550) : juce::Colour(0xff1f2438);
        if (!isFront && isMouseOver)
            fill = fill.brighter(0.08f);
        if (!isFront && isMouseDown)
            fill = fill.brighter(0.14f);

        auto border = isFront ? juce::Colour(0xff6d89c4) : juce::Colour(0xff454a62);

        g.setColour(fill);
        g.fillRoundedRectangle(area, 4.0f);

        g.setColour(border);
        g.drawRoundedRectangle(area, 4.0f, isFront ? 1.5f : 1.0f);

        if (isFront)
        {
            g.setColour(juce::Colour(0xff61d9ff));
            g.fillRect(area.removeFromBottom(2.0f));
        }

        drawTabButtonText(button, g, isMouseOver, isMouseDown);
    }

    void drawTabButtonText(juce::TabBarButton& button, juce::Graphics& g,
                           bool isMouseOver, bool isMouseDown) override
    {
        juce::ignoreUnused(isMouseOver, isMouseDown);

        auto colour = juce::Colours::white.withAlpha(button.isFrontTab() ? 0.98f : 0.70f);
        if (auto* bar = button.findParentComponentOfClass<juce::TabbedButtonBar>())
        {
            const auto colourId = button.isFrontTab()
                ? juce::TabbedButtonBar::frontTextColourId
                : juce::TabbedButtonBar::tabTextColourId;

            if (bar->isColourSpecified(colourId))
                colour = bar->findColour(colourId);
            else if (isColourSpecified(colourId))
                colour = findColour(colourId);
        }

        auto font = juce::Font(juce::FontOptions(14.0f));
        if (button.isFrontTab())
            font = font.boldened();

        g.setColour(colour);
        g.setFont(font);
        g.drawText(button.getButtonText(), button.getTextArea(), juce::Justification::centred, false);
    }
};

TabTextLookAndFeel tabTextLookAndFeel;

int computeWaveformPeakResolution(const int waveformWidthPixels)
{
    const auto width = juce::jmax(1, waveformWidthPixels);
    return juce::jlimit(2048, 32768, juce::jmax(4096, width * 8));
}

juce::String formatMidiNoteName(const int midiNote)
{
    static constexpr const char* kNoteNames[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    const auto clamped = juce::jlimit(0, 127, midiNote);
    const auto name = kNoteNames[clamped % 12];
    const auto octave = (clamped / 12) - 2;
    return juce::String(name) + juce::String(octave) + " (" + juce::String(clamped) + ")";
}

int filterModeToComboId(const audiocity::engine::EngineCore::FilterSettings::Mode mode)
{
    using Mode = audiocity::engine::EngineCore::FilterSettings::Mode;
    switch (mode)
    {
        case Mode::lowPass12: return 1;
        case Mode::lowPass24: return 2;
        case Mode::highPass12: return 3;
        case Mode::highPass24: return 4;
        case Mode::bandPass12: return 5;
        case Mode::notch12: return 6;
        default: return 1;
    }
}

audiocity::engine::EngineCore::FilterSettings::Mode comboIdToFilterMode(const int comboId)
{
    using Mode = audiocity::engine::EngineCore::FilterSettings::Mode;
    switch (comboId)
    {
        case 2: return Mode::lowPass24;
        case 3: return Mode::highPass12;
        case 4: return Mode::highPass24;
        case 5: return Mode::bandPass12;
        case 6: return Mode::notch12;
        case 1:
        default:
            return Mode::lowPass12;
    }
}

int lfoShapeToComboId(const audiocity::engine::EngineCore::FilterSettings::LfoShape shape)
{
    using LfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape;
    switch (shape)
    {
        case LfoShape::sine: return 1;
        case LfoShape::triangle: return 2;
        case LfoShape::square: return 3;
        case LfoShape::sawUp: return 4;
        case LfoShape::sawDown: return 5;
        default: return 1;
    }
}

audiocity::engine::EngineCore::FilterSettings::LfoShape comboIdToLfoShape(const int comboId)
{
    using LfoShape = audiocity::engine::EngineCore::FilterSettings::LfoShape;
    switch (comboId)
    {
        case 2: return LfoShape::triangle;
        case 3: return LfoShape::square;
        case 4: return LfoShape::sawUp;
        case 5: return LfoShape::sawDown;
        case 1:
        default:
            return LfoShape::sine;
    }
}

class PadAssignmentDialogContent final : public juce::Component
{
public:
    PadAssignmentDialogContent(const int initialNote,
                               const int initialVelocity,
                               std::function<void(int, int)> onAccepted)
        : onAccepted_(std::move(onAccepted))
    {
        addAndMakeVisible(noteLabel_);
        addAndMakeVisible(noteSlider_);
        addAndMakeVisible(velocityLabel_);
        addAndMakeVisible(velocitySlider_);
        addAndMakeVisible(cancelButton_);
        addAndMakeVisible(applyButton_);

        noteLabel_.setText("MIDI Note", juce::dontSendNotification);
        noteLabel_.setJustificationType(juce::Justification::centredLeft);
        velocityLabel_.setText("Velocity", juce::dontSendNotification);
        velocityLabel_.setJustificationType(juce::Justification::centredLeft);

        noteSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
        noteSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 22);
        noteSlider_.setRange(0, 127, 1);
        noteSlider_.setValue(juce::jlimit(0, 127, initialNote), juce::dontSendNotification);

        velocitySlider_.setSliderStyle(juce::Slider::LinearHorizontal);
        velocitySlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 22);
        velocitySlider_.setRange(1, 127, 1);
        velocitySlider_.setValue(juce::jlimit(1, 127, initialVelocity), juce::dontSendNotification);

        cancelButton_.setButtonText("Cancel");
        applyButton_.setButtonText("Apply");

        cancelButton_.onClick = [this]
        {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState(0);
        };

        applyButton_.onClick = [this]
        {
            if (onAccepted_)
            {
                onAccepted_(static_cast<int>(noteSlider_.getValue()),
                            static_cast<int>(velocitySlider_.getValue()));
            }
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState(1);
        };
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        noteLabel_.setBounds(area.removeFromTop(18));
        noteSlider_.setBounds(area.removeFromTop(28));
        area.removeFromTop(8);
        velocityLabel_.setBounds(area.removeFromTop(18));
        velocitySlider_.setBounds(area.removeFromTop(28));
        area.removeFromTop(12);

        auto buttons = area.removeFromTop(26);
        applyButton_.setBounds(buttons.removeFromRight(78));
        buttons.removeFromRight(6);
        cancelButton_.setBounds(buttons.removeFromRight(78));
    }

private:
    juce::Label noteLabel_;
    juce::Slider noteSlider_;
    juce::Label velocityLabel_;
    juce::Slider velocitySlider_;
    juce::TextButton cancelButton_;
    juce::TextButton applyButton_;
    std::function<void(int, int)> onAccepted_;
};

struct SamplePreviewData
{
    std::vector<float> peaks;
    juce::String metadataLine;
    juce::String loopFormatBadge;
    juce::String loopMetadataLine;
};

auto buildPreviewAndMetadata(const juce::File& file) -> SamplePreviewData
{
    auto getMetadataValueCaseInsensitive = [](const juce::StringPairArray& metadata,
                                              const juce::String& key) -> juce::String
    {
        const auto keys = metadata.getAllKeys();
        for (int i = 0; i < keys.size(); ++i)
        {
            if (keys[i].equalsIgnoreCase(key))
                return metadata.getValue(keys[i], {});
        }
        return {};
    };

    auto parseEmbeddedRootNote = [&getMetadataValueCaseInsensitive](const juce::StringPairArray& metadata) -> int
    {
        static const juce::StringArray candidateKeys
        {
            "MidiUnityNote",
            "RootNote",
            "ACID Root Note",
            "AcidRootNote",
            "acidrootnote"
        };

        for (const auto& key : candidateKeys)
        {
            const auto value = getMetadataValueCaseInsensitive(metadata, key).trim();
            if (value.isEmpty())
                continue;

            if (value.containsOnly("-0123456789"))
                return juce::jlimit(0, 127, value.getIntValue());
        }

        return -1;
    };

    auto parseLoopPoint = [&getMetadataValueCaseInsensitive](const juce::StringPairArray& metadata,
                                                              const juce::String& key) -> int
    {
        const auto value = getMetadataValueCaseInsensitive(metadata, key).trim();
        if (value.isEmpty() || !value.containsOnly("-0123456789"))
            return -1;

        return value.getIntValue();
    };

    auto parseTempo = [&getMetadataValueCaseInsensitive](const juce::StringPairArray& metadata) -> double
    {
        static const juce::StringArray candidateKeys
        {
            "Tempo",
            "BPM",
            "ACID Tempo",
            "AcidTempo",
            "acidtempo"
        };

        for (const auto& key : candidateKeys)
        {
            const auto value = getMetadataValueCaseInsensitive(metadata, key).trim();
            if (value.isEmpty())
                continue;

            const auto bpm = value.getDoubleValue();
            if (bpm > 0.0)
                return bpm;
        }

        return 0.0;
    };

    auto detectLoopFormatBadge = [&parseEmbeddedRootNote, &parseLoopPoint](
        const juce::File& sampleFile, const juce::StringPairArray& metadata) -> juce::String
    {
        const auto hasRootNote = parseEmbeddedRootNote(metadata) >= 0;
        const auto loopStart = parseLoopPoint(metadata, "Loop0Start");
        const auto loopEnd = parseLoopPoint(metadata, "Loop0End");
        const auto hasLoop = loopStart >= 0 && loopEnd > loopStart;
        if (!(hasRootNote && hasLoop))
            return {};

        const auto ext = sampleFile.getFileExtension().toLowerCase();
        if (ext == ".wav")
            return "Acidized";
        if (ext == ".aif" || ext == ".aiff")
            return "Apple Loop";
        return {};
    };

    auto buildData = [&detectLoopFormatBadge, &parseEmbeddedRootNote, &parseLoopPoint, &parseTempo](
        const juce::File& sampleFile) -> SamplePreviewData
    {
        constexpr int kPeakCount = 256;
        SamplePreviewData out;
        out.peaks.assign(static_cast<std::size_t>(kPeakCount), 0.0f);

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(sampleFile));
        if (reader == nullptr || reader->lengthInSamples <= 0)
        {
            out.metadataLine = "SR: --  Ch: --  Bit Depth: --  Duration: --  Samples: --";
            return out;
        }

        out.loopFormatBadge = detectLoopFormatBadge(sampleFile, reader->metadataValues);

        if (out.loopFormatBadge.isNotEmpty())
        {
            const auto root = parseEmbeddedRootNote(reader->metadataValues);
            const auto loopStart = parseLoopPoint(reader->metadataValues, "Loop0Start");
            const auto loopEnd = parseLoopPoint(reader->metadataValues, "Loop0End");
            const auto tempoBpm = parseTempo(reader->metadataValues);

            juce::StringArray parts;
            if (root >= 0)
                parts.add("Root: " + formatMidiNoteName(root));
            if (loopStart >= 0 && loopEnd > loopStart)
                parts.add("Loop: " + juce::String(loopStart) + "-" + juce::String(loopEnd));
            if (tempoBpm > 0.0)
                parts.add("Tempo: " + juce::String(tempoBpm, 2) + " BPM");

            out.loopMetadataLine = parts.joinIntoString("  |  ");
        }

        const auto totalSamples = static_cast<int64_t>(reader->lengthInSamples);
        const auto sampleRateHz = static_cast<int>(std::round(reader->sampleRate));
        const auto totalMs = static_cast<int64_t>(std::round((static_cast<double>(totalSamples) * 1000.0) / reader->sampleRate));
        const auto minutes = static_cast<int>(totalMs / 60000);
        const auto seconds = static_cast<int>((totalMs % 60000) / 1000);
        const auto millis = static_cast<int>(totalMs % 1000);
        const auto durationText = juce::String::formatted("%02d:%02d.%03d", minutes, seconds, millis);
        out.metadataLine = "SR: " + juce::String(sampleRateHz)
            + " Hz  Ch: " + juce::String(static_cast<int>(reader->numChannels))
            + "  Bit Depth: " + juce::String(reader->bitsPerSample)
            + "  Duration: " + durationText
            + "  Samples: " + juce::String(static_cast<juce::int64>(totalSamples));

        juce::AudioBuffer<float> scratchBuffer(1, 4096);
        for (int i = 0; i < kPeakCount; ++i)
        {
            const auto start = (static_cast<int64_t>(i) * totalSamples) / kPeakCount;
            const auto end = juce::jmax(start + 1, (static_cast<int64_t>(i + 1) * totalSamples) / kPeakCount);

            auto maxAbs = 0.0f;
            int64_t position = start;
            while (position < end)
            {
                const auto chunk = static_cast<int>(juce::jmin<int64_t>(scratchBuffer.getNumSamples(), end - position));
                if (!reader->read(&scratchBuffer, 0, chunk, position, true, true))
                    break;

                const auto* samples = scratchBuffer.getReadPointer(0);
                for (int s = 0; s < chunk; ++s)
                    maxAbs = juce::jmax(maxAbs, std::abs(samples[s]));

                position += chunk;
            }

            out.peaks[static_cast<std::size_t>(i)] = juce::jlimit(0.0f, 1.0f, maxAbs);
        }

        return out;
    };

    return buildData(file);
}
}

void AudiocityAudioProcessorEditor::GeneratedWaveformView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff121629));
    g.setColour(juce::Colours::white.withAlpha(0.22f));
    g.drawRect(getLocalBounds(), 1);

    if (waveform_.empty())
    {
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.drawText("Generate a waveform", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto bounds = getLocalBounds().toFloat().reduced(8.0f, 8.0f);
    const auto midY = bounds.getCentreY();

    g.setColour(juce::Colours::white.withAlpha(0.07f));
    constexpr int kGridDivisionsX = 8;
    constexpr int kGridDivisionsY = 4;
    for (int i = 1; i < kGridDivisionsX; ++i)
    {
        const auto x = bounds.getX() + bounds.getWidth() * (static_cast<float>(i) / static_cast<float>(kGridDivisionsX));
        g.drawLine(x, bounds.getY(), x, bounds.getBottom(), 1.0f);
    }
    for (int i = 1; i < kGridDivisionsY; ++i)
    {
        const auto y = bounds.getY() + bounds.getHeight() * (static_cast<float>(i) / static_cast<float>(kGridDivisionsY));
        g.drawLine(bounds.getX(), y, bounds.getRight(), y, 1.0f);
    }

    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.drawLine(bounds.getX(), midY, bounds.getRight(), midY, 1.0f);

    juce::Path path;
    const auto count = static_cast<int>(waveform_.size());
    for (int i = 0; i < count; ++i)
    {
        const auto x = bounds.getX() + (static_cast<float>(i) / static_cast<float>(juce::jmax(1, count - 1))) * bounds.getWidth();
        const auto y = midY - juce::jlimit(-1.0f, 1.0f, waveform_[static_cast<std::size_t>(i)]) * (bounds.getHeight() * 0.45f);
        if (i == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }

    g.setColour(juce::Colour(0xff59ddff));
    g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

int AudiocityAudioProcessorEditor::GeneratedWaveformView::sampleIndexFromX(const float x) const
{
    if (waveform_.empty())
        return 0;

    const auto bounds = getLocalBounds().toFloat().reduced(8.0f, 8.0f);
    const auto norm = juce::jlimit(0.0f, 1.0f, (x - bounds.getX()) / juce::jmax(1.0f, bounds.getWidth()));
    return juce::jlimit(0, static_cast<int>(waveform_.size()) - 1,
        static_cast<int>(std::round(norm * static_cast<float>(juce::jmax(1, static_cast<int>(waveform_.size()) - 1)))));
}

float AudiocityAudioProcessorEditor::GeneratedWaveformView::sampleValueFromY(const float y) const
{
    const auto bounds = getLocalBounds().toFloat().reduced(8.0f, 8.0f);
    const auto norm = juce::jlimit(0.0f, 1.0f, (y - bounds.getY()) / juce::jmax(1.0f, bounds.getHeight()));
    return juce::jlimit(-1.0f, 1.0f, 1.0f - norm * 2.0f);
}

void AudiocityAudioProcessorEditor::GeneratedWaveformView::applyPoint(const juce::Point<float>& position, const bool interpolateFromLast)
{
    if (waveform_.empty())
        return;

    const auto currentIndex = sampleIndexFromX(position.x);
    const auto currentValue = sampleValueFromY(position.y);

    if (interpolateFromLast && lastDrawIndex_ >= 0)
    {
        const auto startIndex = juce::jmin(lastDrawIndex_, currentIndex);
        const auto endIndex = juce::jmax(lastDrawIndex_, currentIndex);
        const auto distance = juce::jmax(1, endIndex - startIndex);

        for (int i = startIndex; i <= endIndex; ++i)
        {
            const auto t = static_cast<float>(i - startIndex) / static_cast<float>(distance);
            const auto value = (lastDrawIndex_ <= currentIndex)
                ? juce::jmap(t, lastDrawValue_, currentValue)
                : juce::jmap(t, currentValue, lastDrawValue_);
            waveform_[static_cast<std::size_t>(i)] = value;
        }
    }
    else
    {
        waveform_[static_cast<std::size_t>(currentIndex)] = currentValue;
    }

    lastDrawIndex_ = currentIndex;
    lastDrawValue_ = currentValue;

    if (onWaveChanged_)
        onWaveChanged_(waveform_);

    repaint();
}

void AudiocityAudioProcessorEditor::GeneratedWaveformView::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isLeftButtonDown())
        return;

    drawing_ = true;
    lastDrawIndex_ = -1;
    applyPoint(event.position, false);
}

void AudiocityAudioProcessorEditor::GeneratedWaveformView::mouseDrag(const juce::MouseEvent& event)
{
    if (!drawing_)
        return;

    applyPoint(event.position, true);
}

void AudiocityAudioProcessorEditor::GeneratedWaveformView::mouseUp(const juce::MouseEvent&)
{
    drawing_ = false;
    lastDrawIndex_ = -1;
}

void AudiocityAudioProcessorEditor::AmpEnvelopeGraph::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff202234));
    g.setColour(juce::Colour(0xff3a3a52));
    g.drawRect(getLocalBounds(), 1);

    const auto area = getLocalBounds().toFloat().reduced(10.0f, 8.0f);
    if (area.getWidth() <= 1.0f || area.getHeight() <= 1.0f)
        return;

    g.setColour(juce::Colours::white.withAlpha(0.08f));
    for (int i = 1; i < 4; ++i)
    {
        const auto y = area.getY() + area.getHeight() * (static_cast<float>(i) / 4.0f);
        g.drawLine(area.getX(), y, area.getRight(), y, 1.0f);
    }

    const auto attack = juce::jmax(0.001f, attackMs_);
    const auto decay = juce::jmax(0.001f, decayMs_);
    const auto release = juce::jmax(0.001f, releaseMs_);
    const auto hold = juce::jmax(5.0f, (attack + decay + release) * 0.3f);
    const auto total = attack + decay + hold + release;

    auto xFromTime = [&](float t)
    {
        return area.getX() + (t / total) * area.getWidth();
    };
    auto yFromLevel = [&](float level)
    {
        return area.getBottom() - juce::jlimit(0.0f, 1.0f, level) * area.getHeight();
    };

    const auto x0 = area.getX();
    const auto y0 = yFromLevel(0.0f);
    const auto x1 = xFromTime(attack);
    const auto y1 = yFromLevel(1.0f);
    const auto x2 = xFromTime(attack + decay);
    const auto y2 = yFromLevel(sustain_);
    const auto x3 = xFromTime(attack + decay + hold);
    const auto y3 = y2;
    const auto x4 = area.getRight();
    const auto y4 = yFromLevel(0.0f);

    juce::Path fill;
    fill.startNewSubPath(x0, y0);
    fill.lineTo(x1, y1);
    fill.lineTo(x2, y2);
    fill.lineTo(x3, y3);
    fill.lineTo(x4, y4);
    fill.lineTo(x4, area.getBottom());
    fill.lineTo(x0, area.getBottom());
    fill.closeSubPath();

    g.setColour(juce::Colour(0xff4fc3f7).withAlpha(0.18f));
    g.fillPath(fill);

    juce::Path env;
    env.startNewSubPath(x0, y0);
    env.lineTo(x1, y1);
    env.lineTo(x2, y2);
    env.lineTo(x3, y3);
    env.lineTo(x4, y4);

    g.setColour(juce::Colour(0xff61d9ff));
    g.strokePath(env, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(juce::Colour(0xff9ea5bf));
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    const auto labelY = juce::jmin(static_cast<int>(area.getBottom()) + 1, getHeight() - 12);
    const auto drawMarker = [&](const juce::String& text, const float centerX)
    {
        const int markerWidth = 14;
        const auto x = juce::jlimit(0, juce::jmax(0, getWidth() - markerWidth),
            static_cast<int>(std::round(centerX)) - markerWidth / 2);
        g.drawText(text, x, labelY, markerWidth, 10, juce::Justification::centred);
    };

    drawMarker("A", (x0 + x1) * 0.5f);
    drawMarker("D", (x1 + x2) * 0.5f);
    drawMarker("S", (x2 + x3) * 0.5f);
    drawMarker("R", (x3 + x4) * 0.5f);
}

void AudiocityAudioProcessorEditor::FilterResponseGraph::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff202234));
    g.setColour(juce::Colour(0xff3a3a52));
    g.drawRect(getLocalBounds(), 1);

    const auto area = getLocalBounds().toFloat().reduced(10.0f, 8.0f);
    if (area.getWidth() <= 2.0f || area.getHeight() <= 2.0f)
        return;

    g.setColour(juce::Colours::white.withAlpha(0.08f));
    for (int i = 1; i < 4; ++i)
    {
        const auto y = area.getY() + area.getHeight() * (static_cast<float>(i) / 4.0f);
        g.drawLine(area.getX(), y, area.getRight(), y, 1.0f);
    }

    constexpr std::array<float, 4> kGuidesHz{ 100.0f, 1000.0f, 5000.0f, 10000.0f };
    for (const auto hz : kGuidesHz)
    {
        const auto norm = std::log10(hz / 20.0f) / std::log10(1000.0f);
        const auto x = area.getX() + juce::jlimit(0.0f, 1.0f, norm) * area.getWidth();
        g.drawLine(x, area.getY(), x, area.getBottom(), 1.0f);
    }

    const auto q = 0.6f + resonance_ * 12.0f;
    auto magnitudeAtHz = [&](const float hz) -> float
    {
        const auto ratio = juce::jmax(0.00001f, hz / juce::jmax(20.0f, cutoffHz_));
        const auto ratioSquared = ratio * ratio;
        const auto oneMinusRatioSquared = 1.0f - ratioSquared;

        switch (modeId_)
        {
            case 2: // LP24
            {
                const auto lp12 = 1.0f / std::sqrt(1.0f + ratioSquared);
                return lp12 * lp12;
            }
            case 1: // LP12
                return 1.0f / std::sqrt(1.0f + ratioSquared);
            case 4: // HP24
            {
                const auto hp12 = ratio / std::sqrt(1.0f + ratioSquared);
                return hp12 * hp12;
            }
            case 3: // HP12
                return ratio / std::sqrt(1.0f + ratioSquared);
            case 5: // BP12
            {
                const auto denominator = std::sqrt(oneMinusRatioSquared * oneMinusRatioSquared
                                                   + (ratio / q) * (ratio / q));
                return juce::jmax(0.0f, (ratio / q) / juce::jmax(0.00001f, denominator));
            }
            case 6: // Notch
            {
                const auto numerator = std::abs(oneMinusRatioSquared);
                const auto denominator = std::sqrt(oneMinusRatioSquared * oneMinusRatioSquared
                                                   + (ratio / q) * (ratio / q));
                return juce::jmax(0.0f, numerator / juce::jmax(0.00001f, denominator));
            }
            default:
                return 1.0f;
        }
    };

    auto xToHz = [&](const float x) -> float
    {
        const auto norm = juce::jlimit(0.0f, 1.0f, (x - area.getX()) / juce::jmax(1.0f, area.getWidth()));
        return 20.0f * std::pow(1000.0f, norm);
    };

    auto gainToY = [&](const float magnitude) -> float
    {
        constexpr float kMinDb = -30.0f;
        constexpr float kMaxDb = 12.0f;
        const auto db = juce::jlimit(kMinDb, kMaxDb, 20.0f * std::log10(juce::jmax(0.00001f, magnitude)));
        const auto norm = (db - kMinDb) / (kMaxDb - kMinDb);
        return area.getBottom() - norm * area.getHeight();
    };

    juce::Path curve;
    for (int px = 0; px < static_cast<int>(std::round(area.getWidth())); ++px)
    {
        const auto x = area.getX() + static_cast<float>(px);
        const auto hz = xToHz(x);
        const auto y = gainToY(magnitudeAtHz(hz));
        if (px == 0)
            curve.startNewSubPath(x, y);
        else
            curve.lineTo(x, y);
    }

    g.setColour(juce::Colour(0xff61d9ff));
    g.strokePath(curve, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto cutoffNorm = std::log10(cutoffHz_ / 20.0f) / std::log10(1000.0f);
    const auto cutoffX = area.getX() + juce::jlimit(0.0f, 1.0f, cutoffNorm) * area.getWidth();
    g.setColour(juce::Colour(0xfff5b76b).withAlpha(0.9f));
    g.drawLine(cutoffX, area.getY(), cutoffX, area.getBottom(), 1.0f);

    const auto envCutoffHz = juce::jlimit(20.0f, 20000.0f, cutoffHz_ + envAmountHz_);
    const auto envNorm = std::log10(envCutoffHz / 20.0f) / std::log10(1000.0f);
    const auto envX = area.getX() + juce::jlimit(0.0f, 1.0f, envNorm) * area.getWidth();
    g.setColour(juce::Colour(0xff93d984).withAlpha(0.75f));
    g.drawLine(envX, area.getY(), envX, area.getBottom(), 1.0f);
}

void AudiocityAudioProcessorEditor::StereoPeakMeter::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll(juce::Colour(0xff1f2336));
    g.setColour(juce::Colour(0xff3a3f55));
    g.drawRect(bounds, 1);

    auto area = bounds.reduced(8, 6);
    if (area.getWidth() < 80 || area.getHeight() < 20)
        return;

    constexpr int labelWidth = 14;
    constexpr int rowGap = 5;
    const int rowHeight = juce::jmax(8, (area.getHeight() - rowGap) / 2);

    auto leftRow = area.removeFromTop(rowHeight);
    area.removeFromTop(rowGap);
    auto rightRow = area.removeFromTop(rowHeight);

    auto drawRow = [&](juce::Rectangle<int> row, const juce::String& label, const float level)
    {
        g.setColour(juce::Colour(0xffaab2cf));
        g.setFont(juce::Font(juce::FontOptions(10.0f)).boldened());
        g.drawText(label, row.removeFromLeft(labelWidth), juce::Justification::centredLeft);

        auto meterArea = row.reduced(1, 1);
        g.setColour(juce::Colour(0xff111523));
        g.fillRoundedRectangle(meterArea.toFloat(), 2.0f);
        g.setColour(juce::Colour(0xff434a66));
        g.drawRoundedRectangle(meterArea.toFloat(), 2.0f, 1.0f);

        const int segmentCount = 20;
        for (int i = 1; i < segmentCount; ++i)
        {
            const auto x = meterArea.getX() + static_cast<int>(std::round((meterArea.getWidth() * i) / static_cast<float>(segmentCount)));
            g.setColour(juce::Colours::white.withAlpha(i % 5 == 0 ? 0.20f : 0.10f));
            g.drawVerticalLine(x, static_cast<float>(meterArea.getY() + 1), static_cast<float>(meterArea.getBottom() - 1));
        }

        const auto clampedLevel = juce::jlimit(0.0f, 1.0f, level);
        auto fillArea = meterArea;
        fillArea.setWidth(static_cast<int>(std::round(static_cast<float>(meterArea.getWidth()) * clampedLevel)));

        if (fillArea.getWidth() > 0)
        {
            auto gradient = juce::ColourGradient(juce::Colour(0xff74d56a),
                                                 static_cast<float>(meterArea.getX()),
                                                 static_cast<float>(meterArea.getCentreY()),
                                                 juce::Colour(0xffef6b73),
                                                 static_cast<float>(meterArea.getRight()),
                                                 static_cast<float>(meterArea.getCentreY()),
                                                 false);
            gradient.addColour(0.72, juce::Colour(0xfff2c66a));
            g.setGradientFill(gradient);
            g.fillRoundedRectangle(fillArea.toFloat(), 2.0f);

            const int peakX = juce::jlimit(meterArea.getX(), meterArea.getRight() - 1, fillArea.getRight() - 1);
            g.setColour(juce::Colours::white.withAlpha(0.7f));
            g.drawVerticalLine(peakX, static_cast<float>(meterArea.getY()), static_cast<float>(meterArea.getBottom()));
        }
    };

    drawRow(leftRow, "L", leftLevel_);
    drawRow(rightRow, "R", rightLevel_);
}

// ─── WaveformView ──────────────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::WaveformView::setState(
    const int totalSamples, std::vector<std::vector<MinMax>> waveformByChannel,
    const int playbackStart, const int playbackEnd,
    const int loopStart, const int loopEnd,
    juce::String loopFormatBadge)
{
    totalSamples_ = juce::jmax(0, totalSamples);
    waveformByChannel_ = std::move(waveformByChannel);
    playbackStart_ = playbackStart;
    playbackEnd_ = playbackEnd;
    loopStart_ = loopStart;
    loopEnd_ = loopEnd;
    loopFormatBadge_ = std::move(loopFormatBadge);

    if (viewSampleCount_ <= 0)
        viewSampleCount_ = juce::jmax(1, totalSamples_);

    clampView();
    repaint();
}

void AudiocityAudioProcessorEditor::WaveformView::setVoicePlaybackPositions(const VoicePlaybackPositions& positions)
{
    voicePlaybackPositions_ = positions;
    repaint();
}

void AudiocityAudioProcessorEditor::WaveformView::resetView()
{
    viewStartSample_ = 0;
    viewSampleCount_ = juce::jmax(1, totalSamples_);
    clampView();
    repaint();
}

void AudiocityAudioProcessorEditor::WaveformView::setViewRange(const int viewStartSample, const int viewSampleCount)
{
    viewStartSample_ = juce::jmax(0, viewStartSample);
    viewSampleCount_ = juce::jmax(0, viewSampleCount);

    if (viewSampleCount_ <= 0)
        viewSampleCount_ = juce::jmax(1, totalSamples_);

    clampView();
    repaint();
}

int AudiocityAudioProcessorEditor::WaveformView::sampleFromX(const float x) const noexcept
{
    const auto width = juce::jmax(1.0f, static_cast<float>(getWidth()));
    const auto norm = juce::jlimit(0.0f, 1.0f, x / width);
    return viewStartSample_ + static_cast<int>(norm * static_cast<float>(juce::jmax(1, viewSampleCount_ - 1)));
}

float AudiocityAudioProcessorEditor::WaveformView::xFromSample(const int sample) const noexcept
{
    const auto width = juce::jmax(1.0f, static_cast<float>(getWidth()));
    const auto local = juce::jlimit(0, juce::jmax(1, viewSampleCount_ - 1), sample - viewStartSample_);
    return (static_cast<float>(local) / static_cast<float>(juce::jmax(1, viewSampleCount_ - 1))) * width;
}

void AudiocityAudioProcessorEditor::WaveformView::clampView()
{
    const auto total = juce::jmax(1, totalSamples_);
    const auto minWindow = juce::jmin(32, total);
    viewSampleCount_ = juce::jlimit(minWindow, total, viewSampleCount_);
    viewStartSample_ = juce::jlimit(0, juce::jmax(0, total - viewSampleCount_), viewStartSample_);
}

void AudiocityAudioProcessorEditor::WaveformView::zoomAround(const float anchorX, const float zoomFactor)
{
    if (totalSamples_ <= 0)
        return;

    const auto anchorSample = sampleFromX(anchorX);
    const auto minWindow = juce::jmin(32, juce::jmax(1, totalSamples_));
    const auto nextCount = juce::jlimit(minWindow, juce::jmax(1, totalSamples_),
        static_cast<int>(std::round(static_cast<double>(viewSampleCount_) * zoomFactor)));
    const auto anchorNorm = juce::jlimit(0.0, 1.0,
        static_cast<double>(anchorX / juce::jmax(1.0f, static_cast<float>(getWidth()))));

    viewSampleCount_ = nextCount;
    viewStartSample_ = anchorSample - static_cast<int>(anchorNorm * static_cast<double>(viewSampleCount_));
    clampView();
    repaint();
}

void AudiocityAudioProcessorEditor::WaveformView::panByPixels(const float deltaX)
{
    if (totalSamples_ <= 0)
        return;

    const auto samplesPerPixel = static_cast<double>(juce::jmax(1, viewSampleCount_))
        / static_cast<double>(juce::jmax(1, getWidth()));
    viewStartSample_ -= static_cast<int>(std::round(static_cast<double>(deltaX) * samplesPerPixel));
    clampView();
    repaint();
}

void AudiocityAudioProcessorEditor::WaveformView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.85f));
    g.setColour(juce::Colours::white.withAlpha(0.25f));
    g.drawRect(getLocalBounds(), 1);

    if (totalSamples_ <= 0 || waveformByChannel_.empty())
    {
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.drawText("Load a sample (WAV/AIFF)", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto bounds = getLocalBounds().toFloat();
    const auto channelCount = juce::jmax(1, static_cast<int>(waveformByChannel_.size()));
    const auto channelHeight = bounds.getHeight() / static_cast<float>(channelCount);

    // ── Playback region markers ──
    const auto pbX1 = xFromSample(playbackStart_);
    const auto pbX2 = xFromSample(playbackEnd_);

    // ── Loop region markers ──
    const auto lx1 = xFromSample(loopStart_);
    const auto lx2 = xFromSample(loopEnd_);
    const auto loopLeft = juce::jmin(lx1, lx2);
    const auto loopRight = juce::jmax(lx1, lx2);

    const auto handleVisualX = [bounds](const float x)
    {
        return juce::jlimit(bounds.getX(), bounds.getRight(), x);
    };
    const auto pbHX1 = handleVisualX(pbX1);
    const auto pbHX2 = handleVisualX(pbX2);
    const auto lpHX1 = handleVisualX(loopLeft);
    const auto lpHX2 = handleVisualX(loopRight);

    for (int channel = 0; channel < channelCount; ++channel)
    {
        const auto laneY = bounds.getY() + static_cast<float>(channel) * channelHeight;
        const auto lane = juce::Rectangle<float>(bounds.getX(), laneY, bounds.getWidth(), channelHeight).reduced(0.0f, 1.0f);
        const auto centerY = lane.getCentreY();

        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRect(lane);

        g.setColour(juce::Colours::black.withAlpha(0.45f));
        if (pbX1 > lane.getX())
            g.fillRect(juce::Rectangle<float>(lane.getX(), lane.getY(), pbX1 - lane.getX(), lane.getHeight()));
        if (pbX2 < lane.getRight())
            g.fillRect(juce::Rectangle<float>(pbX2, lane.getY(), lane.getRight() - pbX2, lane.getHeight()));

        g.setColour(juce::Colours::orange.withAlpha(0.16f));
        g.fillRect(juce::Rectangle<float>(loopLeft, lane.getY(), juce::jmax(1.0f, loopRight - loopLeft), lane.getHeight()));

        if (channelCount > 1)
        {
            g.setColour(juce::Colours::grey.withAlpha(0.25f));
            g.drawHorizontalLine(static_cast<int>(lane.getBottom()), lane.getX(), lane.getRight());
        }

        g.setColour(juce::Colour(0xff9a9aad));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        juce::String channelName;
        if (channelCount == 2)
            channelName = (channel == 0 ? "Left" : "Right");
        else
            channelName = "Channel " + juce::String(channel + 1);
        g.drawText(channelName, lane.withTrimmedLeft(6.0f).withTrimmedTop(2.0f).removeFromTop(12.0f),
            juce::Justification::centredLeft, false);

        const auto& waveform = waveformByChannel_[static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(waveformByChannel_.size()) - 1, channel))];
        const auto bucketCount = static_cast<int>(waveform.size());
        if (bucketCount > 0)
        {
            const auto samplesPerPixel = static_cast<float>(juce::jmax(1, viewSampleCount_))
                / juce::jmax(1.0f, lane.getWidth());
            const auto drawDiscreteSamples = samplesPerPixel <= 2.5f && viewSampleCount_ <= 4096;
            const auto yFromSample = [&](const float sampleValue)
            {
                const auto clamped = juce::jlimit(-1.0f, 1.0f, sampleValue);
                return centerY - clamped * lane.getHeight() * 0.45f;
            };

            if (drawDiscreteSamples)
            {
                const auto firstSample = juce::jlimit(0, juce::jmax(0, totalSamples_ - 1), viewStartSample_);
                const auto lastSample = juce::jlimit(firstSample, juce::jmax(0, totalSamples_ - 1),
                    viewStartSample_ + juce::jmax(1, viewSampleCount_) - 1);

                juce::Path topPath;
                juce::Path bottomPath;
                auto started = false;

                for (int sample = firstSample; sample <= lastSample; ++sample)
                {
                    const auto x = xFromSample(sample);
                    const auto norm = static_cast<float>(sample)
                        / static_cast<float>(juce::jmax(1, totalSamples_ - 1));
                    const auto bucketIndex = juce::jlimit(0, bucketCount - 1,
                        static_cast<int>(std::round(norm * static_cast<float>(juce::jmax(0, bucketCount - 1)))));

                    const auto& range = waveform[static_cast<std::size_t>(bucketIndex)];
                    const auto topSample = displayMode_ == DisplayMode::symmetricEnvelope
                        ? juce::jmax(std::abs(range.max), std::abs(range.min))
                        : range.max;
                    const auto bottomSample = displayMode_ == DisplayMode::symmetricEnvelope
                        ? -juce::jmax(std::abs(range.max), std::abs(range.min))
                        : range.min;
                    const auto topY = yFromSample(topSample);
                    const auto bottomY = yFromSample(bottomSample);

                    if (!started)
                    {
                        topPath.startNewSubPath(x, topY);
                        bottomPath.startNewSubPath(x, bottomY);
                        started = true;
                    }
                    else
                    {
                        topPath.lineTo(x, topY);
                        bottomPath.lineTo(x, bottomY);
                    }

                    g.setColour(juce::Colours::deepskyblue.withAlpha(0.25f));
                    g.drawLine(x, centerY, x, topY, 1.0f);
                    g.drawLine(x, centerY, x, bottomY, 1.0f);
                }

                g.setColour(juce::Colours::deepskyblue.withAlpha(0.95f));
                g.strokePath(topPath, juce::PathStrokeType(1.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
                g.setColour(juce::Colours::deepskyblue.withAlpha(0.8f));
                g.strokePath(bottomPath, juce::PathStrokeType(1.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
            }
            else
            {
                const auto pixelCount = juce::jmax(1, static_cast<int>(std::round(lane.getWidth())));
                std::vector<float> topYs(static_cast<std::size_t>(pixelCount + 1), centerY);
                std::vector<float> bottomYs(static_cast<std::size_t>(pixelCount + 1), centerY);
                juce::Path topPath;
                juce::Path bottomPath;

                for (int px = 0; px <= pixelCount; ++px)
                {
                    const auto t = static_cast<float>(px) / static_cast<float>(pixelCount);
                    const auto x = lane.getX() + t * lane.getWidth();
                    const auto sample = sampleFromX(x);

                    const auto norm = static_cast<float>(juce::jlimit(0, juce::jmax(1, totalSamples_ - 1), sample))
                        / static_cast<float>(juce::jmax(1, totalSamples_ - 1));
                    const auto peakPos = norm * static_cast<float>(juce::jmax(0, bucketCount - 1));
                    const auto i0 = juce::jlimit(0, bucketCount - 1, static_cast<int>(std::floor(peakPos)));
                    const auto i1 = juce::jlimit(0, bucketCount - 1, i0 + 1);
                    const auto frac = peakPos - static_cast<float>(i0);

                    const auto& r0 = waveform[static_cast<std::size_t>(i0)];
                    const auto& r1 = waveform[static_cast<std::size_t>(i1)];
                    const auto interpMax = r0.max + (r1.max - r0.max) * frac;
                    const auto interpMin = r0.min + (r1.min - r0.min) * frac;
                    const auto topSample = displayMode_ == DisplayMode::symmetricEnvelope
                        ? juce::jmax(std::abs(interpMax), std::abs(interpMin))
                        : interpMax;
                    const auto bottomSample = displayMode_ == DisplayMode::symmetricEnvelope
                        ? -juce::jmax(std::abs(interpMax), std::abs(interpMin))
                        : interpMin;

                    const auto topY = yFromSample(topSample);
                    const auto bottomY = yFromSample(bottomSample);
                    topYs[static_cast<std::size_t>(px)] = topY;
                    bottomYs[static_cast<std::size_t>(px)] = bottomY;

                    if (px == 0)
                    {
                        topPath.startNewSubPath(x, topY);
                        bottomPath.startNewSubPath(x, bottomY);
                    }
                    else
                    {
                        topPath.lineTo(x, topY);
                        bottomPath.lineTo(x, bottomY);
                    }
                }

                juce::Path fillPath;
                fillPath.startNewSubPath(lane.getX(), topYs.front());
                for (int px = 1; px <= pixelCount; ++px)
                {
                    const auto t = static_cast<float>(px) / static_cast<float>(pixelCount);
                    const auto x = lane.getX() + t * lane.getWidth();
                    fillPath.lineTo(x, topYs[static_cast<std::size_t>(px)]);
                }

                for (int px = pixelCount; px >= 0; --px)
                {
                    const auto t = static_cast<float>(px) / static_cast<float>(pixelCount);
                    const auto x = lane.getX() + t * lane.getWidth();
                    fillPath.lineTo(x, bottomYs[static_cast<std::size_t>(px)]);
                }
                fillPath.closeSubPath();

                juce::ColourGradient fillGradient(
                    juce::Colours::deepskyblue.withAlpha(0.32f), lane.getCentreX(), lane.getY(),
                    juce::Colours::deepskyblue.withAlpha(0.08f), lane.getCentreX(), lane.getBottom(),
                    false);
                fillGradient.addColour(0.5, juce::Colours::deepskyblue.withAlpha(0.20f));
                g.setGradientFill(fillGradient);
                g.fillPath(fillPath);

                g.setColour(juce::Colours::deepskyblue.withAlpha(0.9f));
                g.strokePath(topPath, juce::PathStrokeType(1.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour(juce::Colours::deepskyblue.withAlpha(0.7f));
                g.strokePath(bottomPath, juce::PathStrokeType(1.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        g.setColour(juce::Colours::limegreen.withAlpha(0.9f));
        g.drawLine(pbHX1, lane.getY(), pbHX1, lane.getBottom(), 1.5f);
        g.drawLine(pbHX2, lane.getY(), pbHX2, lane.getBottom(), 1.5f);

        g.setColour(juce::Colours::orange.withAlpha(0.9f));
        g.drawLine(lpHX1, lane.getY(), lpHX1, lane.getBottom(), 1.5f);
        g.drawLine(lpHX2, lane.getY(), lpHX2, lane.getBottom(), 1.5f);

        for (int voiceIndex = 0; voiceIndex < static_cast<int>(voicePlaybackPositions_.size()); ++voiceIndex)
        {
            const auto sampleIndex = voicePlaybackPositions_[static_cast<std::size_t>(voiceIndex)];
            if (sampleIndex < 0)
                continue;

            const auto markerX = juce::jlimit(bounds.getX(), bounds.getRight(), xFromSample(sampleIndex));
            const auto hue = std::fmod(0.08f + static_cast<float>(voiceIndex) * 0.61803398875f, 1.0f);
            const auto markerColour = juce::Colour::fromHSV(hue, 0.68f, 0.98f, 0.92f);

            g.setColour(markerColour);
            g.drawLine(markerX, lane.getY(), markerX, lane.getBottom(), 1.35f);
        }
    }

    // ── Labels on handles ──
    g.setFont(10.0f);

    // Playback labels (green, at bottom)
    g.setColour(juce::Colours::limegreen);
    g.drawText("P", static_cast<int>(pbHX1) - 6, static_cast<int>(bounds.getBottom()) - 14, 12, 12, juce::Justification::centred);
    g.drawText("P", static_cast<int>(pbHX2) - 6, static_cast<int>(bounds.getBottom()) - 14, 12, 12, juce::Justification::centred);

    // Loop labels (orange, at top)
    g.setColour(juce::Colours::orange);
    g.drawText("S", static_cast<int>(lpHX1) - 6, static_cast<int>(bounds.getY()) + 2, 12, 12, juce::Justification::centred);
    g.drawText("E", static_cast<int>(lpHX2) - 6, static_cast<int>(bounds.getY()) + 2, 12, 12, juce::Justification::centred);

    // Circular grab handles for easier interaction at the range ends
    constexpr float kHandleRadius = 4.5f;
    const auto topY = bounds.getY() + 10.0f;
    const auto bottomY = bounds.getBottom() - 10.0f;

    g.setColour(juce::Colours::orange.withAlpha(0.95f));
    g.fillEllipse(lpHX1 - kHandleRadius, topY - kHandleRadius, kHandleRadius * 2.0f, kHandleRadius * 2.0f);
    g.fillEllipse(lpHX2 - kHandleRadius, topY - kHandleRadius, kHandleRadius * 2.0f, kHandleRadius * 2.0f);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.drawEllipse(lpHX1 - kHandleRadius, topY - kHandleRadius, kHandleRadius * 2.0f, kHandleRadius * 2.0f, 1.0f);
    g.drawEllipse(lpHX2 - kHandleRadius, topY - kHandleRadius, kHandleRadius * 2.0f, kHandleRadius * 2.0f, 1.0f);

    g.setColour(juce::Colours::limegreen.withAlpha(0.95f));
    g.fillEllipse(pbHX1 - kHandleRadius, bottomY - kHandleRadius, kHandleRadius * 2.0f, kHandleRadius * 2.0f);
    g.fillEllipse(pbHX2 - kHandleRadius, bottomY - kHandleRadius, kHandleRadius * 2.0f, kHandleRadius * 2.0f);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.drawEllipse(pbHX1 - kHandleRadius, bottomY - kHandleRadius, kHandleRadius * 2.0f, kHandleRadius * 2.0f, 1.0f);
    g.drawEllipse(pbHX2 - kHandleRadius, bottomY - kHandleRadius, kHandleRadius * 2.0f, kHandleRadius * 2.0f, 1.0f);

    if (loopFormatBadge_.isNotEmpty())
    {
        const auto badgeWidth = loopFormatBadge_ == "Apple Loop" ? 84 : 66;
        auto badge = juce::Rectangle<float>(bounds.getRight() - static_cast<float>(badgeWidth) - 8.0f,
            bounds.getY() + 6.0f, static_cast<float>(badgeWidth), 16.0f);

        g.setColour(loopFormatBadge_ == "Apple Loop" ? juce::Colour(0xff5b4b8a) : juce::Colour(0xff4b6b2a));
        g.fillRoundedRectangle(badge, 4.0f);
        g.setColour(juce::Colour(0xffdfe6ff));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText(loopFormatBadge_, badge, juce::Justification::centred, false);
    }
}

void AudiocityAudioProcessorEditor::WaveformView::mouseDown(const juce::MouseEvent& event)
{
    linkedPlaybackDuringLoopDrag_ = false;

    if (event.mods.isRightButtonDown() || event.mods.isMiddleButtonDown())
    {
        dragMode_ = DragMode::pan;
        dragAnchorViewStart_ = viewStartSample_;
        return;
    }

    // Check proximity to all four handles — pick the nearest one
    const auto visualHandleX = [this](const float x)
    {
        return juce::jlimit(0.0f, static_cast<float>(juce::jmax(0, getWidth())), x);
    };

    const auto topY = 10.0f;
    const auto bottomY = juce::jmax(10.0f, static_cast<float>(getHeight()) - 10.0f);

    struct Handle { DragMode mode; float x; float y; };
    const Handle handles[] = {
        { DragMode::dragPlaybackStart, visualHandleX(xFromSample(playbackStart_)), bottomY },
        { DragMode::dragPlaybackEnd,   visualHandleX(xFromSample(playbackEnd_)), bottomY },
        { DragMode::dragLoopStart,     visualHandleX(xFromSample(loopStart_)), topY },
        { DragMode::dragLoopEnd,       visualHandleX(xFromSample(loopEnd_)), topY },
    };

    constexpr float kHandlePickPx = 12.0f;
    float bestDist = kHandlePickPx + 1.0f;
    DragMode bestMode = DragMode::none;

    for (const auto& h : handles)
    {
        const auto dx = event.position.x - h.x;
        const auto dy = event.position.y - h.y;
        const auto dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= kHandlePickPx && dist < bestDist)
        {
            bestDist = dist;
            bestMode = h.mode;
        }
    }

    dragMode_ = bestMode;
}

void AudiocityAudioProcessorEditor::WaveformView::mouseDrag(const juce::MouseEvent& event)
{
    if (dragMode_ == DragMode::pan)
    {
        panByPixels(static_cast<float>(event.getDistanceFromDragStartX()));
        return;
    }

    const auto sample = juce::jlimit(0, juce::jmax(0, totalSamples_ - 1), sampleFromX(event.position.x));
    const auto shiftHeld = event.mods.isShiftDown();

    if (dragMode_ == DragMode::dragLoopStart)
    {
        if (shiftHeld)
        {
            loopStart_ = juce::jlimit(0, juce::jmax(0, loopEnd_ - 1), sample);
            playbackStart_ = loopStart_;
            linkedPlaybackDuringLoopDrag_ = true;
            if (onPlaybackPreview)
                onPlaybackPreview(playbackStart_, playbackEnd_);
        }
        else
        {
            // Loop start must stay >= playbackStart_ and < loopEnd_
            loopStart_ = juce::jlimit(playbackStart_, juce::jmax(playbackStart_, loopEnd_ - 1), sample);
        }

        if (onLoopPreview)
            onLoopPreview(loopStart_, loopEnd_);
    }
    else if (dragMode_ == DragMode::dragLoopEnd)
    {
        if (shiftHeld)
        {
            loopEnd_ = juce::jlimit(loopStart_ + 1, juce::jmax(loopStart_ + 1, totalSamples_ - 1), sample);
            playbackEnd_ = loopEnd_;
            linkedPlaybackDuringLoopDrag_ = true;
            if (onPlaybackPreview)
                onPlaybackPreview(playbackStart_, playbackEnd_);
        }
        else
        {
            // Loop end must stay <= playbackEnd_ and > loopStart_
            loopEnd_ = juce::jlimit(loopStart_ + 1, juce::jmax(loopStart_ + 1, playbackEnd_), sample);
        }

        if (onLoopPreview)
            onLoopPreview(loopStart_, loopEnd_);
    }
    else if (dragMode_ == DragMode::dragPlaybackStart)
    {
        // Playback start must stay <= loopStart_
        playbackStart_ = juce::jlimit(0, juce::jmax(0, loopStart_), sample);
        if (onPlaybackPreview)
            onPlaybackPreview(playbackStart_, playbackEnd_);
    }
    else if (dragMode_ == DragMode::dragPlaybackEnd)
    {
        // Playback end must stay >= loopEnd_
        playbackEnd_ = juce::jlimit(loopEnd_, juce::jmax(0, totalSamples_ - 1), sample);
        if (onPlaybackPreview)
            onPlaybackPreview(playbackStart_, playbackEnd_);
    }
    else
    {
        return;
    }

    repaint();
}

void AudiocityAudioProcessorEditor::WaveformView::mouseUp(const juce::MouseEvent& event)
{
    if (dragMode_ == DragMode::dragLoopStart || dragMode_ == DragMode::dragLoopEnd)
    {
        if (event.mods.isShiftDown())
        {
            if (dragMode_ == DragMode::dragLoopStart)
                playbackStart_ = loopStart_;
            else
                playbackEnd_ = loopEnd_;

            linkedPlaybackDuringLoopDrag_ = true;
        }

        const auto committedPlaybackStart = playbackStart_;
        const auto committedPlaybackEnd = playbackEnd_;
        const auto committedLoopStart = loopStart_;
        const auto committedLoopEnd = loopEnd_;

        if (linkedPlaybackDuringLoopDrag_ && onPlaybackCommitted)
        {
            onPlaybackCommitted(committedPlaybackStart, committedPlaybackEnd);
        }

        if (onLoopCommitted)
        {
            onLoopCommitted(committedLoopStart, committedLoopEnd);
        }
    }
    else if (dragMode_ == DragMode::dragPlaybackStart || dragMode_ == DragMode::dragPlaybackEnd)
    {
        const auto committedPlaybackStart = playbackStart_;
        const auto committedPlaybackEnd = playbackEnd_;

        if (onPlaybackCommitted)
        {
            onPlaybackCommitted(committedPlaybackStart, committedPlaybackEnd);
        }
    }

    dragMode_ = DragMode::none;
    linkedPlaybackDuringLoopDrag_ = false;
}

void AudiocityAudioProcessorEditor::WaveformView::mouseDoubleClick(const juce::MouseEvent&)
{
    if (onResetRangesRequested)
        onResetRangesRequested();
    else
        resetView();
}

void AudiocityAudioProcessorEditor::WaveformView::mouseWheelMove(
    const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (event.mods.isCommandDown())
    {
        const auto zoomFactor = wheel.deltaY > 0.0f ? 0.85f : 1.2f;
        zoomAround(event.position.x, zoomFactor);
        return;
    }

    const auto panAmount = (wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY) * 120.0f;
    panByPixels(-panAmount);
}

// ─── Editor Constructor ────────────────────────────────────────────────────────

AudiocityAudioProcessorEditor::AudiocityAudioProcessorEditor(AudiocityAudioProcessor& processor)
    : AudioProcessorEditor(&processor),
      processor_(processor)
{
    setName("Audiocity");
    setSize(980, 860);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
    setLookAndFeel(&dialLaf_);
    tooltipWindow_ = std::make_unique<juce::TooltipWindow>(this, 700);
    tooltipWindow_->setLookAndFeel(&dialLaf_);

    addAndMakeVisible(tabBar_);
    tabBar_.setLookAndFeel(&tabTextLookAndFeel);
    tabBar_.setTabBarDepth(30);
    tabBar_.addTab("Sample", juce::Colour(0xff252538), &tabSamplePage_, false);
    tabBar_.addTab("Library", juce::Colour(0xff252538), &tabLibraryPage_, false);
    tabBar_.addTab("Player", juce::Colour(0xff252538), &tabPlayerPage_, false);
    tabBar_.addTab("Generate", juce::Colour(0xff252538), &tabGeneratePage_, false);
    currentTabIndex_ = processor_.getEditorTabIndex();
    tabBar_.setCurrentTabIndex(currentTabIndex_);

    addAndMakeVisible(sampleControlsViewport_);
    sampleControlsViewport_.setScrollBarsShown(true, false);
    sampleControlsViewport_.setViewedComponent(&sampleControlsContent_, false);
    sampleControlsContent_.onPaint = [this](juce::Graphics& g) { paintGroupBoxes(g); };

    // Player pane
    addAndMakeVisible(playerKeyboardLabel_);
    playerKeyboardLabel_.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(playerKeyboardScrollLeft_);
    addAndMakeVisible(playerKeyboardScrollRight_);
    playerKeyboardScrollLeft_.onClick = [this]
    {
        auto& bar = playerKeyboardViewport_.getHorizontalScrollBar();
        bar.setCurrentRangeStart(juce::jmax(0.0, bar.getCurrentRangeStart() - 140.0));
    };
    playerKeyboardScrollRight_.onClick = [this]
    {
        auto& bar = playerKeyboardViewport_.getHorizontalScrollBar();
        bar.setCurrentRangeStart(bar.getCurrentRangeStart() + 140.0);
    };

    addAndMakeVisible(playerKeyboardViewport_);
    playerKeyboardViewport_.setViewedComponent(&playerKeyboard_, false);
    playerKeyboardViewport_.setScrollBarsShown(true, false);

    playerKeyboardState_.addListener(this);
    playerKeyboard_.setAvailableRange(21, 108);
    playerKeyboard_.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffd7dde8));
    playerKeyboard_.setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff20253a));
    playerKeyboard_.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colour(0xff61d9ff).withAlpha(0.60f));
    playerKeyboard_.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colour(0xff8aa9dc).withAlpha(0.35f));

    addAndMakeVisible(playerPadsLabel_);
    playerPadsLabel_.setJustificationType(juce::Justification::centredLeft);

    playerPadAssignments_ = processor_.getAllPlayerPadAssignments();
    for (int i = 0; i < kPlayerPadCount; ++i)
    {
        auto& padButton = playerPadButtons_[static_cast<std::size_t>(i)];
        auto& assignButton = playerPadAssignButtons_[static_cast<std::size_t>(i)];

        addAndMakeVisible(padButton);
        addAndMakeVisible(assignButton);

        assignButton.setButtonText("...");

        padButton.onClick = [this, i]
        {
            const auto assignment = playerPadAssignments_[static_cast<std::size_t>(i)];

            processor_.enqueueUiMidiNoteOn(assignment.noteNumber, assignment.velocity);
            playerPadPendingOffNotes_[static_cast<std::size_t>(i)] = assignment.noteNumber;
            playerPadPendingOffTicks_[static_cast<std::size_t>(i)] = 7; // ~117ms at 60Hz
        };

        assignButton.onClick = [this, i]
        {
            showPadAssignmentDialog(i);
        };
    }
    refreshPlayerPadButtons();

    // Generate pane
    addAndMakeVisible(generateWaveformView_);
    addAndMakeVisible(generateSineButton_);
    addAndMakeVisible(generateRampButton_);
    addAndMakeVisible(generateSquareButton_);
    addAndMakeVisible(generateSawtoothButton_);
    addAndMakeVisible(generateTriangleButton_);
    addAndMakeVisible(generatePulseButton_);
    addAndMakeVisible(generateRandomButton_);
    addAndMakeVisible(generateNoiseButton_);
    addAndMakeVisible(generateSamplesLabel_);
    addAndMakeVisible(generateSamplesCombo_);
    addAndMakeVisible(generateBitDepthLabel_);
    addAndMakeVisible(generateBitDepthCombo_);
    addAndMakeVisible(generateSketchSmoothingLabel_);
    addAndMakeVisible(generateSketchSmoothingCombo_);
    addAndMakeVisible(generatePulseWidthLabel_);
    addAndMakeVisible(generatePulseWidthSlider_);
    addAndMakeVisible(generatePreviewButton_);
    addAndMakeVisible(generateFrequencyLabel_);
    addAndMakeVisible(generateFrequencyCombo_);
    addAndMakeVisible(generateLoadAsSampleButton_);

    generateSamplesLabel_.setJustificationType(juce::Justification::centredLeft);
    generateBitDepthLabel_.setJustificationType(juce::Justification::centredLeft);
    generateSketchSmoothingLabel_.setJustificationType(juce::Justification::centredLeft);
    generatePulseWidthLabel_.setJustificationType(juce::Justification::centredLeft);
    generateFrequencyLabel_.setJustificationType(juce::Justification::centredLeft);

    for (int power = 4; power <= 11; ++power)
    {
        const auto sampleCount = 1 << power;
        generateSamplesCombo_.addItem(juce::String(sampleCount), sampleCount);
    }
    generateSamplesCombo_.setSelectedId(processor_.getGenerateSampleCount(), juce::dontSendNotification);
    if (generateSamplesCombo_.getSelectedId() <= 0)
        generateSamplesCombo_.setSelectedId(512, juce::dontSendNotification);
    generateSamplesCombo_.onChange = [this]
    {
        processor_.setGenerateSampleCount(getSelectedGenerateSampleCount());
        regenerateWaveform();
    };

    generateBitDepthCombo_.addItem("8 bit", 8);
    generateBitDepthCombo_.addItem("16 bit", 16);
    generateBitDepthCombo_.addItem("24 bit", 24);
    generateBitDepthCombo_.setSelectedId(processor_.getGenerateBitDepth(), juce::dontSendNotification);
    if (generateBitDepthCombo_.getSelectedId() <= 0)
        generateBitDepthCombo_.setSelectedId(16, juce::dontSendNotification);
    generateBitDepthCombo_.onChange = [this]
    {
        processor_.setGenerateBitDepth(getSelectedGenerateBitDepth());
        regenerateWaveform();
    };

    generateSketchSmoothingCombo_.addItem("Line", 1);
    generateSketchSmoothingCombo_.addItem("Curve", 2);
    generateSketchSmoothingCombo_.setSelectedId(processor_.getGenerateSketchSmoothing(), juce::dontSendNotification);
    if (generateSketchSmoothingCombo_.getSelectedId() <= 0)
        generateSketchSmoothingCombo_.setSelectedId(1, juce::dontSendNotification);
    selectedSketchSmoothing_ = generateSketchSmoothingCombo_.getSelectedId() == 2
        ? SketchedWaveSmoothing::curve
        : SketchedWaveSmoothing::line;
    generateSketchSmoothingCombo_.onChange = [this]
    {
        processor_.setGenerateSketchSmoothing(generateSketchSmoothingCombo_.getSelectedId());
        selectedSketchSmoothing_ = generateSketchSmoothingCombo_.getSelectedId() == 2
            ? SketchedWaveSmoothing::curve
            : SketchedWaveSmoothing::line;
    };

    generatePulseWidthSlider_.setRange(1.0, 99.0, 1.0);
    generatePulseWidthSlider_.setValue(processor_.getGeneratePulseWidth(), juce::dontSendNotification);
    generatePulseWidthSlider_.setTextValueSuffix(" %");
    generatePulseWidthSlider_.onValueChange = [this]
    {
        processor_.setGeneratePulseWidth(static_cast<float>(generatePulseWidthSlider_.getValue()));
        regenerateWaveform();
    };

    for (int midi = 0; midi <= 127; ++midi)
        generateFrequencyCombo_.addItem(formatMidiNoteName(midi), midi + 1);
    generateFrequencyCombo_.setSelectedId(processor_.getGenerateFrequencyMidiNote() + 1, juce::dontSendNotification);
    if (generateFrequencyCombo_.getSelectedId() <= 0)
        generateFrequencyCombo_.setSelectedId(61, juce::dontSendNotification);
    generateFrequencyCombo_.onChange = [this]
    {
        const auto selected = generateFrequencyCombo_.getSelectedId();
        if (selected > 0)
            processor_.setGenerateFrequencyMidiNote(selected - 1);
        processor_.setGeneratedWaveformPreviewMidiNote(getSelectedGenerateMidiNote());
    };

    selectedGeneratedWaveType_ = static_cast<GeneratedWaveType>(juce::jlimit(0, 7, processor_.getGenerateWaveType()));

    auto bindWaveButton = [this](juce::TextButton& button, const GeneratedWaveType type)
    {
        button.onClick = [this, type]
        {
            selectedGeneratedWaveType_ = type;
            processor_.setGenerateWaveType(static_cast<int>(type));
            updateGeneratePulseWidthControlState();
            regenerateWaveform();
        };
    };

    bindWaveButton(generateSineButton_, GeneratedWaveType::sine);
    bindWaveButton(generateRampButton_, GeneratedWaveType::ramp);
    bindWaveButton(generateSquareButton_, GeneratedWaveType::square);
    bindWaveButton(generateSawtoothButton_, GeneratedWaveType::sawtooth);
    bindWaveButton(generateTriangleButton_, GeneratedWaveType::triangle);
    bindWaveButton(generatePulseButton_, GeneratedWaveType::pulse);
    bindWaveButton(generateRandomButton_, GeneratedWaveType::random);
    bindWaveButton(generateNoiseButton_, GeneratedWaveType::noise);

    generatePreviewButton_.onClick = [this]
    {
        if (processor_.isGeneratedWaveformPreviewPlaying())
            processor_.stopGeneratedWaveformPreview();
        else
        {
            processor_.setGeneratedWaveformPreview(generatedWaveform_);
            processor_.setGeneratedWaveformPreviewMidiNote(getSelectedGenerateMidiNote());
            processor_.startGeneratedWaveformPreview();
        }
        updateGeneratePreviewButtonText();
    };

    generateLoadAsSampleButton_.onClick = [this]
    {
        processor_.stopGeneratedWaveformPreview();
        updateGeneratePreviewButtonText();

        const auto selectedMidiNote = getSelectedGenerateMidiNote();
        processor_.loadGeneratedWaveformAsSample(generatedWaveform_, selectedMidiNote);
        processor_.setRootMidiNote(selectedMidiNote);
        tabBar_.setCurrentTabIndex(0);
        currentTabIndex_ = 0;
        processor_.setEditorTabIndex(currentTabIndex_);
        updateTabVisibility();
        resized();
        refreshUI(true);
    };

    generateWaveformView_.setWaveChangedCallback([this](const std::vector<float>& sketchedWave)
    {
        applySketchedWaveform(sketchedWave);
    });

    updateGeneratePulseWidthControlState();

    regenerateWaveform();

    // Sample browser pane
    addAndMakeVisible(sampleBrowserRootLabel_);
    sampleBrowserRootLabel_.setJustificationType(juce::Justification::centredLeft);
    sampleBrowserRootLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8a8aa0));
    sampleBrowserRootLabel_.setText("< Select Folder >", juce::dontSendNotification);

    addAndMakeVisible(sampleBrowserChooseRootButton_);
    sampleBrowserChooseRootButton_.onClick = [this] { chooseSampleRootFolder(); };

    addAndMakeVisible(sampleBrowserFilterEditor_);
    sampleBrowserFilterEditor_.setTextToShowWhenEmpty("Search samples...", juce::Colours::grey);
    sampleBrowserFilterEditor_.onTextChange = [this] { rebuildVisibleSampleList(); };

    addAndMakeVisible(sampleBrowserSortCombo_);
    sampleBrowserSortCombo_.addItem("Name", 1);
    sampleBrowserSortCombo_.addItem("Relative Path", 2);
    sampleBrowserSortCombo_.setSelectedId(1, juce::dontSendNotification);
    sampleBrowserSortCombo_.onChange = [this] { rebuildVisibleSampleList(); };

    addAndMakeVisible(sampleBrowserListBox_);
    sampleBrowserListBox_.setModel(this);
    sampleBrowserListBox_.setRowHeight(66);
    sampleBrowserListBox_.setMultipleSelectionEnabled(false);

    addAndMakeVisible(sampleBrowserCountLabel_);
    sampleBrowserCountLabel_.setJustificationType(juce::Justification::centredLeft);
    sampleBrowserCountLabel_.setText("No folder selected", juce::dontSendNotification);

    addAndMakeVisible(sampleBrowserPreviewLabel_);
    sampleBrowserPreviewLabel_.setJustificationType(juce::Justification::centredRight);
    sampleBrowserPreviewLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff61d9ff));
    sampleBrowserPreviewLabel_.setText({}, juce::dontSendNotification);

    // Sample info row
    addAndMakeVisible(samplePathLabel_);
    samplePathLabel_.setJustificationType(juce::Justification::centredLeft);
    samplePathLabel_.setText("No sample loaded", juce::dontSendNotification);

    addAndMakeVisible(loadButton_);
    loadButton_.setTooltip("Load Sample");
    loadButton_.onClick = [this] { openSampleChooser(); };

    addAndMakeVisible(waveformDisplayModeCombo_);
    waveformDisplayModeCombo_.addItem("Signed", 1);
    waveformDisplayModeCombo_.addItem("Symmetric", 2);
    waveformDisplayModeCombo_.setSelectedId(processor_.getWaveformDisplayMode(), juce::dontSendNotification);
    waveformView_.setDisplayMode(waveformDisplayModeCombo_.getSelectedId() == 2
        ? WaveformView::DisplayMode::symmetricEnvelope
        : WaveformView::DisplayMode::signedWaveform);
    waveformDisplayModeCombo_.setTooltip("Waveform Display Mode");
    waveformDisplayModeCombo_.onChange = [this]
    {
        const auto selected = waveformDisplayModeCombo_.getSelectedId();
        processor_.setWaveformDisplayMode(selected);
        waveformView_.setDisplayMode(selected == 2
            ? WaveformView::DisplayMode::symmetricEnvelope
            : WaveformView::DisplayMode::signedWaveform);
    };

    addAndMakeVisible(rootNoteLabel_);
    rootNoteLabel_.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(rootNoteCombo_);
    for (int note = 0; note <= 127; ++note)
        rootNoteCombo_.addItem(formatMidiNoteName(note), note + 1);
    rootNoteCombo_.setSelectedId(61, juce::dontSendNotification);
    rootNoteCombo_.onChange = [this]
    {
        const auto selected = rootNoteCombo_.getSelectedId();
        if (selected > 0)
            processor_.setRootMidiNote(selected - 1);
    };

    addAndMakeVisible(tuneCoarseDial_);
    tuneCoarseDial_.setDoubleClickResetValue(0.0);
    tuneCoarseDial_.onValueChange = [this]
    {
        processor_.setCoarseTuneSemitones(static_cast<float>(tuneCoarseDial_.getValue()));
    };

    addAndMakeVisible(tuneFineDial_);
    tuneFineDial_.setDoubleClickResetValue(0.0);
    tuneFineDial_.onValueChange = [this]
    {
        processor_.setFineTuneCents(static_cast<float>(tuneFineDial_.getValue()));
    };

    addAndMakeVisible(pitchBendRangeDial_);
    pitchBendRangeDial_.setDoubleClickResetValue(2.0);
    pitchBendRangeDial_.onValueChange = [this]
    {
        processor_.setPitchBendRangeSemitones(static_cast<float>(pitchBendRangeDial_.getValue()));
    };
    addAndMakeVisible(pitchLfoRateDial_);
    pitchLfoRateDial_.setDoubleClickResetValue(0.0);
    pitchLfoRateDial_.onValueChange = [this] { pushPitchLfoSettings(); };
    addAndMakeVisible(pitchLfoDepthDial_);
    pitchLfoDepthDial_.setDoubleClickResetValue(0.0);
    pitchLfoDepthDial_.onValueChange = [this] { pushPitchLfoSettings(); };

    // Waveform
    addAndMakeVisible(waveformView_);
    waveformView_.onLoopPreview = [this](const int ls, const int le)
    {
        loopStartDial_.setValue(ls, juce::dontSendNotification);
        loopEndDial_.setValue(le, juce::dontSendNotification);
    };
    waveformView_.onLoopCommitted = [this](const int ls, const int le)
    {
        loopStartDial_.setValue(ls, juce::dontSendNotification);
        loopEndDial_.setValue(le, juce::dontSendNotification);
        applyLoopPoints();
    };
    waveformView_.onPlaybackPreview = [this](const int ps, const int pe)
    {
        playbackStartDial_.setValue(ps, juce::dontSendNotification);
        playbackEndDial_.setValue(pe, juce::dontSendNotification);
    };
    waveformView_.onPlaybackCommitted = [this](const int ps, const int pe)
    {
        playbackStartDial_.setValue(ps, juce::dontSendNotification);
        playbackEndDial_.setValue(pe, juce::dontSendNotification);
        pushPlaybackWindow();
    };
    waveformView_.onResetRangesRequested = [this]
    {
        const auto sampleLength = processor_.getLoadedSampleLength();
        if (sampleLength <= 0)
        {
            waveformView_.resetView();
            return;
        }

        const int defaultStart = 0;
        const int defaultEnd = sampleLength - 1;

        playbackStartDial_.setValue(defaultStart, juce::dontSendNotification);
        playbackEndDial_.setValue(defaultEnd, juce::dontSendNotification);
        loopStartDial_.setValue(defaultStart, juce::dontSendNotification);
        loopEndDial_.setValue(defaultEnd, juce::dontSendNotification);

        processor_.setSampleWindow(defaultStart, defaultEnd);
        processor_.setLoopPoints(defaultStart, defaultEnd);

        waveformView_.setState(sampleLength, getLoadedSampleWaveformMinMaxByChannel(),
            defaultStart, defaultEnd, defaultStart, defaultEnd,
            processor_.getLoadedSampleLoopFormatBadge());
        waveformView_.resetView();
    };

    // Playback mode
    addAndMakeVisible(playbackModeLabel_);
    addAndMakeVisible(playbackModeGateButton_);
    addAndMakeVisible(playbackModeOneShotButton_);
    addAndMakeVisible(playbackModeLoopButton_);

    constexpr int kPlaybackModeRadioGroup = 42001;
    playbackModeGateButton_.setRadioGroupId(kPlaybackModeRadioGroup);
    playbackModeOneShotButton_.setRadioGroupId(kPlaybackModeRadioGroup);
    playbackModeLoopButton_.setRadioGroupId(kPlaybackModeRadioGroup);

    playbackModeGateButton_.setToggleState(true, juce::dontSendNotification);
    playbackModeGateButton_.onClick = [this]
    {
        if (playbackModeGateButton_.getToggleState())
            processor_.setPlaybackMode(AudiocityAudioProcessor::PlaybackMode::gate);
    };
    playbackModeOneShotButton_.onClick = [this]
    {
        if (playbackModeOneShotButton_.getToggleState())
            processor_.setPlaybackMode(AudiocityAudioProcessor::PlaybackMode::oneShot);
    };
    playbackModeLoopButton_.onClick = [this]
    {
        if (playbackModeLoopButton_.getToggleState())
            processor_.setPlaybackMode(AudiocityAudioProcessor::PlaybackMode::loop);
    };

    // Playback window (trim) controls
    addAndMakeVisible(playbackStartDial_);
    addAndMakeVisible(playbackEndDial_);
    playbackStartDial_.onValueChange = [this]
    {
        enforcePlaybackLoopConstraints();
        pushPlaybackWindow();
    };
    playbackEndDial_.onValueChange = [this]
    {
        enforcePlaybackLoopConstraints();
        pushPlaybackWindow();
    };

    // Loop controls
    addAndMakeVisible(loopStartDial_);
    addAndMakeVisible(loopEndDial_);
    addAndMakeVisible(loopCrossfadeDial_);
    loopCrossfadeDial_.setDoubleClickResetValue(0.0);
    loopStartDial_.onValueChange = [this]
    {
        enforcePlaybackLoopConstraints();
        applyLoopPoints();
    };
    loopEndDial_.onValueChange = [this]
    {
        enforcePlaybackLoopConstraints();
        applyLoopPoints();
    };
    loopCrossfadeDial_.onValueChange = [this]
    {
        processor_.setLoopCrossfadeSamples(juce::jmax(0, static_cast<int>(loopCrossfadeDial_.getValue())));
    };

    // Performance
    addAndMakeVisible(monoToggle_);
    addAndMakeVisible(legatoToggle_);
    addAndMakeVisible(velocityCurveLabel_);
    velocityCurveLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(velocityCurveCombo_);
    velocityCurveCombo_.addItem("Vel Linear", 1);
    velocityCurveCombo_.addItem("Vel Soft", 2);
    velocityCurveCombo_.addItem("Vel Hard", 3);
    velocityCurveCombo_.setSelectedId(1, juce::dontSendNotification);
    velocityCurveCombo_.onChange = [this]
    {
        const auto selected = velocityCurveCombo_.getSelectedId();
        auto curve = AudiocityAudioProcessor::VelocityCurve::linear;
        if (selected == 2)
            curve = AudiocityAudioProcessor::VelocityCurve::soft;
        else if (selected == 3)
            curve = AudiocityAudioProcessor::VelocityCurve::hard;
        processor_.setVelocityCurve(curve);
    };
    monoToggle_.onClick = [this] { pushPerformanceControls(); };
    legatoToggle_.onClick = [this] { pushPerformanceControls(); };

    addAndMakeVisible(glideDial_);
    glideDial_.setDoubleClickResetValue(0.0);
    glideDial_.onValueChange = [this]
    {
        processor_.setGlideSeconds(static_cast<float>(glideDial_.getValue()) / 1000.0f);
    };

    addAndMakeVisible(polyphonyDial_);
    polyphonyDial_.setDoubleClickResetValue(64.0);
    polyphonyDial_.onValueChange = [this]
    {
        processor_.setPolyphonyLimit(juce::jlimit(1, 64, static_cast<int>(std::round(polyphonyDial_.getValue()))));
    };

    // Amp ADSR
    addAndMakeVisible(ampAttackDial_);
    ampAttackDial_.setDoubleClickResetValue(0.1);
    addAndMakeVisible(ampDecayDial_);
    ampDecayDial_.setDoubleClickResetValue(1.0);
    addAndMakeVisible(ampSustainDial_);
    ampSustainDial_.setDoubleClickResetValue(1.0);
    addAndMakeVisible(ampReleaseDial_);
    ampReleaseDial_.setDoubleClickResetValue(5.0);
    addAndMakeVisible(ampLfoRateDial_);
    ampLfoRateDial_.setDoubleClickResetValue(0.0);
    addAndMakeVisible(ampLfoDepthDial_);
    ampLfoDepthDial_.setDoubleClickResetValue(0.0);
    addAndMakeVisible(ampLfoShapeLabel_);
    ampLfoShapeLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(ampLfoShapeCombo_);
    ampLfoShapeCombo_.addItem("Sine", 1);
    ampLfoShapeCombo_.addItem("Triangle", 2);
    ampLfoShapeCombo_.addItem("Square", 3);
    ampLfoShapeCombo_.addItem("Saw Up", 4);
    ampLfoShapeCombo_.addItem("Saw Down", 5);
    ampLfoShapeCombo_.setSelectedId(1, juce::dontSendNotification);
    ampLfoShapeCombo_.onChange = [this] { pushAmpLfoSettings(); };
    addAndMakeVisible(ampEnvelopeGraph_);
    ampAttackDial_.onValueChange = [this] { pushAmpEnvelope(); updateAmpEnvelopeGraphFromDials(); };
    ampDecayDial_.onValueChange = [this] { pushAmpEnvelope(); updateAmpEnvelopeGraphFromDials(); };
    ampSustainDial_.onValueChange = [this] { pushAmpEnvelope(); updateAmpEnvelopeGraphFromDials(); };
    ampReleaseDial_.onValueChange = [this] { pushAmpEnvelope(); updateAmpEnvelopeGraphFromDials(); };
    ampLfoRateDial_.onValueChange = [this] { pushAmpLfoSettings(); };
    ampLfoDepthDial_.onValueChange = [this] { pushAmpLfoSettings(); };

    // Filter
    addAndMakeVisible(filterCutoffDial_);
    filterCutoffDial_.setDoubleClickResetValue(18000.0);
    addAndMakeVisible(filterResDial_);
    filterResDial_.setDoubleClickResetValue(0.0);
    addAndMakeVisible(filterEnvAmtDial_);
    filterEnvAmtDial_.setDoubleClickResetValue(0.0);
    addAndMakeVisible(filterTypeLabel_);
    addAndMakeVisible(filterResponseGraph_);
    filterTypeLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(filterTypeCombo_);
    filterTypeCombo_.addItem("LP 12", 1);
    filterTypeCombo_.addItem("LP 24", 2);
    filterTypeCombo_.addItem("HP 12", 3);
    filterTypeCombo_.addItem("HP 24", 4);
    filterTypeCombo_.addItem("BP 12", 5);
    filterTypeCombo_.addItem("Notch", 6);
    filterTypeCombo_.setSelectedId(1, juce::dontSendNotification);
    filterTypeCombo_.onChange = [this] { pushFilterSettings(); };

    addAndMakeVisible(filterAttackDial_);
    filterAttackDial_.setDoubleClickResetValue(0.1);
    addAndMakeVisible(filterDecayDial_);
    filterDecayDial_.setDoubleClickResetValue(1.0);
    addAndMakeVisible(filterSustainDial_);
    filterSustainDial_.setDoubleClickResetValue(1.0);
    addAndMakeVisible(filterReleaseDial_);
    filterReleaseDial_.setDoubleClickResetValue(5.0);
    addAndMakeVisible(filterEnvelopeGraph_);
    addAndMakeVisible(filterKeytrackDial_);
    filterKeytrackDial_.setDoubleClickResetValue(0.0);
    addAndMakeVisible(filterVelDial_);
    filterVelDial_.setDoubleClickResetValue(0.0);
    addAndMakeVisible(filterLfoRateDial_);
    filterLfoRateDial_.setDoubleClickResetValue(0.0);
    addAndMakeVisible(filterLfoRateKeyDial_);
    addAndMakeVisible(filterLfoAmtDial_);
    filterLfoAmtDial_.setDoubleClickResetValue(0.0);
    addAndMakeVisible(filterLfoAmtKeyDial_);
    addAndMakeVisible(filterLfoStartPhaseDial_);
    addAndMakeVisible(filterLfoStartRandDial_);
    addAndMakeVisible(filterLfoFadeInDial_);
    addAndMakeVisible(filterLfoShapeLabel_);
    filterLfoShapeLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(filterLfoShapeCombo_);
    filterLfoShapeCombo_.addItem("Sine", 1);
    filterLfoShapeCombo_.addItem("Triangle", 2);
    filterLfoShapeCombo_.addItem("Square", 3);
    filterLfoShapeCombo_.addItem("Saw Up", 4);
    filterLfoShapeCombo_.addItem("Saw Down", 5);
    filterLfoShapeCombo_.setSelectedId(1, juce::dontSendNotification);
    filterLfoShapeCombo_.onChange = [this] { pushFilterSettings(); };
    addAndMakeVisible(filterLfoRetriggerToggle_);
    filterLfoRetriggerToggle_.onClick = [this] { pushFilterSettings(); };
    addAndMakeVisible(filterLfoTempoSyncToggle_);
    filterLfoTempoSyncToggle_.onClick = [this] { pushFilterSettings(); };
    addAndMakeVisible(filterLfoRateKeySyncToggle_);
    filterLfoRateKeySyncToggle_.onClick = [this] { pushFilterSettings(); };
    addAndMakeVisible(filterLfoKeytrackLinearToggle_);
    filterLfoKeytrackLinearToggle_.onClick = [this] { pushFilterSettings(); };
    addAndMakeVisible(filterLfoUnipolarToggle_);
    filterLfoUnipolarToggle_.onClick = [this] { pushFilterSettings(); };
    addAndMakeVisible(filterLfoDivisionLabel_);
    filterLfoDivisionLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(filterLfoDivisionCombo_);
    filterLfoDivisionCombo_.addItem("1/16", 1);
    filterLfoDivisionCombo_.addItem("1/16T", 2);
    filterLfoDivisionCombo_.addItem("1/16.", 3);
    filterLfoDivisionCombo_.addItem("1/8", 4);
    filterLfoDivisionCombo_.addItem("1/8T", 5);
    filterLfoDivisionCombo_.addItem("1/8.", 6);
    filterLfoDivisionCombo_.addItem("1/4", 7);
    filterLfoDivisionCombo_.addItem("1/4T", 8);
    filterLfoDivisionCombo_.addItem("1/4.", 9);
    filterLfoDivisionCombo_.addItem("1/2", 10);
    filterLfoDivisionCombo_.addItem("1/1", 11);
    filterLfoDivisionCombo_.addItem("2/1", 12);
    filterLfoDivisionCombo_.setSelectedId(7, juce::dontSendNotification);
    filterLfoDivisionCombo_.onChange = [this] { pushFilterSettings(); };
    addAndMakeVisible(filterKeytrackSnapLabel_);
    filterKeytrackSnapLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(filterKeytrackSnapCombo_);
    filterKeytrackSnapCombo_.addItem("Snap...", 1);
    filterKeytrackSnapCombo_.addItem("-100%", 2);
    filterKeytrackSnapCombo_.addItem("0%", 3);
    filterKeytrackSnapCombo_.addItem("50%", 4);
    filterKeytrackSnapCombo_.addItem("100%", 5);
    filterKeytrackSnapCombo_.addItem("200%", 6);
    filterKeytrackSnapCombo_.setSelectedId(1, juce::dontSendNotification);
    filterKeytrackSnapCombo_.onChange = [this]
    {
        const auto selected = filterKeytrackSnapCombo_.getSelectedId();
        if (selected <= 1)
            return;

        int snapPercent = 0;
        switch (selected)
        {
            case 2: snapPercent = -100; break;
            case 3: snapPercent = 0; break;
            case 4: snapPercent = 50; break;
            case 5: snapPercent = 100; break;
            case 6: snapPercent = 200; break;
            default: break;
        }

        filterKeytrackDial_.setValue(static_cast<double>(snapPercent));
        filterKeytrackSnapCombo_.setSelectedId(1, juce::dontSendNotification);
    };
    filterCutoffDial_.onValueChange = [this] { pushFilterSettings(); };
    filterResDial_.onValueChange = [this] { pushFilterSettings(); };
    filterEnvAmtDial_.onValueChange = [this] { pushFilterSettings(); };
    filterKeytrackDial_.onValueChange = [this] { pushFilterSettings(); };
    filterVelDial_.onValueChange = [this] { pushFilterSettings(); };
    filterLfoRateDial_.setShiftWheelFineFactor(8.0);
    filterLfoRateDial_.onValueChange = [this] { pushFilterSettings(); };
    filterLfoRateKeyDial_.onValueChange = [this] { pushFilterSettings(); };
    filterLfoAmtDial_.onValueChange = [this] { pushFilterSettings(); };
    filterLfoAmtKeyDial_.onValueChange = [this] { pushFilterSettings(); };
    filterLfoStartPhaseDial_.onValueChange = [this] { pushFilterSettings(); };
    filterLfoStartRandDial_.onValueChange = [this] { pushFilterSettings(); };
    filterLfoFadeInDial_.onValueChange = [this] { pushFilterSettings(); };
    filterAttackDial_.onValueChange = [this] { pushFilterEnvelope(); updateFilterEnvelopeGraphFromDials(); };
    filterDecayDial_.onValueChange = [this] { pushFilterEnvelope(); updateFilterEnvelopeGraphFromDials(); };
    filterSustainDial_.onValueChange = [this] { pushFilterEnvelope(); updateFilterEnvelopeGraphFromDials(); };
    filterReleaseDial_.onValueChange = [this] { pushFilterEnvelope(); updateFilterEnvelopeGraphFromDials(); };

    filterLfoRateKeyDial_.setVisible(false);
    filterLfoAmtKeyDial_.setVisible(false);
    filterLfoStartPhaseDial_.setVisible(false);
    filterLfoStartRandDial_.setVisible(false);
    filterLfoFadeInDial_.setVisible(false);
    filterKeytrackSnapLabel_.setVisible(false);
    filterKeytrackSnapCombo_.setVisible(false);
    filterLfoRateKeySyncToggle_.setVisible(false);
    filterLfoKeytrackLinearToggle_.setVisible(false);
    filterLfoUnipolarToggle_.setVisible(false);

    // Quality / Preload
    addAndMakeVisible(qualityLabel_);
    addAndMakeVisible(qualityCpuButton_);
    addAndMakeVisible(qualityFidelityButton_);
    addAndMakeVisible(qualityUltraButton_);

    constexpr int kQualityRadioGroup = 42002;
    qualityCpuButton_.setRadioGroupId(kQualityRadioGroup);
    qualityFidelityButton_.setRadioGroupId(kQualityRadioGroup);
    qualityUltraButton_.setRadioGroupId(kQualityRadioGroup);

    qualityFidelityButton_.setToggleState(true, juce::dontSendNotification);
    qualityCpuButton_.onClick = [this]
    {
        if (qualityCpuButton_.getToggleState())
            processor_.setQualityTier(AudiocityAudioProcessor::QualityTier::cpu);
    };
    qualityFidelityButton_.onClick = [this]
    {
        if (qualityFidelityButton_.getToggleState())
            processor_.setQualityTier(AudiocityAudioProcessor::QualityTier::fidelity);
    };
    qualityUltraButton_.onClick = [this]
    {
        if (qualityUltraButton_.getToggleState())
            processor_.setQualityTier(AudiocityAudioProcessor::QualityTier::ultra);
    };

    addAndMakeVisible(preloadDial_);
    preloadDial_.setDoubleClickResetValue(32768.0);
    addAndMakeVisible(masterVolumeDial_);
    addAndMakeVisible(panDial_);
    addAndMakeVisible(outputLevelMeter_);
    addAndMakeVisible(reverbMixDial_);
    addAndMakeVisible(delayTimeDial_);
    addAndMakeVisible(delayFeedbackDial_);
    addAndMakeVisible(delayMixDial_);
    addAndMakeVisible(delayTempoSyncToggle_);
    addAndMakeVisible(dcFilterEnabledToggle_);
    addAndMakeVisible(dcFilterCutoffDial_);
    addAndMakeVisible(autopanRateDial_);
    addAndMakeVisible(autopanDepthDial_);
    addAndMakeVisible(saturationDriveDial_);
    addAndMakeVisible(saturationModeCombo_);
    masterVolumeDial_.setDoubleClickResetValue(100.0);
    panDial_.setDoubleClickResetValue(0.0);
    reverbMixDial_.setDoubleClickResetValue(0.0);
    delayTimeDial_.setDoubleClickResetValue(320.0);
    delayFeedbackDial_.setDoubleClickResetValue(35.0);
    delayMixDial_.setDoubleClickResetValue(0.0);
    dcFilterCutoffDial_.setDoubleClickResetValue(10.0);
    autopanRateDial_.setDoubleClickResetValue(0.5);
    autopanDepthDial_.setDoubleClickResetValue(0.0);
    saturationDriveDial_.setDoubleClickResetValue(0.0);
    saturationModeCombo_.addItem("Soft Clip", 1);
    saturationModeCombo_.addItem("Hard Clip", 2);
    saturationModeCombo_.addItem("Tape", 3);
    saturationModeCombo_.addItem("Tube", 4);
    saturationModeCombo_.setSelectedId(1, juce::dontSendNotification);
    preloadDial_.onValueChange = [this]
    {
        processor_.setPreloadSamples(juce::jmax(256, static_cast<int>(preloadDial_.getValue())));
        refreshUI();
    };
    masterVolumeDial_.onValueChange = [this]
    {
        processor_.setMasterVolume(static_cast<float>(masterVolumeDial_.getValue()) / 100.0f);
    };
    panDial_.onValueChange = [this]
    {
        processor_.setPan(static_cast<float>(panDial_.getValue()) / 100.0f);
    };
    reverbMixDial_.onValueChange = [this]
    {
        processor_.setReverbMix(static_cast<float>(reverbMixDial_.getValue()) / 100.0f);
    };
    delayTimeDial_.onValueChange = [this] { pushDelaySettings(); };
    delayFeedbackDial_.onValueChange = [this] { pushDelaySettings(); };
    delayMixDial_.onValueChange = [this] { pushDelaySettings(); };
    delayTempoSyncToggle_.onClick = [this] { pushDelaySettings(); };
    dcFilterEnabledToggle_.onClick = [this] { pushDcFilterSettings(); };
    dcFilterCutoffDial_.onValueChange = [this] { pushDcFilterSettings(); };
    autopanRateDial_.onValueChange = [this] { pushAutopanSettings(); };
    autopanDepthDial_.onValueChange = [this] { pushAutopanSettings(); };
    saturationDriveDial_.onValueChange = [this] { pushSaturationSettings(); };
    saturationModeCombo_.onChange = [this] { pushSaturationSettings(); };

    // Reverse / Fade
    addAndMakeVisible(reverseToggle_);
    reverseToggle_.onClick = [this]
    {
        processor_.setReversePlayback(reverseToggle_.getToggleState());
    };

    addAndMakeVisible(fadeInDial_);
    addAndMakeVisible(fadeOutDial_);
    fadeInDial_.setDoubleClickResetValue(0.0);
    fadeOutDial_.setDoubleClickResetValue(0.0);
    auto pushFades = [this]
    {
        processor_.setFadeSamples(
            juce::jmax(0, static_cast<int>(fadeInDial_.getValue())),
            juce::jmax(0, static_cast<int>(fadeOutDial_.getValue())));
    };
    fadeInDial_.onValueChange = pushFades;
    fadeOutDial_.onValueChange = pushFades;

    // Diagnostics
    addAndMakeVisible(diagnosticsLabel_);
    diagnosticsLabel_.setJustificationType(juce::Justification::centredLeft);
    diagnosticsLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));

    // Register all dials with their parameter IDs for CC mapping
    allDials_ = {
        { &playbackStartDial_,  "playbackStart" },
        { &playbackEndDial_,    "playbackEnd" },
        { &loopStartDial_,      "loopStart" },
        { &loopEndDial_,        "loopEnd" },
        { &loopCrossfadeDial_,  "loopCrossfade" },
        { &glideDial_,          "glide" },
        { &polyphonyDial_,      "polyphony" },
        { &ampAttackDial_,      "ampAttack" },
        { &ampDecayDial_,       "ampDecay" },
        { &ampSustainDial_,     "ampSustain" },
        { &ampReleaseDial_,     "ampRelease" },
        { &ampLfoRateDial_,     "ampLfoRate" },
        { &ampLfoDepthDial_,    "ampLfoDepth" },
        { &filterCutoffDial_,   "filterCutoff" },
        { &filterResDial_,      "filterRes" },
        { &filterEnvAmtDial_,   "filterEnvAmt" },
        { &filterAttackDial_,   "filterAttack" },
        { &filterDecayDial_,    "filterDecay" },
        { &filterSustainDial_,  "filterSustain" },
        { &filterReleaseDial_,  "filterRelease" },
        { &filterKeytrackDial_, "filterKeytrack" },
        { &filterVelDial_,      "filterVel" },
        { &filterLfoRateDial_,  "filterLfoRate" },
        { &filterLfoRateKeyDial_, "filterLfoRateKeytrack" },
        { &filterLfoAmtDial_,   "filterLfoAmount" },
        { &filterLfoAmtKeyDial_, "filterLfoAmountKeytrack" },
        { &filterLfoStartPhaseDial_, "filterLfoStartPhase" },
        { &filterLfoStartRandDial_, "filterLfoStartRand" },
        { &filterLfoFadeInDial_, "filterLfoFadeIn" },
        { &tuneCoarseDial_,     "tuneCoarse" },
        { &tuneFineDial_,       "tuneFine" },
        { &pitchBendRangeDial_, "pitchBendRange" },
        { &pitchLfoRateDial_,   "pitchLfoRate" },
        { &pitchLfoDepthDial_,  "pitchLfoDepth" },
        { &preloadDial_,        "preload" },
        { &masterVolumeDial_,   "masterVolume" },
        { &panDial_,            "pan" },
        { &reverbMixDial_,      "reverbMix" },
        { &delayTimeDial_,      "delayTime" },
        { &delayFeedbackDial_,  "delayFeedback" },
        { &delayMixDial_,       "delayMix" },
        { &dcFilterCutoffDial_, "dcFilterCutoff" },
        { &autopanRateDial_,    "autopanRate" },
        { &autopanDepthDial_,   "autopanDepth" },
        { &saturationDriveDial_,"saturationDrive" },
        { &fadeInDial_,         "fadeIn" },
        { &fadeOutDial_,        "fadeOut" },
    };

    for (auto& [dial, paramId] : allDials_)
    {
        dial->onCcClearedByUser = [this, paramId]
        {
            processor_.clearCcMappingByParam(paramId);
        };
    }

    auto addToSampleControls = [this](juce::Component& c)
    {
        sampleControlsContent_.addAndMakeVisible(c);
    };

    addToSampleControls(playbackStartDial_);
    addToSampleControls(playbackEndDial_);
    addToSampleControls(loopStartDial_);
    addToSampleControls(loopEndDial_);
    addToSampleControls(loopCrossfadeDial_);
    addToSampleControls(playbackModeLabel_);
    addToSampleControls(playbackModeGateButton_);
    addToSampleControls(playbackModeOneShotButton_);
    addToSampleControls(playbackModeLoopButton_);
    addToSampleControls(monoToggle_);
    addToSampleControls(legatoToggle_);
    addToSampleControls(reverseToggle_);
    addToSampleControls(velocityCurveLabel_);
    addToSampleControls(velocityCurveCombo_);
    addToSampleControls(glideDial_);
    addToSampleControls(polyphonyDial_);
    addToSampleControls(rootNoteLabel_);
    addToSampleControls(rootNoteCombo_);
    addToSampleControls(tuneCoarseDial_);
    addToSampleControls(tuneFineDial_);
    addToSampleControls(pitchBendRangeDial_);
    addToSampleControls(pitchLfoRateDial_);
    addToSampleControls(pitchLfoDepthDial_);
    addToSampleControls(ampAttackDial_);
    addToSampleControls(ampDecayDial_);
    addToSampleControls(ampSustainDial_);
    addToSampleControls(ampReleaseDial_);
    addToSampleControls(ampLfoRateDial_);
    addToSampleControls(ampLfoDepthDial_);
    addToSampleControls(ampLfoShapeLabel_);
    addToSampleControls(ampLfoShapeCombo_);
    addToSampleControls(ampEnvelopeGraph_);
    addToSampleControls(filterCutoffDial_);
    addToSampleControls(filterResDial_);
    addToSampleControls(filterEnvAmtDial_);
    addToSampleControls(filterTypeLabel_);
    addToSampleControls(filterTypeCombo_);
    addToSampleControls(filterResponseGraph_);
    addToSampleControls(filterAttackDial_);
    addToSampleControls(filterDecayDial_);
    addToSampleControls(filterSustainDial_);
    addToSampleControls(filterReleaseDial_);
    addToSampleControls(filterEnvelopeGraph_);
    addToSampleControls(filterKeytrackDial_);
    addToSampleControls(filterVelDial_);
    addToSampleControls(filterLfoRateDial_);
    addToSampleControls(filterLfoRateKeyDial_);
    addToSampleControls(filterLfoAmtDial_);
    addToSampleControls(filterLfoAmtKeyDial_);
    addToSampleControls(filterLfoStartPhaseDial_);
    addToSampleControls(filterLfoStartRandDial_);
    addToSampleControls(filterLfoFadeInDial_);
    addToSampleControls(filterLfoShapeLabel_);
    addToSampleControls(filterLfoShapeCombo_);
    addToSampleControls(filterLfoRetriggerToggle_);
    addToSampleControls(filterLfoTempoSyncToggle_);
    addToSampleControls(filterLfoRateKeySyncToggle_);
    addToSampleControls(filterLfoKeytrackLinearToggle_);
    addToSampleControls(filterLfoUnipolarToggle_);
    addToSampleControls(filterLfoDivisionLabel_);
    addToSampleControls(filterLfoDivisionCombo_);
    addToSampleControls(filterKeytrackSnapLabel_);
    addToSampleControls(filterKeytrackSnapCombo_);
    addToSampleControls(fadeInDial_);
    addToSampleControls(fadeOutDial_);
    addToSampleControls(qualityLabel_);
    addToSampleControls(qualityCpuButton_);
    addToSampleControls(qualityFidelityButton_);
    addToSampleControls(qualityUltraButton_);
    addToSampleControls(preloadDial_);
    addToSampleControls(masterVolumeDial_);
    addToSampleControls(panDial_);
    addToSampleControls(outputLevelMeter_);
    addToSampleControls(reverbMixDial_);
    addToSampleControls(delayTimeDial_);
    addToSampleControls(delayFeedbackDial_);
    addToSampleControls(delayMixDial_);
    addToSampleControls(delayTempoSyncToggle_);
    addToSampleControls(dcFilterEnabledToggle_);
    addToSampleControls(dcFilterCutoffDial_);
    addToSampleControls(autopanRateDial_);
    addToSampleControls(autopanDepthDial_);
    addToSampleControls(saturationDriveDial_);
    addToSampleControls(saturationModeCombo_);
    addToSampleControls(diagnosticsLabel_);

    setupTooltips();
    rebuildVisibleSampleList();

    const auto restoredSampleRoot = processor_.getSampleBrowserRootFolder();
    if (restoredSampleRoot.isNotEmpty())
    {
        const juce::File restoredFolder(restoredSampleRoot);
        if (restoredFolder.isDirectory())
            scanSampleRootFolder(restoredFolder);
        else
            sampleBrowserRootLabel_.setText("< Select Folder >", juce::dontSendNotification);
    }

    updateTabVisibility();
    refreshUI();
    syncCcMappingsFromProcessor();
    startTimerHz(60);
}

AudiocityAudioProcessorEditor::~AudiocityAudioProcessorEditor()
{
    stopTimer();
    playerKeyboardState_.removeListener(this);
    tabBar_.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void AudiocityAudioProcessorEditor::handleNoteOn(juce::MidiKeyboardState* source,
                                                 const int midiChannel,
                                                 const int midiNoteNumber,
                                                 const float velocity)
{
    juce::ignoreUnused(source, midiChannel);
    processor_.enqueueUiMidiNoteOn(midiNoteNumber,
        juce::jlimit(1, 127, static_cast<int>(std::round(velocity * 127.0f))));
}

void AudiocityAudioProcessorEditor::handleNoteOff(juce::MidiKeyboardState* source,
                                                  const int midiChannel,
                                                  const int midiNoteNumber,
                                                  const float velocity)
{
    juce::ignoreUnused(source, midiChannel, velocity);
    processor_.enqueueUiMidiNoteOff(midiNoteNumber);
}

// ─── Timer: poll CC FIFO ───────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::timerCallback()
{
    updateGeneratePreviewButtonText();

    processor_.setWaveformViewRange(waveformView_.getViewStartSample(), waveformView_.getViewSampleCount());

    waveformView_.setVoicePlaybackPositions(processor_.getVoicePlaybackPositions());

    const auto outputPeaks = processor_.consumeOutputPeakLevels();
    outputLevelMeter_.pushLevels(outputPeaks.left, outputPeaks.right);

    for (int i = 0; i < kPlayerPadCount; ++i)
    {
        auto& ticks = playerPadPendingOffTicks_[static_cast<std::size_t>(i)];
        if (ticks <= 0)
            continue;

        --ticks;
        if (ticks == 0)
            processor_.enqueueUiMidiNoteOff(playerPadPendingOffNotes_[static_cast<std::size_t>(i)]);
    }

    // ── Process deferred drag-and-drop (safe: outside OLE modal loop) ──
    if (hasPendingDrop_)
    {
        DBG("[DnD] timerCallback: processing pending drop");
        hasPendingDrop_ = false;
        auto droppedFiles = std::move(pendingDropFiles_);
        pendingDropFiles_.clear();

        for (const auto& path : droppedFiles)
        {
            auto normalizedPath = path.trim();
            DBG("[DnD]   raw path: \"" + normalizedPath + "\"");

            // Normalise file:// URIs to a local path (string ops first, URL fallback)
            if (normalizedPath.startsWithIgnoreCase("file:///"))
                normalizedPath = normalizedPath.substring(8).replace("/", "\\");
            else if (normalizedPath.startsWithIgnoreCase("file://"))
                normalizedPath = normalizedPath.substring(7).replace("/", "\\");

            // Percent-decode common URL-encoded chars (%20 → space, etc.)
            normalizedPath = juce::URL::removeEscapeChars(normalizedPath);

            DBG("[DnD]   normalized: \"" + normalizedPath + "\"");

            const juce::File dropped(normalizedPath);
            DBG("[DnD]   existsAsFile: " + juce::String(dropped.existsAsFile() ? "yes" : "no"));
            if (!dropped.existsAsFile())
                continue;

            const auto ext = dropped.getFileExtension().toLowerCase();
            DBG("[DnD]   ext: \"" + ext + "\"");
            const auto rexSupported = processor_.isRexRuntimeAvailable();
            if (ext == ".wav" || ext == ".aiff" || ext == ".aif"
                || (rexSupported && (ext == ".rex" || ext == ".rx2")))
            {
                DBG("[DnD]   loading sample...");
                if (processor_.loadSampleFromFile(dropped))
                {
                    DBG("[DnD]   load succeeded, refreshing UI");
                    refreshUI(true);
                }
                else
                {
                    DBG("[DnD]   load FAILED");
                }
                break;
            }
        }
        DBG("[DnD] timerCallback: pending drop processing complete");
    }

    const auto selectedTab = tabBar_.getCurrentTabIndex();
    if (selectedTab != currentTabIndex_)
    {
        currentTabIndex_ = selectedTab;
        processor_.setEditorTabIndex(currentTabIndex_);
        updateTabVisibility();
        resized();
        repaint();

        if (currentTabIndex_ == 1)
            sampleBrowserListBox_.grabKeyboardFocus();
    }

    AudiocityAudioProcessor::CcEvent event{};
    while (processor_.popCcEvent(event))
    {
        // Check if any dial is armed for CC learn
        for (auto& [dial, paramId] : allDials_)
        {
            if (dial->isCcLearnArmed())
            {
                dial->assignCc(event.ccNumber);
                processor_.setCcMapping(event.ccNumber, paramId);
                break;
            }
        }

        // Route mapped CC to the correct dial
        const auto mappedParam = processor_.getParamForCc(event.ccNumber);
        if (mappedParam.isNotEmpty())
        {
            for (auto& [dial, paramId] : allDials_)
            {
                if (paramId == mappedParam)
                {
                    dial->handleCcValue(event.value);
                    break;
                }
            }
        }
    }

    if (currentTabIndex_ == 1)
    {
        sampleBrowserPreviewLabel_.setText(
            processor_.isSamplePreviewPlaying() ? "Previewing..." : juce::String{},
            juce::dontSendNotification);
    }

    syncAutomatedControlsFromProcessor();
}

void AudiocityAudioProcessorEditor::syncAutomatedControlsFromProcessor()
{
    if (currentTabIndex_ != 0)
        return;

    const auto playbackStart = static_cast<double>(processor_.getSampleWindowStart());
    const auto playbackEnd = static_cast<double>(processor_.getSampleWindowEnd());
    const auto loopStart = static_cast<double>(processor_.getLoopStart());
    const auto loopEnd = static_cast<double>(processor_.getLoopEnd());
    const auto loopXfade = static_cast<double>(processor_.getLoopCrossfadeSamples());

    if (std::abs(playbackStartDial_.getValue() - playbackStart) > 0.5)
    {
        playbackStartDial_.setValue(playbackStart, juce::dontSendNotification);
    }
    if (std::abs(playbackEndDial_.getValue() - playbackEnd) > 0.5)
    {
        playbackEndDial_.setValue(playbackEnd, juce::dontSendNotification);
    }
    if (std::abs(loopStartDial_.getValue() - loopStart) > 0.5)
    {
        loopStartDial_.setValue(loopStart, juce::dontSendNotification);
    }
    if (std::abs(loopEndDial_.getValue() - loopEnd) > 0.5)
    {
        loopEndDial_.setValue(loopEnd, juce::dontSendNotification);
    }
    if (std::abs(loopCrossfadeDial_.getValue() - loopXfade) > 0.5)
        loopCrossfadeDial_.setValue(loopXfade, juce::dontSendNotification);

    const auto rootId = processor_.getRootMidiNote() + 1;
    const bool isEditingRootNote = rootNoteCombo_.hasKeyboardFocus(true) || rootNoteCombo_.isPopupActive();
    if (!isEditingRootNote && rootNoteCombo_.getSelectedId() != rootId)
        rootNoteCombo_.setSelectedId(rootId, juce::dontSendNotification);
    tuneCoarseDial_.setValue(processor_.getCoarseTuneSemitones(), juce::dontSendNotification);
    tuneFineDial_.setValue(processor_.getFineTuneCents(), juce::dontSendNotification);
    pitchBendRangeDial_.setValue(processor_.getPitchBendRangeSemitones(), juce::dontSendNotification);
    const auto pitchLfo = processor_.getPitchLfoSettings();
    pitchLfoRateDial_.setValue(pitchLfo.rateHz, juce::dontSendNotification);
    pitchLfoDepthDial_.setValue(pitchLfo.depthCents, juce::dontSendNotification);

    const auto playbackMode = processor_.getPlaybackMode();
    playbackModeGateButton_.setToggleState(playbackMode == AudiocityAudioProcessor::PlaybackMode::gate,
        juce::dontSendNotification);
    playbackModeOneShotButton_.setToggleState(playbackMode == AudiocityAudioProcessor::PlaybackMode::oneShot,
        juce::dontSendNotification);
    playbackModeLoopButton_.setToggleState(playbackMode == AudiocityAudioProcessor::PlaybackMode::loop,
        juce::dontSendNotification);

    monoToggle_.setToggleState(processor_.getMonoMode(), juce::dontSendNotification);
    legatoToggle_.setToggleState(processor_.getLegatoMode(), juce::dontSendNotification);
    legatoToggle_.setEnabled(processor_.getMonoMode());
    reverseToggle_.setToggleState(processor_.getReversePlayback(), juce::dontSendNotification);

    glideDial_.setValue(processor_.getGlideSeconds() * 1000.0f, juce::dontSendNotification);
    polyphonyDial_.setValue(static_cast<double>(processor_.getPolyphonyLimit()), juce::dontSendNotification);
    fadeInDial_.setValue(processor_.getFadeInSamples(), juce::dontSendNotification);
    fadeOutDial_.setValue(processor_.getFadeOutSamples(), juce::dontSendNotification);

    const auto amp = processor_.getAmpEnvelope();
    ampAttackDial_.setValue(amp.attackSeconds * 1000.0f, juce::dontSendNotification);
    ampDecayDial_.setValue(amp.decaySeconds * 1000.0f, juce::dontSendNotification);
    ampSustainDial_.setValue(amp.sustainLevel, juce::dontSendNotification);
    ampReleaseDial_.setValue(amp.releaseSeconds * 1000.0f, juce::dontSendNotification);
    const auto ampLfo = processor_.getAmpLfoSettings();
    ampLfoRateDial_.setValue(ampLfo.rateHz, juce::dontSendNotification);
    ampLfoDepthDial_.setValue(ampLfo.depth * 100.0f, juce::dontSendNotification);
    ampLfoShapeCombo_.setSelectedId(static_cast<int>(ampLfo.shape) + 1, juce::dontSendNotification);

    const auto filter = processor_.getFilterSettings();
    filterCutoffDial_.setValue(filter.baseCutoffHz, juce::dontSendNotification);
    filterResDial_.setValue(static_cast<double>(filter.resonance) * 100.0, juce::dontSendNotification);
    filterEnvAmtDial_.setValue(filter.envAmountHz, juce::dontSendNotification);
    filterKeytrackDial_.setValue(filter.keyTracking * 100.0f, juce::dontSendNotification);
    filterVelDial_.setValue(filter.velocityAmountHz, juce::dontSendNotification);
    filterLfoRateDial_.setValue(filter.lfoRateHz, juce::dontSendNotification);
    filterLfoRateKeyDial_.setValue(filter.lfoRateKeyTracking * 100.0f, juce::dontSendNotification);
    filterLfoAmtDial_.setValue(filter.lfoAmountHz, juce::dontSendNotification);
    filterLfoAmtKeyDial_.setValue(filter.lfoAmountKeyTracking * 100.0f, juce::dontSendNotification);
    filterLfoStartPhaseDial_.setValue(filter.lfoStartPhaseDegrees, juce::dontSendNotification);
    filterLfoStartRandDial_.setValue(filter.lfoStartPhaseRandomDegrees, juce::dontSendNotification);
    filterLfoFadeInDial_.setValue(filter.lfoFadeInMs, juce::dontSendNotification);
    filterLfoShapeCombo_.setSelectedId(lfoShapeToComboId(filter.lfoShape), juce::dontSendNotification);
    filterLfoRetriggerToggle_.setToggleState(filter.lfoRetrigger, juce::dontSendNotification);
    filterLfoTempoSyncToggle_.setToggleState(filter.lfoTempoSync, juce::dontSendNotification);
    filterLfoRateKeySyncToggle_.setToggleState(filter.lfoRateKeytrackInTempoSync, juce::dontSendNotification);
    filterLfoDivisionCombo_.setSelectedId(filter.lfoSyncDivision + 1, juce::dontSendNotification);
    filterLfoRateDial_.setEnabled(!filter.lfoTempoSync);
    filterLfoRateKeySyncToggle_.setEnabled(filter.lfoTempoSync);
    filterLfoDivisionLabel_.setEnabled(filter.lfoTempoSync);
    filterLfoDivisionCombo_.setEnabled(filter.lfoTempoSync);
    filterTypeCombo_.setSelectedId(filterModeToComboId(filter.mode), juce::dontSendNotification);

    const auto filterEnv = processor_.getFilterEnvelope();
    filterAttackDial_.setValue(filterEnv.attackSeconds * 1000.0f, juce::dontSendNotification);
    filterDecayDial_.setValue(filterEnv.decaySeconds * 1000.0f, juce::dontSendNotification);
    filterSustainDial_.setValue(filterEnv.sustainLevel, juce::dontSendNotification);
    filterReleaseDial_.setValue(filterEnv.releaseSeconds * 1000.0f, juce::dontSendNotification);

    const auto velCurve = processor_.getVelocityCurve();
    const bool isEditingVelocityCurve = velocityCurveCombo_.hasKeyboardFocus(true) || velocityCurveCombo_.isPopupActive();
    if (!isEditingVelocityCurve)
    {
        velocityCurveCombo_.setSelectedId(
            velCurve == AudiocityAudioProcessor::VelocityCurve::soft ? 2
                : (velCurve == AudiocityAudioProcessor::VelocityCurve::hard ? 3 : 1),
            juce::dontSendNotification);
    }

    qualityCpuButton_.setToggleState(
        processor_.getQualityTier() == AudiocityAudioProcessor::QualityTier::cpu,
        juce::dontSendNotification);
    qualityFidelityButton_.setToggleState(
        processor_.getQualityTier() == AudiocityAudioProcessor::QualityTier::fidelity,
        juce::dontSendNotification);
    qualityUltraButton_.setToggleState(
        processor_.getQualityTier() == AudiocityAudioProcessor::QualityTier::ultra,
        juce::dontSendNotification);
    masterVolumeDial_.setValue(processor_.getMasterVolume() * 100.0f, juce::dontSendNotification);
    panDial_.setValue(processor_.getPan() * 100.0f, juce::dontSendNotification);
    reverbMixDial_.setValue(processor_.getReverbMix() * 100.0f, juce::dontSendNotification);
    const auto delay = processor_.getDelaySettings();
    delayTimeDial_.setValue(delay.timeMs, juce::dontSendNotification);
    delayFeedbackDial_.setValue(delay.feedback * 100.0f, juce::dontSendNotification);
    delayMixDial_.setValue(delay.mix * 100.0f, juce::dontSendNotification);
    delayTempoSyncToggle_.setToggleState(delay.tempoSync, juce::dontSendNotification);
    const auto dcFilter = processor_.getDcFilterSettings();
    dcFilterEnabledToggle_.setToggleState(dcFilter.enabled, juce::dontSendNotification);
    dcFilterCutoffDial_.setValue(dcFilter.cutoffHz, juce::dontSendNotification);
    const auto autopan = processor_.getAutopanSettings();
    autopanRateDial_.setValue(autopan.rateHz, juce::dontSendNotification);
    autopanDepthDial_.setValue(autopan.depth * 100.0f, juce::dontSendNotification);
    const auto saturation = processor_.getSaturationSettings();
    saturationDriveDial_.setValue(saturation.drive * 100.0f, juce::dontSendNotification);
    saturationModeCombo_.setSelectedId(static_cast<int>(saturation.mode) + 1, juce::dontSendNotification);

    updateDiagnosticsStatusText();
}

void AudiocityAudioProcessorEditor::updateTabVisibility()
{
    const bool showSampleTab = (currentTabIndex_ == 0);
    const bool showLibraryTab = (currentTabIndex_ == 1);
    const bool showPlayerTab = (currentTabIndex_ == 2);
    const bool showGenerateTab = (currentTabIndex_ == 3);

    sampleBrowserRootLabel_.setVisible(showLibraryTab);
    sampleBrowserChooseRootButton_.setVisible(showLibraryTab);
    sampleBrowserFilterEditor_.setVisible(showLibraryTab);
    sampleBrowserSortCombo_.setVisible(showLibraryTab);
    sampleBrowserListBox_.setVisible(showLibraryTab);
    sampleBrowserCountLabel_.setVisible(showLibraryTab);
    sampleBrowserPreviewLabel_.setVisible(showLibraryTab);

    samplePathLabel_.setVisible(showSampleTab);
    loadButton_.setVisible(showSampleTab);
    waveformDisplayModeCombo_.setVisible(showSampleTab);
    sampleControlsViewport_.setVisible(showSampleTab);
    rootNoteLabel_.setVisible(showSampleTab);
    rootNoteCombo_.setVisible(showSampleTab);
    tuneCoarseDial_.setVisible(showSampleTab);
    tuneFineDial_.setVisible(showSampleTab);
    pitchBendRangeDial_.setVisible(showSampleTab);
    pitchLfoRateDial_.setVisible(showSampleTab);
    pitchLfoDepthDial_.setVisible(showSampleTab);
    waveformView_.setVisible(showSampleTab);
    playbackModeLabel_.setVisible(showSampleTab);
    playbackModeGateButton_.setVisible(showSampleTab);
    playbackModeOneShotButton_.setVisible(showSampleTab);
    playbackModeLoopButton_.setVisible(showSampleTab);
    reverseToggle_.setVisible(showSampleTab);
    playbackStartDial_.setVisible(showSampleTab);
    playbackEndDial_.setVisible(showSampleTab);
    loopStartDial_.setVisible(showSampleTab);
    loopEndDial_.setVisible(showSampleTab);
    loopCrossfadeDial_.setVisible(showSampleTab);
    monoToggle_.setVisible(showSampleTab);
    legatoToggle_.setVisible(showSampleTab);
    velocityCurveLabel_.setVisible(showSampleTab);
    velocityCurveCombo_.setVisible(showSampleTab);
    glideDial_.setVisible(showSampleTab);
    polyphonyDial_.setVisible(showSampleTab);
    ampAttackDial_.setVisible(showSampleTab);
    ampDecayDial_.setVisible(showSampleTab);
    ampSustainDial_.setVisible(showSampleTab);
    ampReleaseDial_.setVisible(showSampleTab);
    ampLfoRateDial_.setVisible(showSampleTab);
    ampLfoDepthDial_.setVisible(showSampleTab);
    ampLfoShapeLabel_.setVisible(showSampleTab);
    ampLfoShapeCombo_.setVisible(showSampleTab);
    filterCutoffDial_.setVisible(showSampleTab);
    filterResDial_.setVisible(showSampleTab);
    filterEnvAmtDial_.setVisible(showSampleTab);
    filterTypeLabel_.setVisible(showSampleTab);
    filterTypeCombo_.setVisible(showSampleTab);
    filterAttackDial_.setVisible(showSampleTab);
    filterDecayDial_.setVisible(showSampleTab);
    filterSustainDial_.setVisible(showSampleTab);
    filterReleaseDial_.setVisible(showSampleTab);
    filterKeytrackDial_.setVisible(showSampleTab);
    filterVelDial_.setVisible(showSampleTab);
    filterLfoRateDial_.setVisible(showSampleTab);
    filterLfoRateKeyDial_.setVisible(showSampleTab);
    filterLfoAmtDial_.setVisible(showSampleTab);
    filterLfoAmtKeyDial_.setVisible(showSampleTab);
    filterLfoStartPhaseDial_.setVisible(showSampleTab);
    filterLfoStartRandDial_.setVisible(showSampleTab);
    filterLfoFadeInDial_.setVisible(showSampleTab);
    filterLfoShapeLabel_.setVisible(showSampleTab);
    filterLfoShapeCombo_.setVisible(showSampleTab);
    filterLfoRetriggerToggle_.setVisible(showSampleTab);
    filterLfoTempoSyncToggle_.setVisible(showSampleTab);
    filterLfoRateKeySyncToggle_.setVisible(showSampleTab);
    filterLfoKeytrackLinearToggle_.setVisible(showSampleTab);
    filterLfoUnipolarToggle_.setVisible(showSampleTab);
    filterLfoDivisionLabel_.setVisible(showSampleTab);
    filterLfoDivisionCombo_.setVisible(showSampleTab);
    filterKeytrackSnapLabel_.setVisible(showSampleTab);
    filterKeytrackSnapCombo_.setVisible(showSampleTab);
    fadeInDial_.setVisible(showSampleTab);
    fadeOutDial_.setVisible(showSampleTab);
    qualityLabel_.setVisible(showSampleTab);
    qualityCpuButton_.setVisible(showSampleTab);
    qualityFidelityButton_.setVisible(showSampleTab);
    qualityUltraButton_.setVisible(showSampleTab);
    preloadDial_.setVisible(showSampleTab);
    masterVolumeDial_.setVisible(showSampleTab);
    panDial_.setVisible(showSampleTab);
    outputLevelMeter_.setVisible(showSampleTab);
    reverbMixDial_.setVisible(showSampleTab);
    delayTimeDial_.setVisible(showSampleTab);
    delayFeedbackDial_.setVisible(showSampleTab);
    delayMixDial_.setVisible(showSampleTab);
    delayTempoSyncToggle_.setVisible(showSampleTab);
    dcFilterEnabledToggle_.setVisible(showSampleTab);
    dcFilterCutoffDial_.setVisible(showSampleTab);
    autopanRateDial_.setVisible(showSampleTab);
    autopanDepthDial_.setVisible(showSampleTab);
    saturationDriveDial_.setVisible(showSampleTab);
    saturationModeCombo_.setVisible(showSampleTab);
    diagnosticsLabel_.setVisible(showSampleTab);

    playerKeyboardLabel_.setVisible(showPlayerTab);
    playerKeyboardScrollLeft_.setVisible(showPlayerTab);
    playerKeyboardScrollRight_.setVisible(showPlayerTab);
    playerKeyboardViewport_.setVisible(showPlayerTab);
    playerPadsLabel_.setVisible(showPlayerTab);
    for (int i = 0; i < kPlayerPadCount; ++i)
    {
        playerPadButtons_[static_cast<std::size_t>(i)].setVisible(showPlayerTab);
        playerPadAssignButtons_[static_cast<std::size_t>(i)].setVisible(showPlayerTab);
    }

    generateWaveformView_.setVisible(showGenerateTab);
    generateSineButton_.setVisible(showGenerateTab);
    generateRampButton_.setVisible(showGenerateTab);
    generateSquareButton_.setVisible(showGenerateTab);
    generateSawtoothButton_.setVisible(showGenerateTab);
    generateTriangleButton_.setVisible(showGenerateTab);
    generatePulseButton_.setVisible(showGenerateTab);
    generateRandomButton_.setVisible(showGenerateTab);
    generateNoiseButton_.setVisible(showGenerateTab);
    generateSamplesLabel_.setVisible(showGenerateTab);
    generateSamplesCombo_.setVisible(showGenerateTab);
    generateBitDepthLabel_.setVisible(showGenerateTab);
    generateBitDepthCombo_.setVisible(showGenerateTab);
    generateSketchSmoothingLabel_.setVisible(showGenerateTab);
    generateSketchSmoothingCombo_.setVisible(showGenerateTab);
    generatePulseWidthLabel_.setVisible(showGenerateTab);
    generatePulseWidthSlider_.setVisible(showGenerateTab);
    generatePreviewButton_.setVisible(showGenerateTab);
    generateFrequencyLabel_.setVisible(showGenerateTab);
    generateFrequencyCombo_.setVisible(showGenerateTab);
    generateLoadAsSampleButton_.setVisible(showGenerateTab);
}

int AudiocityAudioProcessorEditor::getNumRows()
{
    return static_cast<int>(visibleSampleEntryIndices_.size());
}

void AudiocityAudioProcessorEditor::paintListBoxItem(
    const int rowNumber, juce::Graphics& g, const int width, const int height, const bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int>(visibleSampleEntryIndices_.size()))
        return;

    const auto& entry = allSampleEntries_[static_cast<std::size_t>(visibleSampleEntryIndices_[static_cast<std::size_t>(rowNumber)])];
    const auto rowBounds = juce::Rectangle<int>(0, 0, width, height).reduced(2, 1);

    if (rowIsSelected)
    {
        g.setColour(juce::Colour(0xff2d3f66));
        g.fillRoundedRectangle(rowBounds.toFloat(), 4.0f);
    }
    else
    {
        g.setColour((rowNumber % 2 == 0) ? juce::Colour(0x22252538) : juce::Colour(0x16252538));
        g.fillRoundedRectangle(rowBounds.toFloat(), 4.0f);
    }

    auto content = rowBounds.reduced(6, 4);
    auto thumbBounds = content.removeFromLeft(120).withTrimmedBottom(1);
    content.removeFromLeft(8);

    g.setColour(juce::Colour(0xff1f2f4d));
    g.fillRoundedRectangle(thumbBounds.toFloat(), 4.0f);
    g.setColour(juce::Colour(0xff3a5a8a));
    g.drawRoundedRectangle(thumbBounds.toFloat().reduced(0.5f), 4.0f, 1.0f);

    if (!entry.previewPeaks.empty())
    {
        const auto centerY = static_cast<float>(thumbBounds.getCentreY());
        const auto peakCount = static_cast<int>(entry.previewPeaks.size());
        const auto pixelCount = juce::jmax(1, thumbBounds.getWidth() - 2);

        std::vector<float> topYs(static_cast<std::size_t>(pixelCount + 1), centerY);
        std::vector<float> bottomYs(static_cast<std::size_t>(pixelCount + 1), centerY);
        juce::Path topPath;
        juce::Path bottomPath;

        for (int px = 0; px <= pixelCount; ++px)
        {
            const auto t = static_cast<float>(px) / static_cast<float>(pixelCount);
            const auto x = static_cast<float>(thumbBounds.getX()) + t * static_cast<float>(thumbBounds.getWidth() - 1);

            const auto peakPos = t * static_cast<float>(juce::jmax(0, peakCount - 1));
            const auto i0 = juce::jlimit(0, peakCount - 1, static_cast<int>(std::floor(peakPos)));
            const auto i1 = juce::jlimit(0, peakCount - 1, i0 + 1);
            const auto frac = peakPos - static_cast<float>(i0);

            const auto a0 = entry.previewPeaks[static_cast<std::size_t>(i0)];
            const auto a1 = entry.previewPeaks[static_cast<std::size_t>(i1)];
            const auto ampNorm = juce::jlimit(0.0f, 1.0f, a0 + (a1 - a0) * frac);
            const auto amp = ampNorm * (thumbBounds.getHeight() * 0.44f);

            const auto topY = centerY - amp;
            const auto bottomY = centerY + amp;
            topYs[static_cast<std::size_t>(px)] = topY;
            bottomYs[static_cast<std::size_t>(px)] = bottomY;

            if (px == 0)
            {
                topPath.startNewSubPath(x, topY);
                bottomPath.startNewSubPath(x, bottomY);
            }
            else
            {
                topPath.lineTo(x, topY);
                bottomPath.lineTo(x, bottomY);
            }
        }

        juce::Path fillPath;
        fillPath.startNewSubPath(static_cast<float>(thumbBounds.getX()), topYs.front());
        for (int px = 1; px <= pixelCount; ++px)
        {
            const auto t = static_cast<float>(px) / static_cast<float>(pixelCount);
            const auto x = static_cast<float>(thumbBounds.getX()) + t * static_cast<float>(thumbBounds.getWidth() - 1);
            fillPath.lineTo(x, topYs[static_cast<std::size_t>(px)]);
        }
        for (int px = pixelCount; px >= 0; --px)
        {
            const auto t = static_cast<float>(px) / static_cast<float>(pixelCount);
            const auto x = static_cast<float>(thumbBounds.getX()) + t * static_cast<float>(thumbBounds.getWidth() - 1);
            fillPath.lineTo(x, bottomYs[static_cast<std::size_t>(px)]);
        }
        fillPath.closeSubPath();

        juce::ColourGradient fillGradient(
            juce::Colour(0xff61d9ff).withAlpha(0.35f), static_cast<float>(thumbBounds.getCentreX()), static_cast<float>(thumbBounds.getY()),
            juce::Colour(0xff61d9ff).withAlpha(0.10f), static_cast<float>(thumbBounds.getCentreX()), static_cast<float>(thumbBounds.getBottom()),
            false);
        g.setGradientFill(fillGradient);
        g.fillPath(fillPath);

        g.setColour(juce::Colour(0xff61d9ff).withAlpha(0.9f));
        g.strokePath(topPath, juce::PathStrokeType(1.15f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colour(0xff61d9ff).withAlpha(0.65f));
        g.strokePath(bottomPath, juce::PathStrokeType(1.15f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    auto firstLine = content.removeFromTop(17);
    auto pathArea = firstLine;
    if (entry.loopFormatBadge.isNotEmpty())
    {
        const auto badgeWidth = entry.loopFormatBadge == "Apple Loop" ? 84 : 66;
        auto badgeArea = pathArea.removeFromRight(badgeWidth);

        g.setColour(entry.loopFormatBadge == "Apple Loop" ? juce::Colour(0xff5b4b8a) : juce::Colour(0xff4b6b2a));
        g.fillRoundedRectangle(badgeArea.toFloat(), 4.0f);
        g.setColour(juce::Colour(0xffdfe6ff));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText(entry.loopFormatBadge, badgeArea, juce::Justification::centred, false);

        pathArea.removeFromRight(6);
    }

    const auto pathWidth = juce::jmax(140, pathArea.getWidth() / 2);
    auto fileArea = pathArea.removeFromLeft(juce::jmax(40, pathArea.getWidth() - pathWidth - 6));

    g.setColour(juce::Colour(0xffe5e5ef));
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText(entry.fileName, fileArea, juce::Justification::centredLeft, true);

    g.setColour(juce::Colour(0xffa5a5b8));
    g.setFont(juce::Font(juce::FontOptions(10.5f)));
    g.drawText(entry.relativePath, pathArea, juce::Justification::centredRight, true);

    auto detailsLine = entry.metadataLine;
    if (entry.loopMetadataLine.isNotEmpty())
        detailsLine += "  |  " + entry.loopMetadataLine;

    g.setColour(juce::Colour(0xffc7c7d8));
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText(detailsLine, content.removeFromTop(15), juce::Justification::centredLeft, true);

}

void AudiocityAudioProcessorEditor::listBoxItemClicked(const int row, const juce::MouseEvent& event)
{
    if (event.mods.isShiftDown())
    {
        loadSampleFromBrowserRow(row);
        return;
    }

    previewSampleFromBrowserRow(row);
}

void AudiocityAudioProcessorEditor::listBoxItemDoubleClicked(const int row, const juce::MouseEvent&)
{
    loadSampleFromBrowserRow(row);
}

void AudiocityAudioProcessorEditor::selectedRowsChanged(const int lastRowSelected)
{
    if (currentTabIndex_ != 1)
        return;

    previewSampleFromBrowserRow(lastRowSelected, false);
}

void AudiocityAudioProcessorEditor::returnKeyPressed(const int lastRowSelected)
{
    if (currentTabIndex_ != 1)
        return;

    loadSampleFromBrowserRow(lastRowSelected);
}

void AudiocityAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    isResizingSampleList_ = false;
}

void AudiocityAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
}

void AudiocityAudioProcessorEditor::mouseUp(const juce::MouseEvent&)
{
    isResizingSampleList_ = false;
}

bool AudiocityAudioProcessorEditor::isSupportedSampleFile(const juce::File& file) const
{
    const auto ext = file.getFileExtension().toLowerCase();
    if (ext == ".wav" || ext == ".aif" || ext == ".aiff")
        return true;

    if (processor_.isRexRuntimeAvailable() && (ext == ".rex" || ext == ".rx2"))
        return true;

    return false;
}

void AudiocityAudioProcessorEditor::chooseSampleRootFolder()
{
    fileChooser_ = std::make_unique<juce::FileChooser>("Choose sample root folder", juce::File{});

    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
    fileChooser_->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        const auto selected = chooser.getResult();
        if (selected == juce::File{} || !selected.isDirectory())
            return;

        scanSampleRootFolder(selected);
    });
}

void AudiocityAudioProcessorEditor::scanSampleRootFolder(const juce::File& rootFolder)
{
    sampleRootFolderPath_ = rootFolder.getFullPathName();
    processor_.setSampleBrowserRootFolder(sampleRootFolderPath_);
    sampleBrowserRootLabel_.setText(sampleRootFolderPath_, juce::dontSendNotification);
    sampleBrowserCountLabel_.setText("Scanning...", juce::dontSendNotification);

    allSampleEntries_.clear();
    visibleSampleEntryIndices_.clear();
    sampleBrowserListBox_.updateContent();
    sampleBrowserListBox_.repaint();

    const auto scanGeneration = ++sampleScanGeneration_;
    auto safeThis = juce::Component::SafePointer<AudiocityAudioProcessorEditor>(this);

    std::thread([safeThis, rootFolder, scanGeneration]()
    {
        std::vector<SampleListEntry> batch;
        batch.reserve(24);

        auto flushBatchToUi = [safeThis, scanGeneration](std::vector<SampleListEntry>& batchToFlush)
        {
            if (batchToFlush.empty())
                return;

            auto uiBatch = std::move(batchToFlush);
            batchToFlush.clear();

            juce::MessageManager::callAsync([safeThis, scanGeneration, batch = std::move(uiBatch)]() mutable
            {
                if (safeThis == nullptr)
                    return;

                auto* self = safeThis.getComponent();
                if (scanGeneration != self->sampleScanGeneration_.load())
                    return;

                self->allSampleEntries_.insert(self->allSampleEntries_.end(),
                    std::make_move_iterator(batch.begin()),
                    std::make_move_iterator(batch.end()));
                self->rebuildVisibleSampleList();
            });
        };

        for (const auto& entry : juce::RangedDirectoryIterator(rootFolder, true, "*", juce::File::findFiles))
        {
            if (safeThis == nullptr)
                return;

            auto* self = safeThis.getComponent();
            if (scanGeneration != self->sampleScanGeneration_.load())
                return;

            const auto file = entry.getFile();
            if (!self->isSupportedSampleFile(file))
                continue;

            SampleListEntry item;
            item.file = file;
            item.relativePath = file.getRelativePathFrom(rootFolder).replaceCharacter('\\', '/');
            item.fileName = file.getFileName();
            item.fileNameLower = item.fileName.toLowerCase();
            item.relativePathLower = item.relativePath.toLowerCase();
            auto previewData = buildPreviewAndMetadata(file);
            item.previewPeaks = std::move(previewData.peaks);
            item.metadataLine = std::move(previewData.metadataLine);
            item.loopFormatBadge = std::move(previewData.loopFormatBadge);
            item.loopMetadataLine = std::move(previewData.loopMetadataLine);
            batch.push_back(std::move(item));

            if (batch.size() >= 24)
                flushBatchToUi(batch);
        }

        flushBatchToUi(batch);
    }).detach();
}

void AudiocityAudioProcessorEditor::rebuildVisibleSampleList()
{
    visibleSampleEntryIndices_.clear();

    const auto needle = sampleBrowserFilterEditor_.getText().trim().toLowerCase();
    for (int i = 0; i < static_cast<int>(allSampleEntries_.size()); ++i)
    {
        const auto& item = allSampleEntries_[static_cast<std::size_t>(i)];
        const auto matches = needle.isEmpty()
            || item.fileNameLower.contains(needle)
            || item.relativePathLower.contains(needle);

        if (matches)
            visibleSampleEntryIndices_.push_back(i);
    }

    const auto sortMode = sampleBrowserSortCombo_.getSelectedId();
    std::sort(visibleSampleEntryIndices_.begin(), visibleSampleEntryIndices_.end(),
        [this, sortMode](const int lhs, const int rhs)
        {
            const auto& a = allSampleEntries_[static_cast<std::size_t>(lhs)];
            const auto& b = allSampleEntries_[static_cast<std::size_t>(rhs)];

            if (sortMode == 2)
            {
                if (a.relativePathLower == b.relativePathLower)
                    return a.fileNameLower < b.fileNameLower;
                return a.relativePathLower < b.relativePathLower;
            }

            if (a.fileNameLower == b.fileNameLower)
                return a.relativePathLower < b.relativePathLower;
            return a.fileNameLower < b.fileNameLower;
        });

    sampleBrowserListBox_.updateContent();
    sampleBrowserListBox_.repaint();

    if (sampleRootFolderPath_.isEmpty())
        sampleBrowserCountLabel_.setText("No folder selected", juce::dontSendNotification);
    else
        sampleBrowserCountLabel_.setText(
            juce::String(visibleSampleEntryIndices_.size()) + " / " + juce::String(allSampleEntries_.size()) + " samples",
            juce::dontSendNotification);
}

void AudiocityAudioProcessorEditor::loadSampleFromBrowserRow(const int row)
{
    if (row < 0 || row >= static_cast<int>(visibleSampleEntryIndices_.size()))
        return;

    const auto sourceIndex = visibleSampleEntryIndices_[static_cast<std::size_t>(row)];
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(allSampleEntries_.size()))
        return;

    lastPreviewedBrowserSourceIndex_ = sourceIndex;

    const auto& file = allSampleEntries_[static_cast<std::size_t>(sourceIndex)].file;
    processor_.panicAllAudio();
    updateGeneratePreviewButtonText();
    if (processor_.loadSampleFromFile(file))
    {
        tabBar_.setCurrentTabIndex(0);
        currentTabIndex_ = 0;
        processor_.setEditorTabIndex(currentTabIndex_);
        updateTabVisibility();
        resized();
        repaint();
        refreshUI();
    }
}

void AudiocityAudioProcessorEditor::previewSampleFromBrowserRow(const int row, const bool forceRestart)
{
    if (row < 0 || row >= static_cast<int>(visibleSampleEntryIndices_.size()))
        return;

    const auto sourceIndex = visibleSampleEntryIndices_[static_cast<std::size_t>(row)];
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(allSampleEntries_.size()))
        return;

    if (!forceRestart && sourceIndex == lastPreviewedBrowserSourceIndex_)
        return;

    lastPreviewedBrowserSourceIndex_ = sourceIndex;

    const auto& file = allSampleEntries_[static_cast<std::size_t>(sourceIndex)].file;
    processor_.previewSampleFromFile(file);
    updateGeneratePreviewButtonText();
}

void AudiocityAudioProcessorEditor::paintSampleBrowserPane(
    juce::Graphics& g, const juce::Rectangle<int> browserArea) const
{
    g.setColour(juce::Colour(0xff252538));
    g.fillRoundedRectangle(browserArea.toFloat(), 6.0f);
    g.setColour(juce::Colour(0xff3a3a52));
    g.drawRoundedRectangle(browserArea.toFloat().reduced(0.5f), 6.0f, 1.0f);
}

void AudiocityAudioProcessorEditor::paintPlayerPane(juce::Graphics& g, juce::Rectangle<int> area) const
{
    area = area.reduced(8, 6);

    auto keyboardPanel = area.removeFromTop(juce::jlimit(120, 240, area.getWidth() / 5));
    auto padsPanel = area.withTrimmedTop(10);

    g.setColour(juce::Colour(0xff252538));
    g.fillRoundedRectangle(keyboardPanel.toFloat(), 6.0f);
    g.fillRoundedRectangle(padsPanel.toFloat(), 6.0f);

    g.setColour(juce::Colour(0xff3a3a52));
    g.drawRoundedRectangle(keyboardPanel.toFloat().reduced(0.5f), 6.0f, 1.0f);
    g.drawRoundedRectangle(padsPanel.toFloat().reduced(0.5f), 6.0f, 1.0f);
}

// ─── Layout ────────────────────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::resized()
{
    constexpr int kMargin   = 14;
    constexpr int kDial     = 78;
    constexpr int kDialH    = 96;
    constexpr int kGrpPadH  = 12;
    constexpr int kGrpPadV  = 8;
    constexpr int kGrpHdr   = 22;
    constexpr int kGrpGap   = 10;
    constexpr int kDialGap  = 6;
    constexpr int kModeStackW = 104;
    constexpr int kStackLabelH = 16;
    constexpr int kStackButtonH = 22;
    constexpr int kStackGap = 2;
    constexpr int kStackColGap = kDialGap + 8;
    constexpr int kRowH     = kGrpHdr + kGrpPadV + kDialH + kGrpPadV;  // 134
    constexpr int kTopBarH  = 34;

    auto content = getLocalBounds().reduced(kMargin);
    tabBar_.setBounds(content.removeFromTop(30));
    content.removeFromTop(8);
    auto area = content;

    groupBoxes_.clear();

    if (currentTabIndex_ == 1)
    {
        auto browserArea = area.reduced(8, 6);

        auto header = browserArea.removeFromTop(28);
        sampleBrowserChooseRootButton_.setBounds(header.removeFromRight(30));
        header.removeFromRight(6);
        sampleBrowserRootLabel_.setBounds(header);

        browserArea.removeFromTop(6);
        auto filterRow = browserArea.removeFromTop(28);
        auto filterWidth = juce::jmax(100, filterRow.getWidth() - 110);
        sampleBrowserFilterEditor_.setBounds(filterRow.removeFromLeft(filterWidth));
        filterRow.removeFromLeft(6);
        sampleBrowserSortCombo_.setBounds(filterRow.removeFromLeft(104));

        browserArea.removeFromTop(6);
        auto listArea = browserArea;
        auto statusRow = listArea.removeFromBottom(20);
        sampleBrowserCountLabel_.setBounds(statusRow.removeFromLeft(statusRow.getWidth() / 2));
        sampleBrowserPreviewLabel_.setBounds(statusRow);
        listArea.removeFromBottom(4);
        sampleBrowserListBox_.setBounds(listArea);
        return;
    }

    if (currentTabIndex_ == 2)
    {
        auto playerArea = area.reduced(8, 6);

        auto keyboardPanel = playerArea.removeFromTop(juce::jlimit(120, 240, playerArea.getWidth() / 5));
        keyboardPanel.reduce(10, 10);

        auto keyboardHeader = keyboardPanel.removeFromTop(26);
        playerKeyboardScrollRight_.setBounds(keyboardHeader.removeFromRight(28));
        keyboardHeader.removeFromRight(4);
        playerKeyboardScrollLeft_.setBounds(keyboardHeader.removeFromRight(28));
        keyboardHeader.removeFromRight(8);
        playerKeyboardLabel_.setBounds(keyboardHeader);

        keyboardPanel.removeFromTop(6);
        playerKeyboardViewport_.setBounds(keyboardPanel);
        updatePlayerKeyboardSizing();

        playerArea.removeFromTop(10);
        auto padsPanel = playerArea.reduced(10, 10);
        playerPadsLabel_.setBounds(padsPanel.removeFromTop(22));
        padsPanel.removeFromTop(6);

        constexpr int kPadCols = 4;
        const int kPadRows = (kPlayerPadCount + kPadCols - 1) / kPadCols;
        const int padGap = 8;
        const int padCellWidth = juce::jmax(80, (padsPanel.getWidth() - (kPadCols - 1) * padGap) / kPadCols);
        const int padCellHeight = juce::jmax(90, (padsPanel.getHeight() - (kPadRows - 1) * padGap) / kPadRows);

        for (int i = 0; i < kPlayerPadCount; ++i)
        {
            const int row = i / kPadCols;
            const int col = i % kPadCols;
            auto cell = juce::Rectangle<int>(
                padsPanel.getX() + col * (padCellWidth + padGap),
                padsPanel.getY() + row * (padCellHeight + padGap),
                padCellWidth,
                padCellHeight);

            playerPadButtons_[static_cast<std::size_t>(i)].setBounds(cell);

            constexpr int kAssignW = 28;
            constexpr int kAssignH = 20;
            constexpr int kAssignPad = 6;
            const auto assignBounds = juce::Rectangle<int>(
                cell.getRight() - kAssignW - kAssignPad,
                cell.getBottom() - kAssignH - kAssignPad,
                kAssignW,
                kAssignH);
            playerPadAssignButtons_[static_cast<std::size_t>(i)].setBounds(assignBounds);
            playerPadAssignButtons_[static_cast<std::size_t>(i)].toFront(false);
        }

        return;
    }

    if (currentTabIndex_ == 3)
    {
        auto genArea = area.reduced(8, 6);

        auto waveformArea = genArea.removeFromTop(juce::jmax(200, genArea.getHeight() / 2));
        generateWaveformView_.setBounds(waveformArea);
        genArea.removeFromTop(12);

        auto waveButtons = genArea.removeFromTop(32);
        constexpr int kBtnW = 88;
        constexpr int kBtnGap = 8;
        generateSineButton_.setBounds(waveButtons.removeFromLeft(kBtnW));
        waveButtons.removeFromLeft(kBtnGap);
        generateRampButton_.setBounds(waveButtons.removeFromLeft(kBtnW));
        waveButtons.removeFromLeft(kBtnGap);
        generateSquareButton_.setBounds(waveButtons.removeFromLeft(kBtnW));
        waveButtons.removeFromLeft(kBtnGap);
        generateSawtoothButton_.setBounds(waveButtons.removeFromLeft(kBtnW));
        waveButtons.removeFromLeft(kBtnGap);
        generateTriangleButton_.setBounds(waveButtons.removeFromLeft(kBtnW));
        waveButtons.removeFromLeft(kBtnGap);
        generatePulseButton_.setBounds(waveButtons.removeFromLeft(kBtnW));
        waveButtons.removeFromLeft(kBtnGap);
        generateRandomButton_.setBounds(waveButtons.removeFromLeft(kBtnW));
        waveButtons.removeFromLeft(kBtnGap);
        generateNoiseButton_.setBounds(waveButtons.removeFromLeft(kBtnW));

        genArea.removeFromTop(10);
        auto settingsRow = genArea.removeFromTop(32);
        generateSamplesLabel_.setBounds(settingsRow.removeFromLeft(64));
        generateSamplesCombo_.setBounds(settingsRow.removeFromLeft(98));
        settingsRow.removeFromLeft(16);
        generateBitDepthLabel_.setBounds(settingsRow.removeFromLeft(72));
        generateBitDepthCombo_.setBounds(settingsRow.removeFromLeft(96));
        settingsRow.removeFromLeft(16);
        generateSketchSmoothingLabel_.setBounds(settingsRow.removeFromLeft(58));
        generateSketchSmoothingCombo_.setBounds(settingsRow.removeFromLeft(100));
        settingsRow.removeFromLeft(16);
        generatePulseWidthLabel_.setBounds(settingsRow.removeFromLeft(86));
        generatePulseWidthSlider_.setBounds(settingsRow.removeFromLeft(172));

        genArea.removeFromTop(10);
        auto actionsRow = genArea.removeFromTop(32);
        generatePreviewButton_.setBounds(actionsRow.removeFromLeft(96));
        actionsRow.removeFromLeft(12);
        generateFrequencyLabel_.setBounds(actionsRow.removeFromLeft(72));
        generateFrequencyCombo_.setBounds(actionsRow.removeFromLeft(190));
        actionsRow.removeFromLeft(12);
        generateLoadAsSampleButton_.setBounds(actionsRow.removeFromLeft(160));

        return;
    }

    // ── Top bar: Load button + file path ──
    {
        auto topRow = area.removeFromTop(kTopBarH);
        loadButton_.setBounds(topRow.removeFromRight(32));
        topRow.removeFromRight(8);
        waveformDisplayModeCombo_.setBounds(topRow.removeFromRight(132));
        topRow.removeFromRight(8);
        samplePathLabel_.setBounds(topRow);
    }

    area.removeFromTop(kGrpGap);

    const auto waveformHeight = juce::jlimit(180, 320, area.getHeight() / 3);
    waveformView_.setBounds(area.removeFromTop(waveformHeight));
    area.removeFromTop(kGrpGap);
    sampleControlsViewport_.setBounds(area);

    const auto viewportWidth = juce::jmax(200, sampleControlsViewport_.getWidth() - sampleControlsViewport_.getScrollBarThickness() - 2);
    int scrollY = 0;
    groupBoxes_.clear();

    auto makeGroup = [&](const juce::String& title,
                         const int height) -> juce::Rectangle<int>
    {
        auto bounds = juce::Rectangle<int>(0, scrollY, viewportWidth, height);
        groupBoxes_.push_back({ title, bounds });
        scrollY += height + kGrpGap;
        return bounds.withTrimmedTop(kGrpHdr).reduced(kGrpPadH, kGrpPadV);
    };

    auto layoutThreeButtonStack = [kStackLabelH, kStackButtonH, kStackGap](juce::Rectangle<int> area,
                                                                             juce::Label& label,
                                                                             juce::Button& topButton,
                                                                             juce::Button& middleButton,
                                                                             juce::Button& bottomButton)
    {
        label.setBounds(area.removeFromTop(kStackLabelH));
        area.removeFromTop(kStackGap);
        topButton.setBounds(area.removeFromTop(kStackButtonH));
        area.removeFromTop(kStackGap);
        middleButton.setBounds(area.removeFromTop(kStackButtonH));
        area.removeFromTop(kStackGap);
        bottomButton.setBounds(area.removeFromTop(kStackButtonH));
    };

    auto layoutTwoButtonStack = [kStackLabelH, kStackButtonH, kStackGap](juce::Rectangle<int> area,
                                                                           juce::Label& label,
                                                                           juce::Button& topButton,
                                                                           juce::Button& bottomButton)
    {
        label.setBounds(area.removeFromTop(kStackLabelH));
        area.removeFromTop(kStackGap);
        topButton.setBounds(area.removeFromTop(kStackButtonH));
        area.removeFromTop(kStackGap);
        bottomButton.setBounds(area.removeFromTop(kStackButtonH));
    };

    // ── Panel 1: Performance ──
    {
        auto perfInner = makeGroup("Performance", kRowH);
        auto modeArea = perfInner.removeFromRight(kModeStackW);
        layoutThreeButtonStack(modeArea,
                       playbackModeLabel_,
                       playbackModeGateButton_,
                       playbackModeOneShotButton_,
                       playbackModeLoopButton_);
        perfInner.removeFromRight(kStackColGap);

        auto toggleCol = perfInner.removeFromLeft(86);
        monoToggle_.setBounds(toggleCol.removeFromTop(28));
        toggleCol.removeFromTop(4);
        legatoToggle_.setBounds(toggleCol.removeFromTop(28));
        toggleCol.removeFromTop(4);
        reverseToggle_.setBounds(toggleCol.removeFromTop(28));
        perfInner.removeFromLeft(kDialGap);

        glideDial_.setBounds(perfInner.removeFromLeft(kDial));
        perfInner.removeFromLeft(kDialGap);
        polyphonyDial_.setBounds(perfInner.removeFromLeft(kDial));
        perfInner.removeFromLeft(kDialGap);
        tuneCoarseDial_.setBounds(perfInner.removeFromLeft(kDial));
        perfInner.removeFromLeft(kDialGap);
        tuneFineDial_.setBounds(perfInner.removeFromLeft(kDial));
        perfInner.removeFromLeft(kDialGap);
        pitchBendRangeDial_.setBounds(perfInner.removeFromLeft(kDial));
        perfInner.removeFromLeft(kDialGap);
        pitchLfoRateDial_.setBounds(perfInner.removeFromLeft(kDial));
        perfInner.removeFromLeft(kDialGap);
        pitchLfoDepthDial_.setBounds(perfInner.removeFromLeft(kDial));
        perfInner.removeFromLeft(kDialGap);
        auto rightStack = perfInner.removeFromLeft(136);
        rootNoteLabel_.setBounds(rightStack.removeFromTop(16));
        rightStack.removeFromTop(2);
        rootNoteCombo_.setBounds(rightStack.removeFromTop(28));
        rightStack.removeFromTop(6);
        velocityCurveLabel_.setBounds(rightStack.removeFromTop(16));
        rightStack.removeFromTop(2);
        velocityCurveCombo_.setBounds(rightStack.removeFromTop(24));
    }

    // ── Panel 2: Trim and Loop ──
    {
        auto trimLoopInner = makeGroup("Trim and Loop", kRowH);

        playbackStartDial_.setBounds(trimLoopInner.removeFromLeft(kDial));
        trimLoopInner.removeFromLeft(kDialGap);
        playbackEndDial_.setBounds(trimLoopInner.removeFromLeft(kDial));
        trimLoopInner.removeFromLeft(kDialGap);
        loopStartDial_.setBounds(trimLoopInner.removeFromLeft(kDial));
        trimLoopInner.removeFromLeft(kDialGap);
        loopEndDial_.setBounds(trimLoopInner.removeFromLeft(kDial));
        trimLoopInner.removeFromLeft(kDialGap);
        loopCrossfadeDial_.setBounds(trimLoopInner.removeFromLeft(kDial));
    }

    // ── Panel 4: Amplitude Envelope ──
    {
        auto ampInner = makeGroup("Amplitude Envelope", kRowH);
        ampAttackDial_.setBounds(ampInner.removeFromLeft(kDial));
        ampInner.removeFromLeft(kDialGap);
        ampDecayDial_.setBounds(ampInner.removeFromLeft(kDial));
        ampInner.removeFromLeft(kDialGap);
        ampSustainDial_.setBounds(ampInner.removeFromLeft(kDial));
        ampInner.removeFromLeft(kDialGap);
        ampReleaseDial_.setBounds(ampInner.removeFromLeft(kDial));
        ampInner.removeFromLeft(kDialGap);
        ampLfoRateDial_.setBounds(ampInner.removeFromLeft(kDial));
        ampInner.removeFromLeft(kDialGap);
        ampLfoDepthDial_.setBounds(ampInner.removeFromLeft(kDial));
        ampInner.removeFromLeft(kDialGap);
        auto ampLfoShapeArea = ampInner.removeFromLeft(kDial + 24);
        ampLfoShapeLabel_.setBounds(ampLfoShapeArea.removeFromTop(16));
        ampLfoShapeArea.removeFromTop(2);
        ampLfoShapeCombo_.setBounds(ampLfoShapeArea.removeFromTop(24));
        ampInner.removeFromLeft(10);
        ampEnvelopeGraph_.setBounds(ampInner.reduced(0, 8));
    }

    // ── Panel 5: Filter ──
    {
        auto filterInner = makeGroup("Filter", kRowH);
        filterCutoffDial_.setBounds(filterInner.removeFromLeft(kDial));
        filterInner.removeFromLeft(kDialGap);
        filterResDial_.setBounds(filterInner.removeFromLeft(kDial));
        filterInner.removeFromLeft(kDialGap);
        filterEnvAmtDial_.setBounds(filterInner.removeFromLeft(kDial));
        filterInner.removeFromLeft(kDialGap);
        auto filterTypeArea = filterInner.removeFromLeft(kDial + 24);
        filterTypeLabel_.setBounds(filterTypeArea.removeFromTop(16));
        filterTypeArea.removeFromTop(2);
        filterTypeCombo_.setBounds(filterTypeArea.removeFromTop(24));
        filterInner.removeFromLeft(10);
        filterResponseGraph_.setBounds(filterInner.reduced(0, 8));
    }

    // ── Panel 6: Filter Envelope + Mod ──
    {
        constexpr int kFilterModPanelH = 238;
        auto filterEnvInner = makeGroup("Filter Envelope + Mod", kFilterModPanelH);

        auto row1 = filterEnvInner.removeFromTop(kDialH);
        filterAttackDial_.setBounds(row1.removeFromLeft(kDial));
        row1.removeFromLeft(kDialGap);
        filterDecayDial_.setBounds(row1.removeFromLeft(kDial));
        row1.removeFromLeft(kDialGap);
        filterSustainDial_.setBounds(row1.removeFromLeft(kDial));
        row1.removeFromLeft(kDialGap);
        filterReleaseDial_.setBounds(row1.removeFromLeft(kDial));
        row1.removeFromLeft(10);
        filterEnvelopeGraph_.setBounds(row1.reduced(0, 8));

        filterEnvInner.removeFromTop(8);

        auto row2 = filterEnvInner.removeFromTop(kDialH);
        filterKeytrackDial_.setBounds(row2.removeFromLeft(kDial));
        row2.removeFromLeft(kDialGap);
        filterVelDial_.setBounds(row2.removeFromLeft(kDial));
        row2.removeFromLeft(kDialGap);
        filterLfoRateDial_.setBounds(row2.removeFromLeft(kDial));
        row2.removeFromLeft(kDialGap);
        filterLfoAmtDial_.setBounds(row2.removeFromLeft(kDial));
        row2.removeFromLeft(10);
        auto lfoShapeArea = row2.removeFromLeft(120);
        filterLfoShapeLabel_.setBounds(lfoShapeArea.removeFromTop(16));
        lfoShapeArea.removeFromTop(2);
        filterLfoShapeCombo_.setBounds(lfoShapeArea.removeFromTop(24));
        row2.removeFromLeft(10);
        auto divArea = row2.removeFromLeft(120);
        filterLfoDivisionLabel_.setBounds(divArea.removeFromTop(16));
        divArea.removeFromTop(2);
        filterLfoDivisionCombo_.setBounds(divArea.removeFromTop(24));

        row2.removeFromLeft(12);
        auto syncArea = row2.removeFromLeft(90);
        filterLfoRetriggerToggle_.setBounds(syncArea.removeFromTop(22));
        syncArea.removeFromTop(6);
        filterLfoTempoSyncToggle_.setBounds(syncArea.removeFromTop(22));
    }

    // ── Panel 7: Effects ──
    {
        constexpr int kEffectsPanelH = kRowH + 88;
        auto fxInner = makeGroup("Effects", kEffectsPanelH);

        auto fxDialRow = fxInner.removeFromTop(kDialH);

        reverbMixDial_.setBounds(fxDialRow.removeFromLeft(kDial));
        fxDialRow.removeFromLeft(kDialGap);

        const auto delayTimeBounds = fxDialRow.removeFromLeft(kDial);
        delayTimeDial_.setBounds(delayTimeBounds);
        fxDialRow.removeFromLeft(kDialGap);

        const auto delayFeedbackBounds = fxDialRow.removeFromLeft(kDial);
        delayFeedbackDial_.setBounds(delayFeedbackBounds);
        fxDialRow.removeFromLeft(kDialGap);

        const auto delayMixBounds = fxDialRow.removeFromLeft(kDial);
        delayMixDial_.setBounds(delayMixBounds);
        fxDialRow.removeFromLeft(kDialGap * 2);

        const auto dcFilterDialBounds = fxDialRow.removeFromLeft(kDial);
        dcFilterCutoffDial_.setBounds(dcFilterDialBounds);
        fxDialRow.removeFromLeft(kDialGap);

        autopanRateDial_.setBounds(fxDialRow.removeFromLeft(kDial));
        fxDialRow.removeFromLeft(kDialGap);
        const auto autopanDepthBounds = fxDialRow.removeFromLeft(kDial);
        autopanDepthDial_.setBounds(autopanDepthBounds);
        fxDialRow.removeFromLeft(kDialGap);
        const auto saturationDriveBounds = fxDialRow.removeFromLeft(kDial);
        saturationDriveDial_.setBounds(saturationDriveBounds);

        fxInner.removeFromTop(10);
        auto fxControlRow = fxInner.removeFromTop(24);

        constexpr int kDelaySyncW = 120;
        const auto delayClusterLeft = delayTimeBounds.getX();
        const auto delayClusterRight = delayMixBounds.getRight();
        const auto delaySyncX = delayClusterLeft + (delayClusterRight - delayClusterLeft - kDelaySyncW) / 2;
        delayTempoSyncToggle_.setBounds(delaySyncX, fxControlRow.getY(), kDelaySyncW, 24);

        constexpr int kDcFilterW = 108;
        const auto dcFilterX = dcFilterDialBounds.getX() + (dcFilterDialBounds.getWidth() - kDcFilterW) / 2;
        dcFilterEnabledToggle_.setBounds(dcFilterX, fxControlRow.getY(), kDcFilterW, 24);

        constexpr int kSatModeW = 116;
        const auto satModeX = saturationDriveBounds.getX() + (saturationDriveBounds.getWidth() - kSatModeW) / 2;
        saturationModeCombo_.setBounds(satModeX, fxControlRow.getY(), kSatModeW, 24);
    }

    // ── Panel 8: Output ──
    {
        auto outInner = makeGroup("Output", kRowH);
        fadeInDial_.setBounds(outInner.removeFromLeft(kDial));
        outInner.removeFromLeft(kDialGap);
        fadeOutDial_.setBounds(outInner.removeFromLeft(kDial));
        outInner.removeFromLeft(kStackColGap);

        masterVolumeDial_.setBounds(outInner.removeFromLeft(kDial));
        outInner.removeFromLeft(kDialGap);

        panDial_.setBounds(outInner.removeFromLeft(kDial));
        outInner.removeFromLeft(kDialGap);

        preloadDial_.setBounds(outInner.removeFromLeft(kDial));
        outInner.removeFromLeft(kStackColGap);

        auto qualArea = outInner.removeFromRight(kModeStackW);
        layoutThreeButtonStack(qualArea,
                 qualityLabel_,
                 qualityCpuButton_,
                 qualityFidelityButton_,
                 qualityUltraButton_);

        outInner.removeFromRight(kStackColGap);
        outputLevelMeter_.setBounds(outInner.reduced(0, 6));
    }

    // ── Panel 9: Diagnostics ──
    {
        auto diagInner = makeGroup("Diagnostics", 56);
        diagnosticsLabel_.setBounds(diagInner.removeFromTop(22));
    }

    sampleControlsContent_.setSize(viewportWidth, juce::jmax(sampleControlsViewport_.getHeight(), scrollY + 4));
}

void AudiocityAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e2e));

    constexpr int kMargin = 14;

    // Title bar accent
    g.setColour(juce::Colour(0xff2d2d44));
    g.fillRect(0, 0, getWidth(), 8);

    auto content = getLocalBounds().reduced(kMargin);
    content.removeFromTop(30);
    content.removeFromTop(8);

    if (currentTabIndex_ == 1)
        paintSampleBrowserPane(g, content);
    else if (currentTabIndex_ == 2)
        paintPlayerPane(g, content);
    else if (currentTabIndex_ == 3)
    {
        g.setColour(juce::Colour(0xff252538));
        g.fillRoundedRectangle(content.reduced(6).toFloat(), 8.0f);
    }

    if (!isHoveringValidDrop_)
        return;

    // Drop overlay
    auto overlay = content.reduced(6);
    g.setColour(juce::Colours::deepskyblue.withAlpha(0.12f));
    g.fillRect(overlay);
    g.setColour(juce::Colours::deepskyblue.withAlpha(0.85f));
    g.drawRect(overlay, 2);
    g.setColour(juce::Colours::white.withAlpha(0.95f));
    g.setFont(14.0f);
    const auto dropText = processor_.isRexRuntimeAvailable()
        ? juce::String("Drop .wav, .aiff, .rex, or .rx2 to load")
        : juce::String("Drop .wav or .aiff to load");
    g.drawText(dropText, overlay, juce::Justification::centred);
}

bool AudiocityAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::returnKey && currentTabIndex_ == 1)
    {
        const auto selectedRow = sampleBrowserListBox_.getSelectedRow();
        if (selectedRow >= 0)
        {
            loadSampleFromBrowserRow(selectedRow);
            return true;
        }
    }

    if (key == juce::KeyPress::escapeKey)
    {
        processor_.panicAllAudio();
        updateGeneratePreviewButtonText();
        return true;
    }

    return juce::AudioProcessorEditor::keyPressed(key);
}

void AudiocityAudioProcessorEditor::updateGeneratePreviewButtonText()
{
    generatePreviewButton_.setButtonText(processor_.isGeneratedWaveformPreviewPlaying() ? "Stop" : "Play");
}

std::vector<std::vector<AudiocityAudioProcessorEditor::WaveformView::MinMax>>
AudiocityAudioProcessorEditor::getLoadedSampleWaveformMinMaxByChannel(const int maxPeaks) const
{
    const auto source = processor_.getLoadedSampleMinMaxByChannel(maxPeaks);
    std::vector<std::vector<WaveformView::MinMax>> converted;
    converted.resize(source.size());

    for (std::size_t channel = 0; channel < source.size(); ++channel)
    {
        const auto& srcChannel = source[channel];
        auto& dstChannel = converted[channel];
        dstChannel.resize(srcChannel.size());
        for (std::size_t i = 0; i < srcChannel.size(); ++i)
        {
            dstChannel[i].min = srcChannel[i].minValue;
            dstChannel[i].max = srcChannel[i].maxValue;
        }
    }

    return converted;
}

int AudiocityAudioProcessorEditor::getSelectedGenerateSampleCount() const
{
    const auto selected = generateSamplesCombo_.getSelectedId();
    return juce::jlimit(16, 2048, selected > 0 ? selected : 512);
}

int AudiocityAudioProcessorEditor::getSelectedGenerateBitDepth() const
{
    const auto selected = generateBitDepthCombo_.getSelectedId();
    return (selected == 8 || selected == 24) ? selected : 16;
}

int AudiocityAudioProcessorEditor::getSelectedGenerateMidiNote() const
{
    const auto selected = generateFrequencyCombo_.getSelectedId();
    return juce::jlimit(0, 127, selected - 1);
}

float AudiocityAudioProcessorEditor::quantizeWaveSample(const float value, const int bitDepth) const
{
    const auto clamped = juce::jlimit(-1.0f, 1.0f, value);
    if (bitDepth >= 24)
        return std::round(clamped * 8388607.0f) / 8388607.0f;
    if (bitDepth <= 8)
        return std::round(clamped * 127.0f) / 127.0f;
    return std::round(clamped * 32767.0f) / 32767.0f;
}

void AudiocityAudioProcessorEditor::enforceWaveBoundaryZeroCrossings(std::vector<float>& waveform) const
{
    if (waveform.empty())
        return;

    waveform.front() = 0.0f;
    waveform.back() = 0.0f;

    if (waveform.size() > 2)
    {
        waveform[1] *= 0.5f;
        waveform[waveform.size() - 2] *= 0.5f;
    }
}

void AudiocityAudioProcessorEditor::applySketchedWaveform(const std::vector<float>& sketchedWave)
{
    if (sketchedWave.empty())
        return;

    auto processed = sketchedWave;

    if (selectedSketchSmoothing_ == SketchedWaveSmoothing::curve && processed.size() >= 3)
    {
        auto smoothed = processed;
        for (std::size_t i = 1; i + 1 < processed.size(); ++i)
            smoothed[i] = (processed[i - 1] + processed[i] * 2.0f + processed[i + 1]) * 0.25f;
        processed.swap(smoothed);
    }

    enforceWaveBoundaryZeroCrossings(processed);

    const auto bitDepth = getSelectedGenerateBitDepth();
    for (auto& sample : processed)
        sample = quantizeWaveSample(sample, bitDepth);

    generatedWaveform_ = std::move(processed);
    generateWaveformView_.setWaveform(generatedWaveform_);
    processor_.setGeneratedWaveformPreview(generatedWaveform_);
}

void AudiocityAudioProcessorEditor::updateGeneratePulseWidthControlState()
{
    const auto enabled = selectedGeneratedWaveType_ == GeneratedWaveType::pulse;
    generatePulseWidthLabel_.setEnabled(enabled);
    generatePulseWidthSlider_.setEnabled(enabled);
}

void AudiocityAudioProcessorEditor::updateAmpEnvelopeGraphFromDials()
{
    ampEnvelopeGraph_.setEnvelope(static_cast<float>(ampAttackDial_.getValue()),
                                  static_cast<float>(ampDecayDial_.getValue()),
                                  static_cast<float>(ampSustainDial_.getValue()),
                                  static_cast<float>(ampReleaseDial_.getValue()));
}

void AudiocityAudioProcessorEditor::updateFilterEnvelopeGraphFromDials()
{
    filterEnvelopeGraph_.setEnvelope(static_cast<float>(filterAttackDial_.getValue()),
                                     static_cast<float>(filterDecayDial_.getValue()),
                                     static_cast<float>(filterSustainDial_.getValue()),
                                     static_cast<float>(filterReleaseDial_.getValue()));
}

void AudiocityAudioProcessorEditor::updateFilterResponseGraphFromControls()
{
    const auto modeId = filterTypeCombo_.getSelectedId();
    const auto cutoffHz = static_cast<float>(filterCutoffDial_.getValue());
    const auto resonance = static_cast<float>(filterResDial_.getValue()) / 100.0f;
    const auto envAmountHz = static_cast<float>(filterEnvAmtDial_.getValue());
    filterResponseGraph_.setState(modeId, cutoffHz, resonance, envAmountHz);
}

void AudiocityAudioProcessorEditor::regenerateWaveform()
{
    const auto sampleCount = getSelectedGenerateSampleCount();
    const auto bitDepth = getSelectedGenerateBitDepth();

    generatedWaveform_.assign(static_cast<std::size_t>(sampleCount), 0.0f);

    if (selectedGeneratedWaveType_ == GeneratedWaveType::random && sampleCount >= 2)
    {
        struct Anchor
        {
            float x = 0.0f;
            float y = 0.0f;
        };

        struct CurveSegment
        {
            Anchor p0;
            Anchor p1;
            Anchor p2;
            Anchor p3;
        };

        juce::Random random(juce::Random::getSystemRandom().nextInt());
        const auto anchorCount = random.nextInt(juce::Range<int>(3, 10));

        std::vector<Anchor> anchors;
        anchors.resize(static_cast<std::size_t>(anchorCount));
        anchors.front() = { 0.0f, 0.0f };
        anchors.back() = { 1.0f, 0.0f };

        std::vector<float> segmentWeights;
        segmentWeights.resize(static_cast<std::size_t>(anchorCount - 1), 0.0f);
        float totalWeight = 0.0f;
        for (int i = 0; i < anchorCount - 1; ++i)
        {
            const auto w = 0.25f + random.nextFloat();
            segmentWeights[static_cast<std::size_t>(i)] = w;
            totalWeight += w;
        }

        float cumulative = 0.0f;
        for (int i = 1; i < anchorCount - 1; ++i)
        {
            cumulative += segmentWeights[static_cast<std::size_t>(i - 1)] / totalWeight;
            anchors[static_cast<std::size_t>(i)].x = juce::jlimit(0.0f, 1.0f, cumulative);
            anchors[static_cast<std::size_t>(i)].y = (random.nextFloat() * 2.0f) - 1.0f;
        }

        std::vector<CurveSegment> curves;
        curves.reserve(static_cast<std::size_t>(anchorCount - 1));
        for (int i = 0; i < anchorCount - 1; ++i)
        {
            const auto p0 = anchors[static_cast<std::size_t>(i)];
            const auto p3 = anchors[static_cast<std::size_t>(i + 1)];
            const auto dx = p3.x - p0.x;

            CurveSegment segment;
            segment.p0 = p0;
            segment.p3 = p3;
            segment.p1.x = p0.x + dx * (1.0f / 3.0f);
            segment.p2.x = p0.x + dx * (2.0f / 3.0f);
            segment.p1.y = juce::jlimit(-1.0f, 1.0f, p0.y + ((random.nextFloat() * 2.0f) - 1.0f));
            segment.p2.y = juce::jlimit(-1.0f, 1.0f, p3.y + ((random.nextFloat() * 2.0f) - 1.0f));
            curves.push_back(segment);
        }

        int segmentIndex = 0;
        const auto lastSample = juce::jmax(1, sampleCount - 1);
        for (int i = 0; i < sampleCount; ++i)
        {
            const auto x = static_cast<float>(i) / static_cast<float>(lastSample);
            while (segmentIndex < anchorCount - 2
                && x > anchors[static_cast<std::size_t>(segmentIndex + 1)].x)
            {
                ++segmentIndex;
            }

            const auto& left = anchors[static_cast<std::size_t>(segmentIndex)];
            const auto& right = anchors[static_cast<std::size_t>(segmentIndex + 1)];
            const auto span = juce::jmax(1.0e-6f, right.x - left.x);
            const auto t = juce::jlimit(0.0f, 1.0f, (x - left.x) / span);

            float value = 0.0f;
            const auto& curve = curves[static_cast<std::size_t>(segmentIndex)];
            const auto omt = 1.0f - t;
            const auto omt2 = omt * omt;
            const auto t2 = t * t;
            value = (omt2 * omt) * curve.p0.y
                + (3.0f * omt2 * t) * curve.p1.y
                + (3.0f * omt * t2) * curve.p2.y
                + (t2 * t) * curve.p3.y;

            generatedWaveform_[static_cast<std::size_t>(i)] = quantizeWaveSample(value, bitDepth);
        }

        enforceWaveBoundaryZeroCrossings(generatedWaveform_);
        generateWaveformView_.setWaveform(generatedWaveform_);
        processor_.setGeneratedWaveformPreview(generatedWaveform_);
        processor_.setGeneratedWaveformPreviewMidiNote(getSelectedGenerateMidiNote());
        return;
    }

    if (selectedGeneratedWaveType_ == GeneratedWaveType::noise)
    {
        juce::Random random(juce::Random::getSystemRandom().nextInt());
        for (int i = 0; i < sampleCount; ++i)
        {
            const auto value = (random.nextFloat() * 2.0f) - 1.0f;
            generatedWaveform_[static_cast<std::size_t>(i)] = quantizeWaveSample(value, bitDepth);
        }

        enforceWaveBoundaryZeroCrossings(generatedWaveform_);
        generateWaveformView_.setWaveform(generatedWaveform_);
        processor_.setGeneratedWaveformPreview(generatedWaveform_);
        processor_.setGeneratedWaveformPreviewMidiNote(getSelectedGenerateMidiNote());
        return;
    }

    for (int i = 0; i < sampleCount; ++i)
    {
        const auto phase = static_cast<float>(i) / static_cast<float>(sampleCount);
        float value = 0.0f;

        switch (selectedGeneratedWaveType_)
        {
            case GeneratedWaveType::sine:
                value = std::sin(phase * 2.0f * juce::MathConstants<float>::pi);
                break;
            case GeneratedWaveType::ramp:
                value = (phase * 2.0f) - 1.0f;
                break;
            case GeneratedWaveType::square:
                value = phase < 0.5f ? 1.0f : -1.0f;
                break;
            case GeneratedWaveType::sawtooth:
                value = 1.0f - (phase * 2.0f);
                break;
            case GeneratedWaveType::triangle:
                value = 1.0f - 4.0f * std::abs(phase - 0.5f);
                break;
            case GeneratedWaveType::pulse:
            {
                const auto pulseWidth = static_cast<float>(generatePulseWidthSlider_.getValue() * 0.01);
                value = phase < pulseWidth ? 1.0f : -1.0f;
                break;
            }
            case GeneratedWaveType::random:
            case GeneratedWaveType::noise:
                value = 0.0f;
                break;
        }

        generatedWaveform_[static_cast<std::size_t>(i)] = quantizeWaveSample(value, bitDepth);
    }

    enforceWaveBoundaryZeroCrossings(generatedWaveform_);

    generateWaveformView_.setWaveform(generatedWaveform_);
    processor_.setGeneratedWaveformPreview(generatedWaveform_);
    processor_.setGeneratedWaveformPreviewMidiNote(getSelectedGenerateMidiNote());
}

// ─── Group box rendering ───────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::paintGroupBoxes(juce::Graphics& g) const
{
    for (const auto& group : groupBoxes_)
    {
        auto bf = group.bounds.toFloat();

        // Panel background
        g.setColour(juce::Colour(0xff252538));
        g.fillRoundedRectangle(bf, 6.0f);

        // Border
        g.setColour(juce::Colour(0xff3a3a52));
        g.drawRoundedRectangle(bf.reduced(0.5f), 6.0f, 1.0f);

        // Header band
        auto hdr = bf.removeFromTop(22.0f);
        g.setColour(juce::Colour(0xff2d2d44));
        g.fillRoundedRectangle(hdr, 6.0f);
        // Square-off the bottom corners of the header
        g.fillRect(hdr.withTrimmedTop(6.0f));

        // Title text
        g.setColour(juce::Colour(0xff808098));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(group.title, hdr.withTrimmedLeft(10.0f),
                   juce::Justification::centredLeft);
    }
}

// ─── Tooltips ──────────────────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::setupTooltips()
{
    sampleBrowserChooseRootButton_.setTooltip(
        "Select Sample Folder...");
    sampleBrowserFilterEditor_.setTooltip(
        "Search Samples - Filter by sample name or relative path");
    sampleBrowserSortCombo_.setTooltip(
        "Sort Samples - Sort by sample name or by relative source path");
    playerKeyboardScrollLeft_.setTooltip(
        "Scroll Keyboard Left");
    playerKeyboardScrollRight_.setTooltip(
        "Scroll Keyboard Right");

    rootNoteLabel_.setTooltip(
        "Root Note - MIDI note number and pitch name for the sample's original pitch");
    rootNoteCombo_.setTooltip(
        "Root Note - MIDI note number and pitch name for the sample's original pitch");
    tuneCoarseDial_.setLabelTooltip(
        "Tune Coarse - Shift playback pitch in semitones (-24 to +24)");
    tuneFineDial_.setLabelTooltip(
        "Tune Fine - Shift playback pitch in cents (-100 to +100)");
    pitchBendRangeDial_.setLabelTooltip(
        "Pitch Bend Range - Maximum pitch wheel range in semitones (0 to 24)");
    pitchLfoRateDial_.setLabelTooltip(
        "Pitch LFO Rate - Vibrato speed in Hz");
    pitchLfoDepthDial_.setLabelTooltip(
        "Pitch LFO Depth - Vibrato amount in cents");
    playbackStartDial_.setLabelTooltip(
        "Playback Start - Sample position where playback begins");
    playbackEndDial_.setLabelTooltip(
        "Playback End - Sample position where playback ends");
    loopStartDial_.setLabelTooltip(
        "Loop Start - Sample position where the loop region begins");
    loopEndDial_.setLabelTooltip(
        "Loop End - Sample position where the loop region ends");
    loopCrossfadeDial_.setLabelTooltip(
        "Loop Crossfade - Crossfade length in samples at loop wrap point");
    glideDial_.setLabelTooltip(
        "Glide Time - Portamento time between notes in milliseconds");
    polyphonyDial_.setLabelTooltip(
        "Polyphony Limit - Maximum simultaneous voices (1 to 64)");
    ampAttackDial_.setLabelTooltip(
        "Attack - Amplitude envelope attack time in milliseconds");
    ampDecayDial_.setLabelTooltip(
        "Decay - Amplitude envelope decay time in milliseconds");
    ampSustainDial_.setLabelTooltip(
        "Sustain - Amplitude envelope sustain level (0 to 1)");
    ampReleaseDial_.setLabelTooltip(
        "Release - Amplitude envelope release time in milliseconds");
    ampLfoRateDial_.setLabelTooltip(
        "Amp LFO Rate - Tremolo speed in Hz");
    ampLfoDepthDial_.setLabelTooltip(
        "Amp LFO Depth - Tremolo amount from 0% (off) to 100% (full)");
    ampLfoShapeLabel_.setTooltip(
        "Amp LFO Shape - Waveform used for tremolo modulation");
    ampLfoShapeCombo_.setTooltip(
        "Amp LFO Shape - Choose Sine, Triangle, Square, Saw Up, or Saw Down");
    filterCutoffDial_.setLabelTooltip(
        "Filter Cutoff - Low-pass filter frequency in Hz");
    filterResDial_.setLabelTooltip(
        "Resonance - Filter emphasis at the cutoff frequency");
    filterEnvAmtDial_.setLabelTooltip(
        "Envelope Amount - Filter envelope modulation depth in Hz");
    filterAttackDial_.setLabelTooltip(
        "Filter Attack - Filter envelope attack time in milliseconds");
    filterDecayDial_.setLabelTooltip(
        "Filter Decay - Filter envelope decay time in milliseconds");
    filterSustainDial_.setLabelTooltip(
        "Filter Sustain - Filter envelope sustain level (0 to 1)");
    filterReleaseDial_.setLabelTooltip(
        "Filter Release - Filter envelope release time in milliseconds");
    filterKeytrackDial_.setLabelTooltip(
        "Filter Key Tracking - Scales cutoff by keyboard pitch (-100% to 200%)");
    filterVelDial_.setLabelTooltip(
        "Filter Velocity Amount - Extra cutoff added at high velocity");
    filterLfoRateDial_.setLabelTooltip(
        "Filter LFO Rate - Modulation speed in Hz");
    filterLfoRateKeyDial_.setLabelTooltip(
        "Filter LFO Rate Keytracking - Scales LFO speed across keyboard (-100% to 200%)");
    filterLfoAmtDial_.setLabelTooltip(
        "Filter LFO Amount - Bipolar cutoff modulation depth in Hz");
    filterLfoAmtKeyDial_.setLabelTooltip(
        "Filter LFO Amount Keytracking - Scales LFO depth across keyboard (-100% to 200%)");
    filterLfoStartPhaseDial_.setLabelTooltip(
        "Filter LFO Start Phase - Retrigger start offset in degrees (0 to 360)");
    filterLfoStartRandDial_.setLabelTooltip(
        "Filter LFO Start Random - Adds deterministic bipolar random offset per note");
    filterLfoFadeInDial_.setLabelTooltip(
        "Filter LFO Fade In - Time to ramp LFO depth from 0 to full per note");
    filterLfoShapeLabel_.setTooltip(
        "Filter LFO Shape - Waveform used for filter modulation");
    filterLfoShapeCombo_.setTooltip(
        "Filter LFO Shape - Choose Sine, Triangle, Square, Saw Up, or Saw Down");
    filterLfoRetriggerToggle_.setTooltip(
        "Filter LFO Retrigger - Restart LFO phase at note-on when enabled");
    filterLfoTempoSyncToggle_.setTooltip(
        "Filter LFO Tempo Sync - Locks LFO rate to host tempo using musical divisions");
    filterLfoRateKeySyncToggle_.setTooltip(
        "Filter LFO Key Sync - Apply LFO rate keytracking while tempo sync is enabled");
    filterLfoKeytrackLinearToggle_.setTooltip(
        "Filter LFO Key Linear - Use linear keytracking curve for LFO rate and amount when enabled");
    filterLfoUnipolarToggle_.setTooltip(
        "Filter LFO Unipolar - Convert LFO from bipolar (-1..1) to unipolar (0..1)");
    filterLfoDivisionLabel_.setTooltip(
        "Filter LFO Division - Note length used when tempo sync is enabled");
    filterLfoDivisionCombo_.setTooltip(
        "Filter LFO Division - Select 1/16 through 2/1 sync rates");
    filterKeytrackSnapLabel_.setTooltip(
        "Key Snap - Quick preset values for key tracking");
    filterKeytrackSnapCombo_.setTooltip(
        "Key Snap - Quickly set key tracking to musical preset percentages");
    filterTypeCombo_.setTooltip(
        "Filter Type - Select mode/slope (LP/HP/BP/Notch)");
    filterTypeLabel_.setTooltip(
        "Filter Type - Select mode/slope (LP/HP/BP/Notch)");
    fadeInDial_.setLabelTooltip(
        "Fade In - Number of samples to fade in at playback start");
    fadeOutDial_.setLabelTooltip(
        "Fade Out - Number of samples to fade out at playback end");
    preloadDial_.setLabelTooltip(
        "Preload - Number of samples buffered before streaming begins");
    masterVolumeDial_.setLabelTooltip(
        "Master Volume - Final output gain after engine processing");
    panDial_.setLabelTooltip(
        "Pan - Stereo balance from -100L to +100R, applied pre-reverb");
    qualityCpuButton_.setTooltip(
        "Quality - Prioritize lower CPU usage");
    qualityFidelityButton_.setTooltip(
        "Quality - Prioritize highest playback fidelity");
    qualityUltraButton_.setTooltip(
        "Quality - High quality cubic interpolation");
    reverbMixDial_.setLabelTooltip(
        "Reverb Mix - Global wet amount");
    delayTimeDial_.setLabelTooltip(
        "Delay Time - Delay length in milliseconds (or snapped note value when Delay Sync is enabled)");
    delayFeedbackDial_.setLabelTooltip(
        "Delay Feedback - Amount of delayed signal fed back into the delay line");
    delayMixDial_.setLabelTooltip(
        "Delay Mix - Blend between dry signal and delayed signal");
    delayTempoSyncToggle_.setTooltip(
        "Delay Sync - Quantize delay time to host tempo divisions");
    generateRandomButton_.setTooltip(
        "Random - Generate a random waveform from Bezier-curve anchor segments");
    generateNoiseButton_.setTooltip(
        "Noise - Generate random sample values across the waveform");
    dcFilterEnabledToggle_.setTooltip(
        "DC Filter - Enable a subsonic high-pass filter to remove DC offset");
    dcFilterCutoffDial_.setLabelTooltip(
        "DC HPF - Subsonic high-pass cutoff in Hz (5 to 20)");
    autopanRateDial_.setLabelTooltip(
        "Autopan Rate - Stereo modulation speed in Hz");
    autopanDepthDial_.setLabelTooltip(
        "Autopan Depth - Stereo modulation amount from 0% to 100%");
    saturationDriveDial_.setLabelTooltip(
        "Drive - Amount of post-filter waveshaper saturation (adds harmonic character with minimal CPU)");
    saturationModeCombo_.setTooltip(
        "Type - Select saturation character: Soft Clip (smooth), Hard Clip (aggressive), Tape (rounded), Tube (warm odd/even harmonics)");
    velocityCurveCombo_.setTooltip(
        "Velocity Curve - Response curve for velocity to amplitude");
    velocityCurveLabel_.setTooltip(
        "Velocity Curve - Response curve for velocity to amplitude");

    monoToggle_.setTooltip(
        "Monophonic - Limit to a single voice at a time");
    legatoToggle_.setTooltip(
        "Legato - Keep the envelope running between overlapping notes");
    reverseToggle_.setTooltip(
        "Reverse - Play the sample backwards");

    for (int i = 0; i < kPlayerPadCount; ++i)
    {
        playerPadButtons_[static_cast<std::size_t>(i)].setTooltip(
            "Drum Pad - Click and hold to trigger the assigned MIDI note");
        playerPadAssignButtons_[static_cast<std::size_t>(i)].setTooltip(
            "Assign");
    }
}

// ─── Drag & Drop ───────────────────────────────────────────────────────────────

bool AudiocityAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    // IMPORTANT: Called from the OLE modal loop on every mouse-move.
    // juce::File() and getFileExtension() are pure string ops — safe here.
    // Do NOT use juce::URL (triggers COM), File::existsAsFile() (I/O), etc.
    DBG("[DnD] isInterestedInFileDrag called with " + juce::String(files.size()) + " file(s)");
    for (int i = 0; i < files.size(); ++i)
        DBG("[DnD]   file[" + juce::String(i) + "] = \"" + files[i] + "\"");

    for (const auto& rawPath : files)
    {
        auto path = rawPath.trim();
        if (path.isEmpty())
            continue;

        // Strip file:// URI scheme via string ops (no juce::URL)
        if (path.startsWithIgnoreCase("file:///"))
            path = path.substring(8).replace("/", "\\");
        else if (path.startsWithIgnoreCase("file://"))
            path = path.substring(7).replace("/", "\\");

        const auto ext = juce::File(path).getFileExtension().toLowerCase();
        DBG("[DnD]   normalized=\"" + path + "\"  ext=\"" + ext + "\"");
        const auto rexSupported = processor_.isRexRuntimeAvailable();
        if (ext == ".wav" || ext == ".aiff" || ext == ".aif"
            || (rexSupported && (ext == ".rex" || ext == ".rx2")))
        {
            DBG("[DnD]   -> INTERESTED");
            return true;
        }
    }
    DBG("[DnD]   -> NOT interested");
    return false;
}

void AudiocityAudioProcessorEditor::fileDragEnter(const juce::StringArray& files, int, int)
{
    DBG("[DnD] fileDragEnter");
    isHoveringValidDrop_ = isInterestedInFileDrag(files);
    repaint();
}

void AudiocityAudioProcessorEditor::fileDragMove(const juce::StringArray& files, int x, int y)
{
    fileDragEnter(files, x, y);
}

void AudiocityAudioProcessorEditor::fileDragExit(const juce::StringArray&)
{
    DBG("[DnD] fileDragExit");
    isHoveringValidDrop_ = false;
    repaint();
}

void AudiocityAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    // IMPORTANT: Called during the OLE modal loop.
    // Must NOT do file I/O, juce::URL, callAsync, or any COM call.
    // Just stash the raw paths and let timerCallback handle the load.
    DBG("[DnD] filesDropped called with " + juce::String(files.size()) + " file(s)");
    for (int i = 0; i < files.size(); ++i)
        DBG("[DnD]   dropped[" + juce::String(i) + "] = \"" + files[i] + "\"");
    isHoveringValidDrop_ = false;
    pendingDropFiles_ = files;
    hasPendingDrop_ = true;
    repaint();
    DBG("[DnD] filesDropped finished, hasPendingDrop_=true");
}

// ─── File Chooser ──────────────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::openSampleChooser()
{
    const auto wildcard = processor_.isRexRuntimeAvailable()
        ? juce::String("*.wav;*.aiff;*.aif;*.rex;*.rx2")
        : juce::String("*.wav;*.aiff;*.aif");
    fileChooser_ = std::make_unique<juce::FileChooser>("Load sample", juce::File{}, wildcard);

    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser_->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        const auto selected = chooser.getResult();
        if (selected == juce::File{})
            return;

        if (processor_.loadSampleFromFile(selected))
            refreshUI();
    });
}

void AudiocityAudioProcessorEditor::updatePlayerKeyboardSizing()
{
    constexpr int kWhiteKeys = 52;
    const auto viewportBounds = playerKeyboardViewport_.getLocalBounds();
    if (viewportBounds.isEmpty())
        return;

    const auto whiteKeyWidth = juce::jmax(10.0f,
        juce::jmin(18.0f, static_cast<float>(viewportBounds.getWidth()) / static_cast<float>(kWhiteKeys)));

    playerKeyboard_.setKeyWidth(whiteKeyWidth);
    playerKeyboard_.setSize(static_cast<int>(std::ceil(whiteKeyWidth * static_cast<float>(kWhiteKeys))),
                            viewportBounds.getHeight());
}

void AudiocityAudioProcessorEditor::refreshPlayerPadButtons()
{
    for (int i = 0; i < kPlayerPadCount; ++i)
    {
        const auto& assignment = playerPadAssignments_[static_cast<std::size_t>(i)];
        playerPadButtons_[static_cast<std::size_t>(i)].setButtonText(
            "Pad " + juce::String(i + 1)
            + "  " + formatMidiNoteName(assignment.noteNumber)
            + "  Vel " + juce::String(assignment.velocity));
    }
}

void AudiocityAudioProcessorEditor::showPadAssignmentDialog(const int padIndex)
{
    if (padIndex < 0 || padIndex >= kPlayerPadCount)
        return;

    const auto current = playerPadAssignments_[static_cast<std::size_t>(padIndex)];

    auto content = std::make_unique<PadAssignmentDialogContent>(current.noteNumber,
                                                                current.velocity,
                                                                [this, padIndex](const int note, const int vel)
                                                                {
                                                                    const auto noteNumber = juce::jlimit(0, 127, note);
                                                                    const auto velocity = juce::jlimit(1, 127, vel);
                                                                    processor_.setPlayerPadAssignment(padIndex, noteNumber, velocity);
                                                                    playerPadAssignments_[static_cast<std::size_t>(padIndex)] = { noteNumber, velocity };
                                                                    refreshPlayerPadButtons();
                                                                });
    content->setSize(420, 140);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Assign Drum Pad " + juce::String(padIndex + 1);
    options.content.setOwned(content.release());
    options.componentToCentreAround = this;
    options.dialogBackgroundColour = juce::Colour(0xff252538);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

// ─── Refresh UI ────────────────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::refreshUI(const bool forceWaveformReset)
{
    const auto persistedDisplayMode = processor_.getWaveformDisplayMode();
    waveformDisplayModeCombo_.setSelectedId(persistedDisplayMode, juce::dontSendNotification);
    waveformView_.setDisplayMode(persistedDisplayMode == 2
        ? WaveformView::DisplayMode::symmetricEnvelope
        : WaveformView::DisplayMode::signedWaveform);

    const auto path = processor_.getLoadedSamplePath();
    const auto sampleIdentity = path.isNotEmpty()
        ? (juce::String("file:") + path)
        : (processor_.isGeneratedWaveformLoaded() ? juce::String("generated") : juce::String("none"));
    const auto sampleLabel = path.isNotEmpty()
        ? path
        : (processor_.isGeneratedWaveformLoaded() ? juce::String("Generated Waveform") : juce::String("No sample loaded"));
    samplePathLabel_.setText(sampleLabel, juce::dontSendNotification);
    const auto isNewLoadedSample = sampleIdentity != lastWaveformSamplePath_;

    const bool isEditingRootNote = rootNoteCombo_.hasKeyboardFocus(true) || rootNoteCombo_.isPopupActive();
    if (!isEditingRootNote)
        rootNoteCombo_.setSelectedId(processor_.getRootMidiNote() + 1, juce::dontSendNotification);
    tuneCoarseDial_.setValue(processor_.getCoarseTuneSemitones(), juce::dontSendNotification);
    tuneFineDial_.setValue(processor_.getFineTuneCents(), juce::dontSendNotification);
    pitchBendRangeDial_.setValue(processor_.getPitchBendRangeSemitones(), juce::dontSendNotification);
    const auto pitchLfo = processor_.getPitchLfoSettings();
    pitchLfoRateDial_.setValue(pitchLfo.rateHz, juce::dontSendNotification);
    pitchLfoDepthDial_.setValue(pitchLfo.depthCents, juce::dontSendNotification);

    playerPadAssignments_ = processor_.getAllPlayerPadAssignments();
    refreshPlayerPadButtons();

    const auto sampleLength = processor_.getLoadedSampleLength();
    const auto maxSampleIndex = juce::jmax(1, sampleLength - 1);

    playbackStartDial_.setRange(0.0, static_cast<double>(maxSampleIndex), 1.0);
    playbackEndDial_.setRange(0.0, static_cast<double>(maxSampleIndex), 1.0);
    loopStartDial_.setRange(0.0, static_cast<double>(maxSampleIndex), 1.0);
    loopEndDial_.setRange(0.0, static_cast<double>(maxSampleIndex), 1.0);

    const auto targetPeakResolution = computeWaveformPeakResolution(waveformView_.getWidth());
    const auto shouldRefreshPeaks = sampleLength <= 0
        ? false
        : (isNewLoadedSample
            || forceWaveformReset
            || cachedWaveformMinMaxByChannel_.empty()
            || cachedWaveformPeakResolution_ != targetPeakResolution);

    if (sampleLength <= 0)
    {
        cachedWaveformMinMaxByChannel_.clear();
        cachedWaveformPeakResolution_ = 0;
    }
    else if (shouldRefreshPeaks)
    {
        cachedWaveformMinMaxByChannel_ = getLoadedSampleWaveformMinMaxByChannel(targetPeakResolution);
        cachedWaveformPeakResolution_ = targetPeakResolution;
    }

    waveformView_.setState(sampleLength, cachedWaveformMinMaxByChannel_,
        processor_.getSampleWindowStart(), processor_.getSampleWindowEnd(),
        processor_.getLoopStart(), processor_.getLoopEnd(),
        processor_.getLoadedSampleLoopFormatBadge());

    if (forceWaveformReset || isNewLoadedSample)
    {
        waveformView_.resetView();
    }
    else
    {
        const auto [viewStart, viewCount] = processor_.getWaveformViewRange();
        waveformView_.setViewRange(viewStart, viewCount);
    }

    lastWaveformSamplePath_ = sampleIdentity;

    // Playback mode
    const auto playbackMode = processor_.getPlaybackMode();
    playbackModeGateButton_.setToggleState(playbackMode == AudiocityAudioProcessor::PlaybackMode::gate,
        juce::dontSendNotification);
    playbackModeOneShotButton_.setToggleState(playbackMode == AudiocityAudioProcessor::PlaybackMode::oneShot,
        juce::dontSendNotification);
    playbackModeLoopButton_.setToggleState(playbackMode == AudiocityAudioProcessor::PlaybackMode::loop,
        juce::dontSendNotification);

    // Loop points
    loopStartDial_.setValue(processor_.getLoopStart());
    loopEndDial_.setValue(processor_.getLoopEnd());
    loopCrossfadeDial_.setValue(processor_.getLoopCrossfadeSamples());

    // Playback window
    playbackStartDial_.setValue(processor_.getSampleWindowStart());
    playbackEndDial_.setValue(processor_.getSampleWindowEnd());

    // Performance
    monoToggle_.setToggleState(processor_.getMonoMode(), juce::dontSendNotification);
    legatoToggle_.setToggleState(processor_.getLegatoMode(), juce::dontSendNotification);
    legatoToggle_.setEnabled(processor_.getMonoMode());
    glideDial_.setValue(processor_.getGlideSeconds() * 1000.0f);
    polyphonyDial_.setValue(static_cast<double>(processor_.getPolyphonyLimit()));

    // Amp ADSR
    const auto amp = processor_.getAmpEnvelope();
    ampAttackDial_.setValue(amp.attackSeconds * 1000.0f);
    ampDecayDial_.setValue(amp.decaySeconds * 1000.0f);
    ampSustainDial_.setValue(amp.sustainLevel);
    ampReleaseDial_.setValue(amp.releaseSeconds * 1000.0f);
    const auto ampLfo = processor_.getAmpLfoSettings();
    ampLfoRateDial_.setValue(ampLfo.rateHz);
    ampLfoDepthDial_.setValue(ampLfo.depth * 100.0f);
    ampLfoShapeCombo_.setSelectedId(static_cast<int>(ampLfo.shape) + 1, juce::dontSendNotification);
    updateAmpEnvelopeGraphFromDials();

    // Filter
    const auto filter = processor_.getFilterSettings();
    filterCutoffDial_.setValue(filter.baseCutoffHz);
    filterResDial_.setValue(static_cast<double>(filter.resonance) * 100.0);
    filterEnvAmtDial_.setValue(filter.envAmountHz);
    filterTypeCombo_.setSelectedId(filterModeToComboId(filter.mode), juce::dontSendNotification);
    updateFilterResponseGraphFromControls();
    filterKeytrackDial_.setValue(filter.keyTracking * 100.0f);
    filterVelDial_.setValue(filter.velocityAmountHz);
    filterLfoRateDial_.setValue(filter.lfoRateHz);
    filterLfoRateKeyDial_.setValue(filter.lfoRateKeyTracking * 100.0f);
    filterLfoAmtDial_.setValue(filter.lfoAmountHz);
    filterLfoAmtKeyDial_.setValue(filter.lfoAmountKeyTracking * 100.0f);
    filterLfoStartPhaseDial_.setValue(filter.lfoStartPhaseDegrees);
    filterLfoStartRandDial_.setValue(filter.lfoStartPhaseRandomDegrees);
    filterLfoFadeInDial_.setValue(filter.lfoFadeInMs);
    filterLfoShapeCombo_.setSelectedId(lfoShapeToComboId(filter.lfoShape), juce::dontSendNotification);
    filterLfoRetriggerToggle_.setToggleState(filter.lfoRetrigger, juce::dontSendNotification);
    filterLfoTempoSyncToggle_.setToggleState(filter.lfoTempoSync, juce::dontSendNotification);
    filterLfoRateKeySyncToggle_.setToggleState(filter.lfoRateKeytrackInTempoSync, juce::dontSendNotification);
    filterLfoKeytrackLinearToggle_.setToggleState(filter.lfoKeytrackLinear, juce::dontSendNotification);
    filterLfoUnipolarToggle_.setToggleState(filter.lfoUnipolar, juce::dontSendNotification);
    filterLfoDivisionCombo_.setSelectedId(filter.lfoSyncDivision + 1, juce::dontSendNotification);
    filterLfoRateDial_.setEnabled(!filter.lfoTempoSync);
    filterLfoRateKeySyncToggle_.setEnabled(filter.lfoTempoSync);
    filterLfoDivisionLabel_.setEnabled(filter.lfoTempoSync);
    filterLfoDivisionCombo_.setEnabled(filter.lfoTempoSync);

    const auto filterEnv = processor_.getFilterEnvelope();
    filterAttackDial_.setValue(filterEnv.attackSeconds * 1000.0f);
    filterDecayDial_.setValue(filterEnv.decaySeconds * 1000.0f);
    filterSustainDial_.setValue(filterEnv.sustainLevel);
    filterReleaseDial_.setValue(filterEnv.releaseSeconds * 1000.0f);
    updateFilterEnvelopeGraphFromDials();

    // Quality / Preload
    qualityCpuButton_.setToggleState(
        processor_.getQualityTier() == AudiocityAudioProcessor::QualityTier::cpu,
        juce::dontSendNotification);
    qualityFidelityButton_.setToggleState(
        processor_.getQualityTier() == AudiocityAudioProcessor::QualityTier::fidelity,
        juce::dontSendNotification);
    qualityUltraButton_.setToggleState(
        processor_.getQualityTier() == AudiocityAudioProcessor::QualityTier::ultra,
        juce::dontSendNotification);
    preloadDial_.setValue(processor_.getPreloadSamples());
    masterVolumeDial_.setValue(processor_.getMasterVolume() * 100.0f);
    panDial_.setValue(processor_.getPan() * 100.0f);
    reverbMixDial_.setValue(processor_.getReverbMix() * 100.0f);
    const auto delay = processor_.getDelaySettings();
    delayTimeDial_.setValue(delay.timeMs);
    delayFeedbackDial_.setValue(delay.feedback * 100.0f);
    delayMixDial_.setValue(delay.mix * 100.0f);
    delayTempoSyncToggle_.setToggleState(delay.tempoSync, juce::dontSendNotification);
    const auto dcFilter = processor_.getDcFilterSettings();
    dcFilterEnabledToggle_.setToggleState(dcFilter.enabled, juce::dontSendNotification);
    dcFilterCutoffDial_.setValue(dcFilter.cutoffHz, juce::dontSendNotification);
    const auto autopan = processor_.getAutopanSettings();
    autopanRateDial_.setValue(autopan.rateHz, juce::dontSendNotification);
    autopanDepthDial_.setValue(autopan.depth * 100.0f, juce::dontSendNotification);
    const auto saturation = processor_.getSaturationSettings();
    saturationDriveDial_.setValue(saturation.drive * 100.0f, juce::dontSendNotification);
    saturationModeCombo_.setSelectedId(static_cast<int>(saturation.mode) + 1, juce::dontSendNotification);

    const auto velCurve = processor_.getVelocityCurve();
    const bool isEditingVelocityCurve = velocityCurveCombo_.hasKeyboardFocus(true) || velocityCurveCombo_.isPopupActive();
    if (!isEditingVelocityCurve)
    {
        velocityCurveCombo_.setSelectedId(
            velCurve == AudiocityAudioProcessor::VelocityCurve::soft ? 2
                : (velCurve == AudiocityAudioProcessor::VelocityCurve::hard ? 3 : 1),
            juce::dontSendNotification);
    }

    // Reverse / Fade
    reverseToggle_.setToggleState(processor_.getReversePlayback(), juce::dontSendNotification);
    fadeInDial_.setValue(processor_.getFadeInSamples());
    fadeOutDial_.setValue(processor_.getFadeOutSamples());

    updateDiagnosticsStatusText();
}

void AudiocityAudioProcessorEditor::updateDiagnosticsStatusText()
{
    diagnosticsLabel_.setText(
        "Preload: " + juce::String(processor_.getLoadedPreloadSamples())
            + " | Stream: " + juce::String(processor_.getLoadedStreamSamples())
            + " | Rebuilds: " + juce::String(processor_.getSegmentRebuildCount())
            + " | Voices: " + juce::String(processor_.getActiveVoiceCount())
            + "/" + juce::String(processor_.getPolyphonyLimit())
            + " | Root: " + juce::String(processor_.getRootMidiNote())
            + " | Length: " + juce::String(processor_.getLoadedSampleLength()),
        juce::dontSendNotification);
}

// ─── CC sync ───────────────────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::syncCcMappingsFromProcessor()
{
    const auto mappings = processor_.getAllCcMappings();
    for (auto& [dial, paramId] : allDials_)
    {
        const auto cc = processor_.getCcForParam(paramId);
        if (cc >= 0)
            dial->assignCc(cc);
        else
            dial->clearCc();
    }
}

// ─── Apply helpers ─────────────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::pushPlaybackWindow()
{
    const auto ps = juce::jmax(0, static_cast<int>(playbackStartDial_.getValue()));
    const auto pe = juce::jmax(ps + 1, static_cast<int>(playbackEndDial_.getValue()));
    processor_.setSampleWindow(ps, pe);

    // Update waveform view to show new playback bounds
    const auto sampleLength = processor_.getLoadedSampleLength();
    waveformView_.setState(sampleLength, getLoadedSampleWaveformMinMaxByChannel(cachedWaveformPeakResolution_ > 0 ? cachedWaveformPeakResolution_ : 2048),
        ps, pe, processor_.getLoopStart(), processor_.getLoopEnd(),
        processor_.getLoadedSampleLoopFormatBadge());
}

void AudiocityAudioProcessorEditor::enforcePlaybackLoopConstraints()
{
    auto pbStart  = static_cast<int>(playbackStartDial_.getValue());
    auto pbEnd    = static_cast<int>(playbackEndDial_.getValue());
    const auto ls = static_cast<int>(loopStartDial_.getValue());
    const auto le = static_cast<int>(loopEndDial_.getValue());

    // Playback start must stay at or before loop start
    if (pbStart > ls)
        playbackStartDial_.setValue(ls);

    // Playback end must stay at or after loop end
    if (pbEnd < le)
        playbackEndDial_.setValue(le);
}

void AudiocityAudioProcessorEditor::applyLoopPoints()
{
    const auto ls = juce::jmax(0, static_cast<int>(loopStartDial_.getValue()));
    const auto le = juce::jmax(ls + 1, static_cast<int>(loopEndDial_.getValue()));
    processor_.setLoopPoints(ls, le);

    const auto appliedLoopStart = processor_.getLoopStart();
    const auto appliedLoopEnd = processor_.getLoopEnd();
    loopStartDial_.setValue(appliedLoopStart, juce::dontSendNotification);
    loopEndDial_.setValue(appliedLoopEnd, juce::dontSendNotification);

    // Auto-switch to Loop mode when applying loop points
    if (processor_.getPlaybackMode() != AudiocityAudioProcessor::PlaybackMode::loop)
    {
        processor_.setPlaybackMode(AudiocityAudioProcessor::PlaybackMode::loop);
        playbackModeLoopButton_.setToggleState(true, juce::dontSendNotification);
    }

    const auto sampleLength = processor_.getLoadedSampleLength();
    waveformView_.setState(sampleLength, getLoadedSampleWaveformMinMaxByChannel(cachedWaveformPeakResolution_ > 0 ? cachedWaveformPeakResolution_ : 2048),
        processor_.getSampleWindowStart(), processor_.getSampleWindowEnd(),
        appliedLoopStart, appliedLoopEnd,
        processor_.getLoadedSampleLoopFormatBadge());
}

void AudiocityAudioProcessorEditor::pushAmpEnvelope()
{
    AudiocityAudioProcessor::AdsrSettings adsr;
    adsr.attackSeconds = juce::jmax(0.0001f, static_cast<float>(ampAttackDial_.getValue()) / 1000.0f);
    adsr.decaySeconds = juce::jmax(0.0001f, static_cast<float>(ampDecayDial_.getValue()) / 1000.0f);
    adsr.sustainLevel = juce::jlimit(0.0f, 1.0f, static_cast<float>(ampSustainDial_.getValue()));
    adsr.releaseSeconds = juce::jmax(0.0001f, static_cast<float>(ampReleaseDial_.getValue()) / 1000.0f);
    processor_.setAmpEnvelope(adsr);
}

void AudiocityAudioProcessorEditor::pushAmpLfoSettings()
{
    AudiocityAudioProcessor::AmpLfoSettings settings;
    settings.rateHz = juce::jlimit(0.0f, 40.0f, static_cast<float>(ampLfoRateDial_.getValue()));
    settings.depth = juce::jlimit(0.0f, 1.0f, static_cast<float>(ampLfoDepthDial_.getValue()) / 100.0f);
    settings.shape = comboIdToLfoShape(ampLfoShapeCombo_.getSelectedId());
    processor_.setAmpLfoSettings(settings);
}

void AudiocityAudioProcessorEditor::pushDelaySettings()
{
    AudiocityAudioProcessor::DelaySettings settings;
    settings.timeMs = juce::jlimit(1.0f, 2000.0f, static_cast<float>(delayTimeDial_.getValue()));
    settings.feedback = juce::jlimit(0.0f, 0.95f, static_cast<float>(delayFeedbackDial_.getValue()) / 100.0f);
    settings.mix = juce::jlimit(0.0f, 1.0f, static_cast<float>(delayMixDial_.getValue()) / 100.0f);
    settings.tempoSync = delayTempoSyncToggle_.getToggleState();
    processor_.setDelaySettings(settings);
}

void AudiocityAudioProcessorEditor::pushDcFilterSettings()
{
    AudiocityAudioProcessor::DcFilterSettings settings;
    settings.enabled = dcFilterEnabledToggle_.getToggleState();
    settings.cutoffHz = juce::jlimit(5.0f, 20.0f, static_cast<float>(dcFilterCutoffDial_.getValue()));
    processor_.setDcFilterSettings(settings);
}

void AudiocityAudioProcessorEditor::pushAutopanSettings()
{
    AudiocityAudioProcessor::AutopanSettings settings;
    settings.rateHz = juce::jlimit(0.01f, 20.0f, static_cast<float>(autopanRateDial_.getValue()));
    settings.depth = juce::jlimit(0.0f, 1.0f, static_cast<float>(autopanDepthDial_.getValue()) / 100.0f);
    processor_.setAutopanSettings(settings);
}

void AudiocityAudioProcessorEditor::pushSaturationSettings()
{
    AudiocityAudioProcessor::SaturationSettings settings;
    settings.drive = juce::jlimit(0.0f, 1.0f, static_cast<float>(saturationDriveDial_.getValue()) / 100.0f);
    settings.mode = static_cast<AudiocityAudioProcessor::SaturationSettings::Mode>(juce::jlimit(0, 3,
        saturationModeCombo_.getSelectedId() - 1));
    processor_.setSaturationSettings(settings);
}

void AudiocityAudioProcessorEditor::pushPitchLfoSettings()
{
    AudiocityAudioProcessor::PitchLfoSettings settings;
    settings.rateHz = juce::jlimit(0.0f, 40.0f, static_cast<float>(pitchLfoRateDial_.getValue()));
    settings.depthCents = juce::jlimit(0.0f, 100.0f, static_cast<float>(pitchLfoDepthDial_.getValue()));
    processor_.setPitchLfoSettings(settings);
}

void AudiocityAudioProcessorEditor::pushFilterSettings()
{
    AudiocityAudioProcessor::FilterSettings settings;
    settings.baseCutoffHz = juce::jmax(20.0f, static_cast<float>(filterCutoffDial_.getValue()));
    settings.resonance = juce::jlimit(0.0f, 1.0f, static_cast<float>(filterResDial_.getValue()) / 100.0f);
    settings.envAmountHz = juce::jmax(0.0f, static_cast<float>(filterEnvAmtDial_.getValue()));
    settings.mode = comboIdToFilterMode(filterTypeCombo_.getSelectedId());
    settings.keyTracking = juce::jlimit(-1.0f, 2.0f, static_cast<float>(filterKeytrackDial_.getValue()) / 100.0f);
    settings.velocityAmountHz = juce::jmax(0.0f, static_cast<float>(filterVelDial_.getValue()));
    settings.lfoRateHz = juce::jlimit(0.0f, 40.0f, static_cast<float>(filterLfoRateDial_.getValue()));
    settings.lfoRateKeyTracking = 0.0f;
    settings.lfoAmountHz = juce::jlimit(-20000.0f, 20000.0f, static_cast<float>(filterLfoAmtDial_.getValue()));
    settings.lfoAmountKeyTracking = 0.0f;
    settings.lfoStartPhaseDegrees = 0.0f;
    settings.lfoStartPhaseRandomDegrees = 0.0f;
    settings.lfoFadeInMs = 0.0f;
    settings.lfoShape = comboIdToLfoShape(filterLfoShapeCombo_.getSelectedId());
    settings.lfoRetrigger = filterLfoRetriggerToggle_.getToggleState();
    settings.lfoTempoSync = filterLfoTempoSyncToggle_.getToggleState();
    settings.lfoRateKeytrackInTempoSync = true;
    settings.lfoKeytrackLinear = false;
    settings.lfoUnipolar = false;
    settings.lfoSyncDivision = juce::jlimit(0, 11, filterLfoDivisionCombo_.getSelectedId() - 1);
    processor_.setFilterSettings(settings);
    updateFilterResponseGraphFromControls();
}

void AudiocityAudioProcessorEditor::pushFilterEnvelope()
{
    AudiocityAudioProcessor::AdsrSettings adsr;
    adsr.attackSeconds = juce::jmax(0.0001f, static_cast<float>(filterAttackDial_.getValue()) / 1000.0f);
    adsr.decaySeconds = juce::jmax(0.0001f, static_cast<float>(filterDecayDial_.getValue()) / 1000.0f);
    adsr.sustainLevel = juce::jlimit(0.0f, 1.0f, static_cast<float>(filterSustainDial_.getValue()));
    adsr.releaseSeconds = juce::jmax(0.0001f, static_cast<float>(filterReleaseDial_.getValue()) / 1000.0f);
    processor_.setFilterEnvelope(adsr);
}

void AudiocityAudioProcessorEditor::pushPerformanceControls()
{
    const auto mono = monoToggle_.getToggleState();
    processor_.setMonoMode(mono);
    legatoToggle_.setEnabled(mono);

    if (!mono && legatoToggle_.getToggleState())
    {
        legatoToggle_.setToggleState(false, juce::dontSendNotification);
        processor_.setLegatoMode(false);
    }
    else
    {
        processor_.setLegatoMode(legatoToggle_.getToggleState());
    }
}

audiocity::engine::SettingsSnapshot AudiocityAudioProcessorEditor::captureSettingsSnapshot() const
{
    int playbackModeIndex = 0;
    if (processor_.getPlaybackMode() == AudiocityAudioProcessor::PlaybackMode::oneShot)
        playbackModeIndex = 1;
    else if (processor_.getPlaybackMode() == AudiocityAudioProcessor::PlaybackMode::loop)
        playbackModeIndex = 2;

    int qualityTierIndex = 1;
    if (processor_.getQualityTier() == AudiocityAudioProcessor::QualityTier::cpu)
        qualityTierIndex = 0;
    else if (processor_.getQualityTier() == AudiocityAudioProcessor::QualityTier::ultra)
        qualityTierIndex = 2;

    return {
        processor_.getPreloadSamples(),
        qualityTierIndex,
        playbackModeIndex,
        processor_.getCoarseTuneSemitones(),
        processor_.getFineTuneCents(),
        processor_.getPitchBendRangeSemitones(),
        processor_.getMonoMode(),
        processor_.getLegatoMode(),
        processor_.getGlideSeconds(),
        processor_.getPolyphonyLimit(),
        processor_.getSampleWindowStart(),
        processor_.getSampleWindowEnd(),
        processor_.getLoopStart(),
        processor_.getLoopEnd(),
        processor_.getFadeInSamples(),
        processor_.getFadeOutSamples(),
        processor_.getReversePlayback()
    };
}

void AudiocityAudioProcessorEditor::applySettingsSnapshot(const audiocity::engine::SettingsSnapshot& snapshot)
{
    processor_.setPreloadSamples(snapshot.preloadSamples);
    processor_.setCoarseTuneSemitones(snapshot.coarseTuneSemitones);
    processor_.setFineTuneCents(snapshot.fineTuneCents);
    processor_.setPitchBendRangeSemitones(snapshot.pitchBendRangeSemitones);
    processor_.setQualityTier(snapshot.qualityTierIndex == 0
        ? AudiocityAudioProcessor::QualityTier::cpu
        : (snapshot.qualityTierIndex == 2
            ? AudiocityAudioProcessor::QualityTier::ultra
            : AudiocityAudioProcessor::QualityTier::fidelity));
    processor_.setPlaybackMode(snapshot.playbackModeIndex == 1
        ? AudiocityAudioProcessor::PlaybackMode::oneShot
        : (snapshot.playbackModeIndex == 2
            ? AudiocityAudioProcessor::PlaybackMode::loop
            : AudiocityAudioProcessor::PlaybackMode::gate));
    processor_.setMonoMode(snapshot.monoEnabled);
    processor_.setLegatoMode(snapshot.legatoEnabled);
    processor_.setGlideSeconds(snapshot.glideSeconds);
    processor_.setPolyphonyLimit(snapshot.polyphonyLimit);
    processor_.setSampleWindow(snapshot.sampleWindowStart, snapshot.sampleWindowEnd);
    processor_.setLoopPoints(snapshot.loopStart, snapshot.loopEnd);
    processor_.setFadeSamples(snapshot.fadeInSamples, snapshot.fadeOutSamples);
    processor_.setReversePlayback(snapshot.reversePlayback);

    refreshUI();
}

