#pragma once

// Behavioral checks run with the real editor/processor. These deliberately cross
// saving, replacement and layout boundaries that image comparisons cannot verify.
struct AudiocityWorkspaceTestAccess
{
    static void prepareSnapshot(AudiocityAudioProcessorEditor& editor, const juce::File& sample, int scope, bool docked)
    {
        editor.browserDocked_ = docked;
        if (scope > 1)
        {
            editor.sampleRootFolderPath_ = sample.getParentDirectory().getFullPathName();
            editor.allSampleEntries_.clear();
            AudiocityAudioProcessorEditor::SampleListEntry entry;
            entry.file = scope == 2 ? sample : sample.withFileExtension(".sfz");
            entry.fileName = scope == 2 ? "Transient phrase.wav" : "Acoustic keys.sfz";
            entry.fileNameLower = entry.fileName.toLowerCase(); entry.relativePath = entry.fileName;
            entry.relativePathLower = entry.fileNameLower; entry.isInstrument = scope == 3;
            entry.metadataLine = scope == 2 ? "48 kHz  |  Mono" : "SFZ instrument";
            editor.allSampleEntries_.push_back(std::move(entry));
            editor.rebuildVisibleSampleList();
            editor.sampleBrowserRootLabel_.setText("Example sound folder", juce::dontSendNotification);
        }
        editor.timerCallback();
        editor.resized();
    }

