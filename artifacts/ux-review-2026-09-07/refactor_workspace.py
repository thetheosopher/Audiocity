from pathlib import Path
root = Path(r'C:\Projects\other\Audiocity')
p = root/'src/plugin/PluginEditor.cpp'
s = p.read_text(encoding='utf-8-sig')
def replace_function(signature, replacement):
    global s
    start = s.index(signature)
    # Top-level function delimiters in this file are at column zero.
    end = s.index('\n}\n', start) + 3
    s = s[:start] + replacement + '\n' + s[end:]
def once(old, new):
    global s
    assert old in s, old[:100]
    s = s.replace(old,new,1)
replace_function('void AudiocityAudioProcessorEditor::resized()', '')
replace_function('void AudiocityAudioProcessorEditor::paint(juce::Graphics& g)', '')
once('    setupTooltips();\n    refreshSampleBrowserBookmarks();', '    setupTooltips();\n    initialiseInstrumentWorkspace();\n    refreshSampleBrowserBookmarks();')
once('    startTimerHz(60);', '    resized();\n    startTimerHz(60);')
once('    syncAutomatedControlsFromProcessor();\n}', '    syncAutomatedControlsFromProcessor();\n    updateSoundIdentity();\n    const auto voiceCount = processor_.getActiveVoiceCount();\n    workspaceStatusLabel_.setText((currentTabIndex_ == 4 || currentTabIndex_ == 5)\n        ? "Preparing source - Preview auditions the take; Use sample commits it"\n        : juce::String(processor_.isSamplePreviewPlaying() ? "Sample preview" : voiceCount > 0 ? "MIDI playing" : "Ready")\n            + "  |  " + juce::String(voiceCount) + " voices", juce::dontSendNotification);\n}')
replace_function('bool AudiocityAudioProcessorEditor::shouldShowPersistentBrowserRail() const noexcept', 'bool AudiocityAudioProcessorEditor::shouldShowPersistentBrowserRail() const noexcept\n{\n    return browserOpen_;\n}')
replace_function('bool AudiocityAudioProcessorEditor::shouldShowSampleInspectorRail() const noexcept', 'bool AudiocityAudioProcessorEditor::shouldShowSampleInspectorRail() const noexcept\n{\n    return false;\n}')
replace_function('void PlayerModulationPanel::paint(juce::Graphics& g)', '''void PlayerModulationPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff18232c));
    g.setColour(juce::Colour(0xffe2ecf0)); g.setFont(14.0f);
    g.drawText("Expression routing - source to destination", 12, 2, getWidth()-24, 24, juce::Justification::centredLeft);
    const juce::StringArray sources { "Mod wheel", "Pressure", "Velocity", "Macro 1", "Macro 2" };
    for (int i=0; i<5; ++i)
    {
        g.setColour(juce::Colour(0xffadbdc7));
        g.drawText(sources[i] + "  ->", 12, 32+i*76, 135, 68, juce::Justification::centredLeft);
    }
}''')
replace_function('void PlayerModulationPanel::resized()', '''void PlayerModulationPanel::resized()
{
    int y = 32;
    for (const auto& route : routeDialSets())
    {
        auto row = juce::Rectangle<int>(154, y, juce::jmax(1,getWidth()-166), 68);
        const int w = (row.getWidth()-24)/3;
        for (auto* dial : {route.pitchDial, route.filterDial, route.ampDial})
        { dial->setBounds(row.removeFromLeft(w)); row.removeFromLeft(12); }
        y += 76;
    }
}''')
once('    forEachDial([this](CcLearnDial& dial, const juce::String&)', '    forEachDial([this](CcLearnDial& dial, const juce::String&)') if False else None
once('        dial.onValueChange = [this] { pushToProcessor(); };', '        dial.onValueChange = [this] { pushToProcessor(); };\n        dial.useHorizontalControl();')
once('void PlayerModulationPanel::syncFromProcessor()\n{', 'void PlayerModulationPanel::syncFromProcessor()\n{')
# Label routes by destination because each row already identifies its source.
once('    forEachDial([this](CcLearnDial& dial, const juce::String&)\n', '    forEachDial([this](CcLearnDial& dial, const juce::String&)\n')
once('    });\n}\n\nstd::array<PlayerModulationPanel::RouteDialSet', '''    });
    for (const auto& route : routeDialSets())
    {
        route.pitchDial->setDisplayName("Pitch");
        route.filterDial->setDisplayName("Filter cutoff");
        route.ampDial->setDisplayName("Amplitude");
    }
}

std::array<PlayerModulationPanel::RouteDialSet''')
once('        if (matches && matchesLibraryFilters && matchesTagFilter)', '        const bool matchesScope = browserScopeCombo_.getSelectedId() < 2\n            || (browserScopeCombo_.getSelectedId() == 2 ? !item.isInstrument : item.isInstrument);\n        if (matches && matchesLibraryFilters && matchesTagFilter && matchesScope)')
once('        visiblePresetNames.add(label);', '''        if (!useSnapshotOverride && index < availablePresetFiles_.size())
        {
            const auto path = availablePresetFiles_[index].getFullPathName();
            if (sampleBrowserFavoritesOnlyToggle_.getToggleState() && !processor_.isLibraryFavorite(path)) continue;
            if (sampleBrowserRecentOnlyToggle_.getToggleState() && processor_.getLibraryMetadataSnapshot().recentRank(path) < 0) continue;
        }
        visiblePresetNames.add(label);''')
