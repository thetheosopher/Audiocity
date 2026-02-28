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

// ─── WaveformView ──────────────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::WaveformView::setState(
    const int totalSamples, std::vector<std::vector<float>> peaksByChannel,
    const int playbackStart, const int playbackEnd,
    const int loopStart, const int loopEnd,
    juce::String loopFormatBadge)
{
    totalSamples_ = juce::jmax(0, totalSamples);
    peaksByChannel_ = std::move(peaksByChannel);
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

void AudiocityAudioProcessorEditor::WaveformView::resetView()
{
    viewStartSample_ = 0;
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

    if (totalSamples_ <= 0 || peaksByChannel_.empty())
    {
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.drawText("Load a sample (WAV/AIFF)", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto bounds = getLocalBounds().toFloat();
    const auto channelCount = juce::jmax(1, static_cast<int>(peaksByChannel_.size()));
    const auto channelHeight = bounds.getHeight() / static_cast<float>(channelCount);

    // ── Playback region markers ──
    const auto pbX1 = xFromSample(playbackStart_);
    const auto pbX2 = xFromSample(playbackEnd_);

    // ── Loop region markers ──
    const auto lx1 = xFromSample(loopStart_);
    const auto lx2 = xFromSample(loopEnd_);
    const auto loopLeft = juce::jmin(lx1, lx2);
    const auto loopRight = juce::jmax(lx1, lx2);

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

        const auto& peaks = peaksByChannel_[static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(peaksByChannel_.size()) - 1, channel))];
        const auto peakCount = static_cast<int>(peaks.size());
        if (peakCount > 0)
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
                const auto peakPos = norm * static_cast<float>(juce::jmax(0, peakCount - 1));
                const auto i0 = juce::jlimit(0, peakCount - 1, static_cast<int>(std::floor(peakPos)));
                const auto i1 = juce::jlimit(0, peakCount - 1, i0 + 1);
                const auto frac = peakPos - static_cast<float>(i0);

                const auto a0 = peaks[static_cast<std::size_t>(i0)];
                const auto a1 = peaks[static_cast<std::size_t>(i1)];
                const auto ampNorm = juce::jlimit(0.0f, 1.0f, a0 + (a1 - a0) * frac);
                const auto amp = ampNorm * lane.getHeight() * 0.42f;

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

        g.setColour(juce::Colours::limegreen.withAlpha(0.9f));
        g.drawLine(pbX1, lane.getY(), pbX1, lane.getBottom(), 1.5f);
        g.drawLine(pbX2, lane.getY(), pbX2, lane.getBottom(), 1.5f);

        g.setColour(juce::Colours::orange.withAlpha(0.9f));
        g.drawLine(loopLeft, lane.getY(), loopLeft, lane.getBottom(), 1.5f);
        g.drawLine(loopRight, lane.getY(), loopRight, lane.getBottom(), 1.5f);
    }

    // ── Labels on handles ──
    g.setFont(10.0f);

    // Playback labels (green, at bottom)
    g.setColour(juce::Colours::limegreen);
    g.drawText("P", static_cast<int>(pbX1) - 6, static_cast<int>(bounds.getBottom()) - 14, 12, 12, juce::Justification::centred);
    g.drawText("P", static_cast<int>(pbX2) - 6, static_cast<int>(bounds.getBottom()) - 14, 12, 12, juce::Justification::centred);

    // Loop labels (orange, at top)
    g.setColour(juce::Colours::orange);
    g.drawText("S", static_cast<int>(loopLeft) - 6, static_cast<int>(bounds.getY()) + 2, 12, 12, juce::Justification::centred);
    g.drawText("E", static_cast<int>(loopRight) - 6, static_cast<int>(bounds.getY()) + 2, 12, 12, juce::Justification::centred);

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
    if (event.mods.isRightButtonDown() || event.mods.isMiddleButtonDown())
    {
        dragMode_ = DragMode::pan;
        dragAnchorViewStart_ = viewStartSample_;
        return;
    }

    // Check proximity to all four handles — pick the nearest one
    struct Handle { DragMode mode; float x; };
    const Handle handles[] = {
        { DragMode::dragPlaybackStart, xFromSample(playbackStart_) },
        { DragMode::dragPlaybackEnd,   xFromSample(playbackEnd_) },
        { DragMode::dragLoopStart,     xFromSample(loopStart_) },
        { DragMode::dragLoopEnd,       xFromSample(loopEnd_) },
    };

    constexpr float kHandlePickPx = 8.0f;
    float bestDist = kHandlePickPx + 1.0f;
    DragMode bestMode = DragMode::none;

    for (const auto& h : handles)
    {
        const auto dist = std::abs(event.position.x - h.x);
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

    if (dragMode_ == DragMode::dragLoopStart)
    {
        // Loop start must stay >= playbackStart_ and < loopEnd_
        loopStart_ = juce::jlimit(playbackStart_, juce::jmax(playbackStart_, loopEnd_ - 1), sample);
        if (onLoopPreview)
            onLoopPreview(loopStart_, loopEnd_);
    }
    else if (dragMode_ == DragMode::dragLoopEnd)
    {
        // Loop end must stay <= playbackEnd_ and > loopStart_
        loopEnd_ = juce::jlimit(loopStart_ + 1, juce::jmax(loopStart_ + 1, playbackEnd_), sample);
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

void AudiocityAudioProcessorEditor::WaveformView::mouseUp(const juce::MouseEvent&)
{
    if (dragMode_ == DragMode::dragLoopStart || dragMode_ == DragMode::dragLoopEnd)
    {
        if (onLoopCommitted)
            onLoopCommitted(loopStart_, loopEnd_);
    }
    else if (dragMode_ == DragMode::dragPlaybackStart || dragMode_ == DragMode::dragPlaybackEnd)
    {
        if (onPlaybackCommitted)
            onPlaybackCommitted(playbackStart_, playbackEnd_);
    }

    dragMode_ = DragMode::none;
}

void AudiocityAudioProcessorEditor::WaveformView::mouseDoubleClick(const juce::MouseEvent&)
{
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
    setSize(940, 800);
    setLookAndFeel(&dialLaf_);

    addAndMakeVisible(tabBar_);
    tabBar_.setLookAndFeel(&tabTextLookAndFeel);
    tabBar_.setTabBarDepth(30);
    tabBar_.addTab("Sample", juce::Colour(0xff252538), &tabSamplePage_, false);
    tabBar_.addTab("Library", juce::Colour(0xff252538), &tabLibraryPage_, false);
    tabBar_.setCurrentTabIndex(0);
    currentTabIndex_ = 0;

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

    // Sample info row
    addAndMakeVisible(samplePathLabel_);
    samplePathLabel_.setJustificationType(juce::Justification::centredLeft);
    samplePathLabel_.setText("No sample loaded", juce::dontSendNotification);

    addAndMakeVisible(loadButton_);
    loadButton_.setTooltip("Load Sample");
    loadButton_.onClick = [this] { openSampleChooser(); };

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

    // Waveform
    addAndMakeVisible(waveformView_);
    waveformView_.onLoopPreview = [this](const int ls, const int le)
    {
        loopStartDial_.setValue(ls);
        loopEndDial_.setValue(le);
    };
    waveformView_.onLoopCommitted = [this](const int ls, const int le)
    {
        loopStartDial_.setValue(ls);
        loopEndDial_.setValue(le);
        applyLoopPoints();
    };
    waveformView_.onPlaybackPreview = [this](const int ps, const int pe)
    {
        playbackStartDial_.setValue(ps);
        playbackEndDial_.setValue(pe);
    };
    waveformView_.onPlaybackCommitted = [this](const int ps, const int pe)
    {
        playbackStartDial_.setValue(ps);
        playbackEndDial_.setValue(pe);
        pushPlaybackWindow();
    };

    // Playback mode
    addAndMakeVisible(playbackModeLabel_);
    addAndMakeVisible(playbackModeCombo_);
    playbackModeCombo_.addItem("Gate", 1);
    playbackModeCombo_.addItem("One-shot", 2);
    playbackModeCombo_.addItem("Loop", 3);
    playbackModeCombo_.setSelectedId(1, juce::dontSendNotification);
    playbackModeCombo_.onChange = [this]
    {
        const auto mode = playbackModeCombo_.getSelectedId() - 1;
        processor_.setPlaybackMode(
            mode == 1 ? AudiocityAudioProcessor::PlaybackMode::oneShot
                : (mode == 2 ? AudiocityAudioProcessor::PlaybackMode::loop
                             : AudiocityAudioProcessor::PlaybackMode::gate));
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

    // Performance
    addAndMakeVisible(monoToggle_);
    addAndMakeVisible(legatoToggle_);
    monoToggle_.onClick = [this] { pushPerformanceControls(); };
    legatoToggle_.onClick = [this] { pushPerformanceControls(); };

    addAndMakeVisible(glideDial_);
    glideDial_.onValueChange = [this]
    {
        processor_.setGlideSeconds(static_cast<float>(glideDial_.getValue()) / 1000.0f);
    };

    addAndMakeVisible(chokeGroupDial_);
    chokeGroupDial_.onValueChange = [this]
    {
        processor_.setChokeGroup(static_cast<int>(chokeGroupDial_.getValue()));
    };

    // Amp ADSR
    addAndMakeVisible(ampAttackDial_);
    addAndMakeVisible(ampDecayDial_);
    addAndMakeVisible(ampSustainDial_);
    addAndMakeVisible(ampReleaseDial_);
    ampAttackDial_.onValueChange = [this] { pushAmpEnvelope(); };
    ampDecayDial_.onValueChange = [this] { pushAmpEnvelope(); };
    ampSustainDial_.onValueChange = [this] { pushAmpEnvelope(); };
    ampReleaseDial_.onValueChange = [this] { pushAmpEnvelope(); };

    // Filter
    addAndMakeVisible(filterCutoffDial_);
    addAndMakeVisible(filterResDial_);
    addAndMakeVisible(filterEnvAmtDial_);
    filterCutoffDial_.onValueChange = [this] { pushFilterSettings(); };
    filterResDial_.onValueChange = [this] { pushFilterSettings(); };
    filterEnvAmtDial_.onValueChange = [this] { pushFilterSettings(); };

    // Quality / Preload
    addAndMakeVisible(qualityLabel_);
    addAndMakeVisible(qualityCombo_);
    qualityCombo_.addItem("CPU", 1);
    qualityCombo_.addItem("Fidelity", 2);
    qualityCombo_.setSelectedId(2, juce::dontSendNotification);
    qualityCombo_.onChange = [this]
    {
        processor_.setQualityTier(qualityCombo_.getSelectedId() == 1
            ? AudiocityAudioProcessor::QualityTier::cpu
            : AudiocityAudioProcessor::QualityTier::fidelity);
    };

    addAndMakeVisible(preloadDial_);
    preloadDial_.onValueChange = [this]
    {
        processor_.setPreloadSamples(juce::jmax(256, static_cast<int>(preloadDial_.getValue())));
        refreshUI();
    };

    // Reverse / Fade
    addAndMakeVisible(reverseToggle_);
    reverseToggle_.onClick = [this]
    {
        processor_.setReversePlayback(reverseToggle_.getToggleState());
    };

    addAndMakeVisible(fadeInDial_);
    addAndMakeVisible(fadeOutDial_);
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
        { &glideDial_,          "glide" },
        { &chokeGroupDial_,     "chokeGroup" },
        { &ampAttackDial_,      "ampAttack" },
        { &ampDecayDial_,       "ampDecay" },
        { &ampSustainDial_,     "ampSustain" },
        { &ampReleaseDial_,     "ampRelease" },
        { &filterCutoffDial_,   "filterCutoff" },
        { &filterResDial_,      "filterRes" },
        { &filterEnvAmtDial_,   "filterEnvAmt" },
        { &preloadDial_,        "preload" },
        { &fadeInDial_,         "fadeIn" },
        { &fadeOutDial_,        "fadeOut" },
    };

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
    tabBar_.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

// ─── Timer: poll CC FIFO ───────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::timerCallback()
{
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
            if (ext == ".wav" || ext == ".aiff" || ext == ".aif")
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
        updateTabVisibility();
        resized();
        repaint();
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
}

void AudiocityAudioProcessorEditor::updateTabVisibility()
{
    const bool showSampleTab = (currentTabIndex_ == 0);
    const bool showLibraryTab = !showSampleTab;

    sampleBrowserRootLabel_.setVisible(showLibraryTab);
    sampleBrowserChooseRootButton_.setVisible(showLibraryTab);
    sampleBrowserFilterEditor_.setVisible(showLibraryTab);
    sampleBrowserSortCombo_.setVisible(showLibraryTab);
    sampleBrowserListBox_.setVisible(showLibraryTab);
    sampleBrowserCountLabel_.setVisible(showLibraryTab);

    samplePathLabel_.setVisible(showSampleTab);
    loadButton_.setVisible(showSampleTab);
    rootNoteLabel_.setVisible(showSampleTab);
    rootNoteCombo_.setVisible(showSampleTab);
    waveformView_.setVisible(showSampleTab);
    playbackModeLabel_.setVisible(showSampleTab);
    playbackModeCombo_.setVisible(showSampleTab);
    reverseToggle_.setVisible(showSampleTab);
    playbackStartDial_.setVisible(showSampleTab);
    playbackEndDial_.setVisible(showSampleTab);
    loopStartDial_.setVisible(showSampleTab);
    loopEndDial_.setVisible(showSampleTab);
    monoToggle_.setVisible(showSampleTab);
    legatoToggle_.setVisible(showSampleTab);
    glideDial_.setVisible(showSampleTab);
    chokeGroupDial_.setVisible(showSampleTab);
    ampAttackDial_.setVisible(showSampleTab);
    ampDecayDial_.setVisible(showSampleTab);
    ampSustainDial_.setVisible(showSampleTab);
    ampReleaseDial_.setVisible(showSampleTab);
    filterCutoffDial_.setVisible(showSampleTab);
    filterResDial_.setVisible(showSampleTab);
    filterEnvAmtDial_.setVisible(showSampleTab);
    fadeInDial_.setVisible(showSampleTab);
    fadeOutDial_.setVisible(showSampleTab);
    qualityLabel_.setVisible(showSampleTab);
    qualityCombo_.setVisible(showSampleTab);
    preloadDial_.setVisible(showSampleTab);
    diagnosticsLabel_.setVisible(showSampleTab);
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

void AudiocityAudioProcessorEditor::listBoxItemClicked(const int row, const juce::MouseEvent&)
{
    loadSampleFromBrowserRow(row);
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
    return ext == ".wav" || ext == ".aif" || ext == ".aiff";
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

    const auto& file = allSampleEntries_[static_cast<std::size_t>(sourceIndex)].file;
    if (processor_.loadSampleFromFile(file))
    {
        tabBar_.setCurrentTabIndex(0);
        currentTabIndex_ = 0;
        updateTabVisibility();
        resized();
        repaint();
        refreshUI();
    }
}

void AudiocityAudioProcessorEditor::paintSampleBrowserPane(
    juce::Graphics& g, const juce::Rectangle<int> browserArea) const
{
    g.setColour(juce::Colour(0xff252538));
    g.fillRoundedRectangle(browserArea.toFloat(), 6.0f);
    g.setColour(juce::Colour(0xff3a3a52));
    g.drawRoundedRectangle(browserArea.toFloat().reduced(0.5f), 6.0f, 1.0f);
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
        sampleBrowserCountLabel_.setBounds(listArea.removeFromBottom(20));
        listArea.removeFromBottom(4);
        sampleBrowserListBox_.setBounds(listArea);
        return;
    }

    // ── Top bar: Load button + file path ──
    {
        auto topRow = area.removeFromTop(kTopBarH);
        loadButton_.setBounds(topRow.removeFromRight(32));
        topRow.removeFromRight(8);
        samplePathLabel_.setBounds(topRow);
    }

    area.removeFromTop(kGrpGap);

    const auto controlsTotal = (kRowH * 3) + (kGrpGap * 3) + 20;
    const auto waveformHeight = juce::jmax(160, area.getHeight() - controlsTotal);

    // ── Waveform (fills all available space between top row and controls) ──
    waveformView_.setBounds(area.removeFromTop(waveformHeight));
    area.removeFromTop(kGrpGap);

    // Helper: register a group box and return the inner content area
    auto makeGroup = [this](const juce::String& title,
                            juce::Rectangle<int> bounds) -> juce::Rectangle<int>
    {
        groupBoxes_.push_back({ title, bounds });
        return bounds.withTrimmedTop(kGrpHdr).reduced(kGrpPadH, kGrpPadV);
    };

    // ── Row 1: Trim | Performance ──
    {
        auto row = area.removeFromTop(kRowH);
        const auto leftW = (row.getWidth() - kGrpGap) * 2 / 5;

        auto trimInner = makeGroup("Trim", row.removeFromLeft(leftW));
        row.removeFromLeft(kGrpGap);
        auto perfInner = makeGroup("Performance", row);

        // Trim: Pb Start + Pb End
        playbackStartDial_.setBounds(trimInner.removeFromLeft(kDial));
        trimInner.removeFromLeft(kDialGap);
        playbackEndDial_.setBounds(trimInner.removeFromLeft(kDial));

        // Performance: stacked toggles, then Glide + Choke dials + Root Note selector
        auto toggleCol = perfInner.removeFromLeft(80);
        monoToggle_.setBounds(toggleCol.removeFromTop(28));
        toggleCol.removeFromTop(4);
        legatoToggle_.setBounds(toggleCol.removeFromTop(28));
        toggleCol.removeFromTop(4);
        reverseToggle_.setBounds(toggleCol.removeFromTop(28));
        perfInner.removeFromLeft(kDialGap);
        glideDial_.setBounds(perfInner.removeFromLeft(kDial));
        perfInner.removeFromLeft(kDialGap);
        chokeGroupDial_.setBounds(perfInner.removeFromLeft(kDial));
        perfInner.removeFromLeft(kDialGap + 6);
        auto rootArea = perfInner.removeFromLeft(150);
        rootNoteLabel_.setBounds(rootArea.removeFromTop(16));
        rootArea.removeFromTop(2);
        rootNoteCombo_.setBounds(rootArea.removeFromTop(28));
    }

    area.removeFromTop(kGrpGap);

    // ── Row 2: Loop | Amplitude Envelope ──
    {
        auto row = area.removeFromTop(kRowH);
        const auto leftW = (row.getWidth() - kGrpGap) / 2;

        auto loopInner = makeGroup("Loop", row.removeFromLeft(leftW));
        row.removeFromLeft(kGrpGap);
        auto ampInner = makeGroup("Amplitude Envelope", row);

        // Loop: Loop Start + Loop End + Mode combo
        loopStartDial_.setBounds(loopInner.removeFromLeft(kDial));
        loopInner.removeFromLeft(kDialGap);
        loopEndDial_.setBounds(loopInner.removeFromLeft(kDial));
        loopInner.removeFromLeft(kDialGap + 8);
        auto modeArea = loopInner.removeFromLeft(100);
        playbackModeLabel_.setBounds(modeArea.removeFromTop(16));
        modeArea.removeFromTop(2);
        playbackModeCombo_.setBounds(modeArea.removeFromTop(26));

        // Amp: A D S R
        ampAttackDial_.setBounds(ampInner.removeFromLeft(kDial));
        ampInner.removeFromLeft(kDialGap);
        ampDecayDial_.setBounds(ampInner.removeFromLeft(kDial));
        ampInner.removeFromLeft(kDialGap);
        ampSustainDial_.setBounds(ampInner.removeFromLeft(kDial));
        ampInner.removeFromLeft(kDialGap);
        ampReleaseDial_.setBounds(ampInner.removeFromLeft(kDial));
    }

    area.removeFromTop(kGrpGap);

    // ── Row 3: Filter | Output ──
    {
        auto row = area.removeFromTop(kRowH);
        const auto leftW = (row.getWidth() - kGrpGap) * 2 / 5;

        auto filterInner = makeGroup("Filter", row.removeFromLeft(leftW));
        row.removeFromLeft(kGrpGap);
        auto outInner = makeGroup("Output", row);

        // Filter: Cutoff + Res + Env
        filterCutoffDial_.setBounds(filterInner.removeFromLeft(kDial));
        filterInner.removeFromLeft(kDialGap);
        filterResDial_.setBounds(filterInner.removeFromLeft(kDial));
        filterInner.removeFromLeft(kDialGap);
        filterEnvAmtDial_.setBounds(filterInner.removeFromLeft(kDial));

        // Output: FadeIn, FadeOut, Preload, Quality
        fadeInDial_.setBounds(outInner.removeFromLeft(kDial));
        outInner.removeFromLeft(kDialGap);
        fadeOutDial_.setBounds(outInner.removeFromLeft(kDial));
        outInner.removeFromLeft(kDialGap + 8);

        preloadDial_.setBounds(outInner.removeFromLeft(kDial));
        outInner.removeFromLeft(kDial + 6);

        auto qualArea = outInner.removeFromLeft(100);
        qualityLabel_.setBounds(qualArea.removeFromTop(16));
        qualArea.removeFromTop(2);
        qualityCombo_.setBounds(qualArea.removeFromTop(26));
    }

    area.removeFromTop(kGrpGap);

    // ── Diagnostics ──
    diagnosticsLabel_.setBounds(area.removeFromTop(20));
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
    else
        paintGroupBoxes(g);

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
    g.drawText("Drop .wav or .aiff to load", overlay, juce::Justification::centred);
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

    rootNoteLabel_.setTooltip(
        "Root Note - MIDI note number and pitch name for the sample's original pitch");
    rootNoteCombo_.setTooltip(
        "Root Note - MIDI note number and pitch name for the sample's original pitch");
    playbackStartDial_.setLabelTooltip(
        "Playback Start - Sample position where playback begins");
    playbackEndDial_.setLabelTooltip(
        "Playback End - Sample position where playback ends");
    loopStartDial_.setLabelTooltip(
        "Loop Start - Sample position where the loop region begins");
    loopEndDial_.setLabelTooltip(
        "Loop End - Sample position where the loop region ends");
    glideDial_.setLabelTooltip(
        "Glide Time - Portamento time between notes in milliseconds");
    chokeGroupDial_.setLabelTooltip(
        "Choke Group - Voices in the same group cut each other off");
    ampAttackDial_.setLabelTooltip(
        "Attack - Amplitude envelope attack time in milliseconds");
    ampDecayDial_.setLabelTooltip(
        "Decay - Amplitude envelope decay time in milliseconds");
    ampSustainDial_.setLabelTooltip(
        "Sustain - Amplitude envelope sustain level (0 to 1)");
    ampReleaseDial_.setLabelTooltip(
        "Release - Amplitude envelope release time in milliseconds");
    filterCutoffDial_.setLabelTooltip(
        "Filter Cutoff - Low-pass filter frequency in Hz");
    filterResDial_.setLabelTooltip(
        "Resonance - Filter emphasis at the cutoff frequency");
    filterEnvAmtDial_.setLabelTooltip(
        "Envelope Amount - Filter envelope modulation depth in Hz");
    fadeInDial_.setLabelTooltip(
        "Fade In - Number of samples to fade in at playback start");
    fadeOutDial_.setLabelTooltip(
        "Fade Out - Number of samples to fade out at playback end");
    preloadDial_.setLabelTooltip(
        "Preload - Number of samples buffered before streaming begins");

    monoToggle_.setTooltip(
        "Monophonic - Limit to a single voice at a time");
    legatoToggle_.setTooltip(
        "Legato - Keep the envelope running between overlapping notes");
    reverseToggle_.setTooltip(
        "Reverse - Play the sample backwards");
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
        if (ext == ".wav" || ext == ".aiff" || ext == ".aif")
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
    fileChooser_ = std::make_unique<juce::FileChooser>("Load sample", juce::File{}, "*.wav;*.aiff;*.aif");

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

// ─── Refresh UI ────────────────────────────────────────────────────────────────

void AudiocityAudioProcessorEditor::refreshUI(const bool forceWaveformReset)
{
    const auto path = processor_.getLoadedSamplePath();
    samplePathLabel_.setText(path.isNotEmpty() ? path : "No sample loaded", juce::dontSendNotification);
    const auto isNewLoadedSample = path.isNotEmpty() && path != lastWaveformSamplePath_;

    rootNoteCombo_.setSelectedId(processor_.getRootMidiNote() + 1, juce::dontSendNotification);

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
            || cachedWaveformPeaksByChannel_.empty()
            || cachedWaveformPeakResolution_ != targetPeakResolution);

    if (sampleLength <= 0)
    {
        cachedWaveformPeaksByChannel_.clear();
        cachedWaveformPeakResolution_ = 0;
    }
    else if (shouldRefreshPeaks)
    {
        cachedWaveformPeaksByChannel_ = processor_.getLoadedSamplePeaksByChannel(targetPeakResolution);
        cachedWaveformPeakResolution_ = targetPeakResolution;
    }

    waveformView_.setState(sampleLength, cachedWaveformPeaksByChannel_,
        processor_.getSampleWindowStart(), processor_.getSampleWindowEnd(),
        processor_.getLoopStart(), processor_.getLoopEnd(),
        processor_.getLoadedSampleLoopFormatBadge());

    if (forceWaveformReset || isNewLoadedSample)
        waveformView_.resetView();

    lastWaveformSamplePath_ = path;

    // Playback mode
    int playbackId = 1;
    if (processor_.getPlaybackMode() == AudiocityAudioProcessor::PlaybackMode::oneShot)
        playbackId = 2;
    else if (processor_.getPlaybackMode() == AudiocityAudioProcessor::PlaybackMode::loop)
        playbackId = 3;
    playbackModeCombo_.setSelectedId(playbackId, juce::dontSendNotification);

    // Loop points
    loopStartDial_.setValue(processor_.getLoopStart());
    loopEndDial_.setValue(processor_.getLoopEnd());

    // Playback window
    playbackStartDial_.setValue(processor_.getSampleWindowStart());
    playbackEndDial_.setValue(processor_.getSampleWindowEnd());

    // Performance
    monoToggle_.setToggleState(processor_.getMonoMode(), juce::dontSendNotification);
    legatoToggle_.setToggleState(processor_.getLegatoMode(), juce::dontSendNotification);
    legatoToggle_.setEnabled(processor_.getMonoMode());
    glideDial_.setValue(processor_.getGlideSeconds() * 1000.0f);
    chokeGroupDial_.setValue(processor_.getChokeGroup());

    // Amp ADSR
    const auto amp = processor_.getAmpEnvelope();
    ampAttackDial_.setValue(amp.attackSeconds * 1000.0f);
    ampDecayDial_.setValue(amp.decaySeconds * 1000.0f);
    ampSustainDial_.setValue(amp.sustainLevel);
    ampReleaseDial_.setValue(amp.releaseSeconds * 1000.0f);

    // Filter
    const auto filter = processor_.getFilterSettings();
    filterCutoffDial_.setValue(filter.baseCutoffHz);
    filterResDial_.setValue(static_cast<double>(filter.resonance) * 100.0);
    filterEnvAmtDial_.setValue(filter.envAmountHz);

    // Quality / Preload
    qualityCombo_.setSelectedId(
        processor_.getQualityTier() == AudiocityAudioProcessor::QualityTier::cpu ? 1 : 2,
        juce::dontSendNotification);
    preloadDial_.setValue(processor_.getPreloadSamples());

    // Reverse / Fade
    reverseToggle_.setToggleState(processor_.getReversePlayback(), juce::dontSendNotification);
    fadeInDial_.setValue(processor_.getFadeInSamples());
    fadeOutDial_.setValue(processor_.getFadeOutSamples());

    // Diagnostics
    diagnosticsLabel_.setText(
        "Preload: " + juce::String(processor_.getLoadedPreloadSamples())
            + " | Stream: " + juce::String(processor_.getLoadedStreamSamples())
            + " | Rebuilds: " + juce::String(processor_.getSegmentRebuildCount())
            + " | Root: " + juce::String(processor_.getRootMidiNote())
            + " | Length: " + juce::String(sampleLength),
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
    waveformView_.setState(sampleLength, processor_.getLoadedSamplePeaksByChannel(),
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

    // Auto-switch to Loop mode when applying loop points
    if (processor_.getPlaybackMode() != AudiocityAudioProcessor::PlaybackMode::loop)
    {
        processor_.setPlaybackMode(AudiocityAudioProcessor::PlaybackMode::loop);
        playbackModeCombo_.setSelectedId(3, juce::dontSendNotification);
    }

    refreshUI();
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

void AudiocityAudioProcessorEditor::pushFilterSettings()
{
    AudiocityAudioProcessor::FilterSettings settings;
    settings.baseCutoffHz = juce::jmax(20.0f, static_cast<float>(filterCutoffDial_.getValue()));
    settings.resonance = juce::jlimit(0.0f, 1.0f, static_cast<float>(filterResDial_.getValue()) / 100.0f);
    settings.envAmountHz = juce::jmax(0.0f, static_cast<float>(filterEnvAmtDial_.getValue()));
    processor_.setFilterSettings(settings);
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

    return {
        processor_.getPreloadSamples(),
        processor_.getQualityTier() == AudiocityAudioProcessor::QualityTier::cpu ? 0 : 1,
        playbackModeIndex,
        processor_.getMonoMode(),
        processor_.getLegatoMode(),
        processor_.getGlideSeconds(),
        processor_.getChokeGroup(),
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
    processor_.setQualityTier(snapshot.qualityTierIndex == 0
        ? AudiocityAudioProcessor::QualityTier::cpu
        : AudiocityAudioProcessor::QualityTier::fidelity);
    processor_.setPlaybackMode(snapshot.playbackModeIndex == 1
        ? AudiocityAudioProcessor::PlaybackMode::oneShot
        : (snapshot.playbackModeIndex == 2
            ? AudiocityAudioProcessor::PlaybackMode::loop
            : AudiocityAudioProcessor::PlaybackMode::gate));
    processor_.setMonoMode(snapshot.monoEnabled);
    processor_.setLegatoMode(snapshot.legatoEnabled);
    processor_.setGlideSeconds(snapshot.glideSeconds);
    processor_.setChokeGroup(snapshot.chokeGroup);
    processor_.setSampleWindow(snapshot.sampleWindowStart, snapshot.sampleWindowEnd);
    processor_.setLoopPoints(snapshot.loopStart, snapshot.loopEnd);
    processor_.setFadeSamples(snapshot.fadeInSamples, snapshot.fadeOutSamples);
    processor_.setReversePlayback(snapshot.reversePlayback);

    refreshUI();
}