    static juce::Result run(AudiocityAudioProcessor& processor, const juce::File& output)
    {
        juce::MemoryBlock original;
        processor.getStateInformation(original);
        auto editor = std::make_unique<AudiocityAudioProcessorEditor>(processor);
        const auto fail = [](const juce::String& message) { return juce::Result::fail("Workspace: " + message); };
        for (const auto size : {juce::Point<int>(980, 860), juce::Point<int>(980, 720), juce::Point<int>(1240, 900)})
        {
            editor->setSize(size.x, size.y); editor->showWorkspace(0);
            for (auto* control : std::initializer_list<juce::Component*>{&editor->ampAttackDial_, &editor->ampReleaseDial_,
                &editor->filterCutoffDial_, &editor->filterResDial_, &editor->reverbMixDial_, &editor->rootNoteCombo_})
            {
                const auto bounds = editor->sampleControlsViewport_.getLocalArea(control, control->getLocalBounds());
                if (!control->isVisible() || !editor->sampleControlsViewport_.isVisible() || !editor->sampleControlsViewport_.getLocalBounds().contains(bounds))
                    return fail("basic sound control is clipped at " + juce::String(size.x) + "x" + juce::String(size.y));
            }
        }
        editor->browserOpen_ = true;
        editor->showWorkspace(4);
        editor->timerCallback();
        if (!editor->generatePage_.isVisible()) return fail("source preparation is lost on timer tick");
        if (editor->browserBackdrop_.isVisible()) return fail("source preparation kept the browser over its controls");
        editor->showWorkspace(0, true);
        if (!editor->modulationPanel_.isVisible()) return fail("modulation page is not reachable");
        editor->showWorkspace(0);
        editor->browserOpen_ = true; editor->resized();
        if (!editor->presetBrowserList_.isVisible() || !editor->browserBackdrop_.isVisible()) return fail("browser is not reachable");
        editor->browserOpen_ = false; editor->resized();

        const auto target = output.getChildFile("interaction-fixtures").getChildFile("Nested").getChildFile("Workspace test.acp");
        const auto before = editor->captureSettingsSnapshot();
        const auto beforeAttack = processor.getAmpEnvelope().attackSeconds;
        if (!editor->savePresetToFile(target) || !target.existsAsFile() || editor->currentSoundFile_ != target)
            return fail("Save As ignored the selected destination");
        const auto savedRow = editor->visiblePresetFiles_.indexOf(target);
        if (savedRow < 0) return fail("external Save As destination is absent from the browser");
        editor->presetBrowserList_.selectRow(savedRow);
        if (editor->getSelectedPresetFile() != target || !editor->sampleBrowserFavoriteButton_.isEnabled())
            return fail("preset selection does not target administration and favorite actions");
        editor->sampleBrowserFavoriteButton_.onClick();
        if (!processor.isLibraryFavorite(target.getFullPathName())) return fail("preset favorite action did not persist");
        auto amp = processor.getAmpEnvelope(); amp.attackSeconds = 0.731f; processor.setAmpEnvelope(amp);
        editor->updateSoundIdentity();
        if (!editor->soundEdited_) return fail("parameter edit has no edited indicator");
        const auto changed = editor->captureSettingsSnapshot();
        editor->recordPendingSettingsChange();
        editor->keyPressed(juce::KeyPress('z', juce::ModifierKeys::ctrlModifier, 'z'));
        if (std::abs(processor.getAmpEnvelope().attackSeconds - beforeAttack) > 0.00001f) return fail("Undo does not restore amp shaping");
        editor->keyPressed(juce::KeyPress('y', juce::ModifierKeys::ctrlModifier, 'y'));
        if (std::abs(processor.getAmpEnvelope().attackSeconds - 0.731f) > 0.00001f) return fail("Redo does not restore amp shaping");
        editor->loadPresetFile(target);
        if (editor->captureSettingsSnapshot() != before || std::abs(processor.getAmpEnvelope().attackSeconds - beforeAttack) > 0.00001f)
            return fail("saved sound did not restore its settings");
        editor->restorePreviousSound();
        if (editor->captureSettingsSnapshot() != changed || std::abs(processor.getAmpEnvelope().attackSeconds - 0.731f) > 0.00001f)
            return fail("previous sound recovery lost edits");

        const auto invalid = target.getSiblingFile("Broken.acp");
        invalid.replaceWithText("<not-an-audiocity-preset/>");
        if (editor->replaceSoundFromPreset(invalid).wasOk() || std::abs(processor.getAmpEnvelope().attackSeconds - 0.731f) > 0.00001f
            || processor.getLoadedSampleLength() == 0) return fail("failed preset replacement damaged the prior sound");
        const auto missing = target.getSiblingFile("Missing source.acp");
        missing.replaceWithText("<AudiocityPatch samplePath=\"Z:/Audiocity-missing-fixture.wav\" generatedWaveformAssetV1=\"\" generatedWaveformData=\"\" capturedSampleAssetV1=\"\" capturedSampleData=\"\" embeddedSampleAssetV1=\"\" embeddedSampleData=\"\"/>");
        if (editor->replaceSoundFromPreset(missing).wasOk() || std::abs(processor.getAmpEnvelope().attackSeconds - 0.731f) > 0.00001f
            || processor.getLoadedSampleLength() == 0) return fail("missing source did not recover the prior playable sound");

        editor->showWorkspace(4);
        editor->showWorkspace(0);
        if (std::abs(processor.getAmpEnvelope().attackSeconds - 0.731f) > 0.00001f) return fail("canceling source preparation changed the sound");
        const auto previousLength = processor.getLoadedSampleLength();
        editor->showWorkspace(4);
        editor->generateLoadAsSampleButton_.onClick();
        if (!processor.isGeneratedWaveformLoaded() || editor->currentTabIndex_ != 0) return fail("generated source did not commit into Sound");
        editor->restorePreviousSound();
        if (processor.getLoadedSampleLength() != previousLength || std::abs(processor.getAmpEnvelope().attackSeconds - 0.731f) > 0.00001f)
            return fail("generated source recovery lost the previous sound");

        const auto previousRoot = processor.getRootMidiNote();
        editor->showWorkspace(5);
        processor.startInputCapture();
        juce::AudioBuffer<float> input(2, 512);
        juce::MidiBuffer midi;
        for (int block = 0; block < 8; ++block)
        {
            for (int channel = 0; channel < 2; ++channel)
                for (int frame = 0; frame < 512; ++frame) input.setSample(channel, frame, std::sin(static_cast<float>(frame) * 0.07f) * 0.2f);
            processor.processBlock(input, midi);
        }
        processor.stopInputCapture();
        editor->showWorkspace(0);
        if (processor.getLoadedSampleLength() != previousLength || processor.getRootMidiNote() != previousRoot)
            return fail("canceling recording replaced the current sound");
        editor->showWorkspace(5);
        editor->captureRootNoteCombo_.setSelectedId(49, juce::dontSendNotification);
        editor->captureLoadAsSampleButton_.onClick();
        if (!processor.isCapturedAudioLoaded() || processor.getRootMidiNote() != 48) return fail("recorded source commit failed");
        editor->restorePreviousSound();
        if (processor.getLoadedSampleLength() != previousLength || processor.getRootMidiNote() != previousRoot)
            return fail("recorded source recovery lost the previous sound");

        const auto audioFile = output.getChildFile("interaction-fixtures").getChildFile("Slice source.wav");
        {
            juce::WavAudioFormat format;
            auto stream = audioFile.createOutputStream();
            if (!stream) return fail("could not create slice fixture");
            stream->setPosition(0); stream->truncate();
            std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.release(), 48000, 1, 16, {}, 0));
            juce::AudioBuffer<float> phrase(1, 12000); phrase.clear();
            for (int hit = 0; hit < 3; ++hit)
                for (int n = 0; n < 480; ++n) phrase.setSample(0, hit*4000+n, 0.9f*std::exp(-static_cast<float>(n)/70.0f));
            if (!writer || !writer->writeFromAudioSampleBuffer(phrase, 0, 12000)) return fail("could not write slice fixture");
        }
        if (!processor.loadSampleFromFile(audioFile)) return fail("could not load slice fixture");
        prepareSnapshot(*editor, audioFile, 2, false);
        editor->browserScopeCombo_.setSelectedId(2, juce::sendNotificationSync);
        const auto sampleLengthBeforePreview = processor.getLoadedSampleLength();
        editor->previewSampleFromBrowserRow(0, true);
        if (!processor.isSamplePreviewPlaying() || processor.getLoadedSampleLength() != sampleLengthBeforePreview)
            return fail("sample preview replaced the loaded instrument");
        editor->browserCloseButton_.onClick();
        if (processor.isSamplePreviewPlaying()) return fail("closing the browser left preview playing");
        editor->refreshUI(true); editor->autoSliceLoadedSample();
        if (!processor.hasImportedProgram() || processor.getImportedProgramSliceMarkerSamples().size() < 2) return fail("explicit slicing failed");
        editor->restorePreviousSound();
        if (processor.hasImportedProgram() || processor.getLoadedSampleLength() != 12000) return fail("slice conversion was not reversible");
        editor->rememberSoundBeforeReplacement();
        if (!audioFile.deleteFile()) return fail("could not move away recovery source fixture");
        editor->generateLoadAsSampleButton_.onClick();
        editor->restorePreviousSound();
        if (processor.getLoadedSampleLength() != 12000) return fail("single-sample recovery depended on the original file");

        editor->setSize(1500, 860); editor->browserDocked_ = true; editor->browserOpen_ = true; editor->resized();
        if (editor->workspaceBrowserBounds_.intersects(editor->sampleControlsViewport_.getBounds())) return fail("docked browser covers musical controls");
        editor->setSize(980, 720);
        if (!editor->workspaceBrowserBounds_.intersects(editor->sampleControlsViewport_.getBounds())) return fail("constrained browser did not become an overlay");
        editor->browserOpen_ = false; editor->resized();

        editor->auditionModeCombo_.setSelectedId(4, juce::sendNotificationSync);
        if (editor->playerKeyboardViewport_.isVisible() || editor->playerPadButtons_[0].isVisible()) return fail("Hidden audition still exposes keys/pads");
        if (!editor->panicButton_.isVisible()) return fail("All Notes Off disappears with audition surface");
        juce::MemoryBlock state;
        processor.getStateInformation(state);
        processor.setAuditionViewMode(1);
        processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        if (processor.getAuditionViewMode() != 4) return fail("audition preference is not restored");
        if (editor->replaceSoundFromPreset(target).failed() || processor.getAuditionViewMode() != 4) return fail("preset replaced the audition preference");
        editor->updateSoundIdentity();
        editor.reset();
        editor = std::make_unique<AudiocityAudioProcessorEditor>(processor);
        if (editor->currentSoundFile_ != target || editor->currentPresetName_ != "Workspace test") return fail("closing the editor lost the sound name or save destination");
        editor.reset();
        processor.setStateInformation(original.getData(), static_cast<int>(original.getSize()));
        return juce::Result::ok();
    }
};