once('    suppressPresetComboChange_ = false;\n    updatePresetActionButtons();', '    suppressPresetComboChange_ = false;\n    updatePresetActionButtons();\n    presetBrowserList_.updateContent();\n    presetBrowserList_.repaint();')
# Respect the chosen destination, not just its basename.
once('        const auto name = file.getFileName().upToLastOccurrenceOf(kPresetFileExtension, false, false);\n        if (name.isNotEmpty())\n            savePreset(name);', '        savePresetToFile(file);')
replace_function('void AudiocityAudioProcessorEditor::showPresetLoadErrorAndOfferDelete(', '''void AudiocityAudioProcessorEditor::showPresetLoadErrorAndOfferDelete(const juce::File& file, const juce::String& error)
{
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Unable to load sound",
        "Could not load " + file.getFileName() + ".\\n\\n" + error + "\\n\\nYour previous sound has been retained. Check the source files and try again.");
}''')
replace_function('void AudiocityAudioProcessorEditor::loadPresetFromSelection()', '''void AudiocityAudioProcessorEditor::loadPresetFromSelection()
{
    loadPresetFile(getSelectedPresetFile());
}''')
once('void AudiocityAudioProcessorEditor::clearSelectedPresetAfterSourceLoad()\n{', 'void AudiocityAudioProcessorEditor::clearSelectedPresetAfterSourceLoad()\n{\n    currentSoundFile_ = {};\n    soundEdited_ = true;')
once('        processor_.loadGeneratedWaveformAsSample(generatedWaveform_, selectedMidiNote);', '        rememberSoundBeforeReplacement();\n        processor_.loadGeneratedWaveformAsSample(generatedWaveform_, selectedMidiNote);')
once('        processor_.setRootMidiNote(selectedRoot);\n        if (!processor_.loadCapturedAudioAsSample(start, end))\n            return;', '        rememberSoundBeforeReplacement();\n        if (!processor_.loadCapturedAudioAsSample(start, end))\n            return;\n        processor_.setRootMidiNote(selectedRoot);')
once('    if (!processor_.importTransientSliceProgram(file))', '    rememberSoundBeforeReplacement();\n    if (!processor_.importTransientSliceProgram(file))')
# Retain prior state at the central import entry, before either async or sync commit.
once('bool AudiocityAudioProcessorEditor::loadFileAsInstrument(const juce::File& file,', 'bool AudiocityAudioProcessorEditor::loadFileAsInstrument(const juce::File& file,')
sig = s.index('bool AudiocityAudioProcessorEditor::loadFileAsInstrument(const juce::File& file,')
pos = s.index('{', sig)+1
s=s[:pos]+'\n    rememberSoundBeforeReplacement();'+s[pos:]
once('    if (commandDown && (key.getTextCharacter() == \'s\' || key.getTextCharacter() == \'S\'))\n    {\n        saveStateToFile();', '    if (commandDown && (key.getTextCharacter() == \'s\' || key.getTextCharacter() == \'S\'))\n    {\n        presetSaveButton_.onClick();')
# Text entry owns its shortcuts, rather than triggering note or page shortcuts.
once('    const bool mappingShortcutContext = currentTabIndex_ == 2', '    if (dynamic_cast<const juce::TextEditor*>(focusedComponent) != nullptr) return false;\n    if (key.getKeyCode() == juce::KeyPress::escapeKey)\n    { showWorkspace(0); return true; }\n    const bool mappingShortcutContext = currentTabIndex_ == 2')
start = s.index('    if (!commandDown && !modifiers.isAltDown())\n    {\n        const auto character = key.getTextCharacter();')
end = s.index('    if (!commandDown && !modifiers.isAltDown()\n', start+5)
s = s[:start]+s[end:]
# Larger keys with native octave-scroll controls instead of 128 squeezed notes.
start=s.index('    const auto whiteKeyWidth = juce::jmax(6.0f,',s.index('void AudiocityAudioProcessorEditor::updatePlayerKeyboardSizing()'))
end=s.index('\n\n', start)
s=s[:start]+'    const auto whiteKeyWidth = 24.0f;'+s[end:]
p.write_text(s,encoding='utf-8')
