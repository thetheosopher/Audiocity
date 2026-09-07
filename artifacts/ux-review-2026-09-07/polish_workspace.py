from pathlib import Path
import re
r=Path(r'C:\Projects\other\Audiocity')
p=r/'src/plugin/PluginEditor.cpp';s=p.read_text(encoding='utf-8')
# Native keyboard handles octave scrolling when it fits its viewport.
s=s.replace('playerKeyboard_.setSize(static_cast<int>(std::ceil(whiteKeyWidth * static_cast<float>(whiteKeyCount))),\n                            keyboardHeight);','playerKeyboard_.setSize(viewportBounds.getWidth(), keyboardHeight);')
s=s.replace('    const auto whiteKeyCount = countWhiteKeysInRange(kPlayerKeyboardMinMidiNote, kPlayerKeyboardMaxMidiNote);\n    constexpr float kWhiteKeyLengthRatio', '    constexpr float kWhiteKeyLengthRatio')
# Concise waveform summary; full gestures remain in the tooltip.
a=s.index('    juce::String waveformSummaryText;',s.index('void AudiocityAudioProcessorEditor::updateSampleInformationDisplay()'))
b=s.index('    waveformInteractionSummaryLabel_.setTooltip',a)
s=s[:a]+'''    const auto waveformSummaryText = sliceCount > 0
        ? juce::String(sliceCount) + " slices  |  Double-click to split; right-click a boundary to merge"
        : hasImportedProgram ? juce::String("Reference sample - edit zone roots, regions and loops in Advanced mapping")
        : sampleLength > 0 ? "Length " + formatDurationFromSamples(sampleLength, sampleRate) + "  |  Drag handles to trim or loop; wheel to zoom"
        : juce::String("Drop a sample here or Browse factory sounds");
    waveformInteractionSummaryLabel_.setText(waveformSummaryText, juce::dontSendNotification);
    waveformInteractionSummaryLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
''' +s[b:]
# Ctrl+Shift+Z is redo, not undo; editing text remains handled by TextEditor.
s=s.replace("if (commandDown && (key.getTextCharacter() == 'z' || key.getTextCharacter() == 'Z'))", "if (commandDown && !modifiers.isShiftDown() && (key.getTextCharacter() == 'z' || key.getTextCharacter() == 'Z'))")
s=s.replace("if (commandDown && (key.getTextCharacter() == 'y' || key.getTextCharacter() == 'Y'))", "if (commandDown && ((key.getTextCharacter() == 'y' || key.getTextCharacter() == 'Y')\n        || (modifiers.isShiftDown() && (key.getTextCharacter() == 'z' || key.getTextCharacter() == 'Z'))))")
# Discover external presets saved by this instance or previously marked recent/favorite.
needle='        juce::StringArray seenNames;'
insert='''        auto externalPaths = processor_.getLibraryMetadataSnapshot().getRecentPaths();
        externalPaths.addArray(processor_.getLibraryMetadataSnapshot().getFavoritePaths());
        for (const auto& externalPath : externalPaths)
        {
            const juce::File externalFile(externalPath);
            if (externalFile.hasFileExtension("acp") && externalFile.existsAsFile()
                && !presetFiles.contains(externalFile) && !userFiles.contains(externalFile))
                userFiles.add(externalFile);
        }

'''
s=s.replace(needle,insert+needle,1)
# Route legacy snapshot entry points to the new browser state.
s=s.replace('    sampleBrowserRailEnabled_ = browserRailEnabled;','    browserOpen_ = browserRailEnabled;\n    sampleBrowserRailEnabled_ = browserRailEnabled;',1)
# mark mapping edits independently of parameter polling.
needle='    editorUndoHistory_.recordMappingChange(beforeState, afterState, label.toStdString());'
s=s.replace(needle,needle+'\n    soundEdited_ = true;',1)
p.write_text(s,encoding='utf-8')
# Move the advanced mapper into its own bounded scroll surface.
names=re.findall(r'    (mapping\w+_)\.setVisible\(showMappingTab\);',s)
p=r/'src/plugin/PluginInstrumentWorkspace.cpp';s=p.read_text()
needle='    addChildComponent(browserBackdrop_);'
insert='''    addChildComponent(mappingControlsViewport_);
    mappingControlsViewport_.setViewedComponent(&mappingControlsContent_, false);
    mappingControlsViewport_.setScrollBarsShown(true, false);
'''+''.join('    mappingControlsContent_.addAndMakeVisible('+n+');\n' for n in names)
s=s.replace(needle,insert+needle,1)
s=s.replace('    browserBackdrop_.setVisible(false);','    browserBackdrop_.setVisible(false);\n    mappingControlsViewport_.setVisible(false);',1)
s=s.replace('    if (currentTabIndex_ == 2) mappingPage_.layout(area);','''    if (currentTabIndex_ == 2)
    {
        place(mappingControlsViewport_, area);
        mappingControlsContent_.setSize(area.getWidth()-14, juce::jmax(620, area.getHeight()));
        mappingPage_.layout(mappingControlsContent_.getLocalBounds());
    }''')
p.write_text(s,encoding='utf-8')
# Replace the authoring-heavy mapper header with export, remove-zone and identity.
p=r/'src/plugin/PluginMappingPage.cpp';s=p.read_text()
a=s.index('    auto header = mappingArea.removeFromTop(60);')
b=s.index('    mappingArea.removeFromTop(4);',a)
s=s[:a]+'''    auto header = mappingArea.removeFromTop(32);
    saveLibraryButton_.setBounds(header.removeFromLeft(150));
    header.removeFromLeft(8);
    deleteZoneButton_.setBounds(header.removeFromRight(100));
    header.removeFromRight(8);
    summaryLabel_.setBounds(header);
    for (auto* button : { &newLibraryButton_, &addSampleButton_, &createZoneButton_, &duplicateZoneButton_, &splitZoneButton_, &refreshButton_ })
        button->setBounds({});

'''+s[b:]
p.write_text(s,encoding='utf-8')
