#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ImportFormatRegistry.h"

namespace
{
constexpr auto panel = 0xff18232c;
constexpr auto background = 0xff10171e;
constexpr auto border = 0xff30414c;
constexpr auto text = 0xffe2ecf0;
constexpr auto muted = 0xffadbdc7;
}

void AudiocityAudioProcessorEditor::initialiseInstrumentWorkspace()
{
    addChildComponent(mappingControlsViewport_);
    mappingControlsViewport_.setViewedComponent(&mappingControlsContent_, false);
    mappingControlsViewport_.setScrollBarsShown(true, false);
    mappingControlsContent_.addAndMakeVisible(mappingSummaryLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingRefreshButton_);
    mappingControlsContent_.addAndMakeVisible(mappingNewLibraryButton_);
    mappingControlsContent_.addAndMakeVisible(mappingAddSampleButton_);
    mappingControlsContent_.addAndMakeVisible(mappingSaveLibraryButton_);
    mappingControlsContent_.addAndMakeVisible(mappingCreateZoneButton_);
    mappingControlsContent_.addAndMakeVisible(mappingDuplicateZoneButton_);
    mappingControlsContent_.addAndMakeVisible(mappingSplitZoneButton_);
    mappingControlsContent_.addAndMakeVisible(mappingDeleteZoneButton_);
    mappingControlsContent_.addAndMakeVisible(mappingOverview_);
    mappingControlsContent_.addAndMakeVisible(mappingZoneListBox_);
    mappingControlsContent_.addAndMakeVisible(mappingDetailsText_);
    mappingControlsContent_.addAndMakeVisible(mappingEditKeyLowLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingEditKeyLowSlider_);
    mappingControlsContent_.addAndMakeVisible(mappingEditKeyHighLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingEditKeyHighSlider_);
    mappingControlsContent_.addAndMakeVisible(mappingEditVelocityLowLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingEditVelocityLowSlider_);
    mappingControlsContent_.addAndMakeVisible(mappingEditVelocityHighLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingEditVelocityHighSlider_);
    mappingControlsContent_.addAndMakeVisible(mappingEditVelocityFadeInLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingEditVelocityFadeInLowSlider_);
    mappingControlsContent_.addAndMakeVisible(mappingEditVelocityFadeInHighSlider_);
    mappingControlsContent_.addAndMakeVisible(mappingEditVelocityFadeOutLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingEditVelocityFadeOutLowSlider_);
    mappingControlsContent_.addAndMakeVisible(mappingEditVelocityFadeOutHighSlider_);
    mappingControlsContent_.addAndMakeVisible(mappingEditRootLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingEditRootSlider_);
    mappingControlsContent_.addAndMakeVisible(mappingEditSampleStartLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingEditSampleStartSlider_);
    mappingControlsContent_.addAndMakeVisible(mappingEditSampleEndLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingEditSampleEndSlider_);
    mappingControlsContent_.addAndMakeVisible(mappingEditLoopStartLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingEditLoopStartSlider_);
    mappingControlsContent_.addAndMakeVisible(mappingEditLoopEndLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingEditLoopEndSlider_);
    mappingControlsContent_.addAndMakeVisible(mappingEditGainLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingEditGainSlider_);
    mappingControlsContent_.addAndMakeVisible(mappingEditPanLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingEditPanSlider_);
    mappingControlsContent_.addAndMakeVisible(mappingEditRoundRobinGroupLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingEditRoundRobinGroupSlider_);
    mappingControlsContent_.addAndMakeVisible(mappingEditRoundRobinPositionLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingEditRoundRobinPositionSlider_);
    mappingControlsContent_.addAndMakeVisible(mappingEditRoundRobinModeLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingEditRoundRobinModeCombo_);
    mappingControlsContent_.addAndMakeVisible(mappingEditChokeLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingEditChokeSlider_);
    mappingControlsContent_.addAndMakeVisible(mappingEditTriggerLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingEditTriggerCombo_);
    mappingControlsContent_.addAndMakeVisible(mappingEditLoopLabel_);
    mappingControlsContent_.addAndMakeVisible(mappingEditLoopCombo_);
    mappingControlsContent_.addAndMakeVisible(mappingEditApplyButton_);
    mappingControlsContent_.addAndMakeVisible(mappingEditStatusLabel_);
    addChildComponent(browserBackdrop_);
    browserBackdrop_.onPaint = [](juce::Graphics& g)
    { g.fillAll(juce::Colour(panel)); };
    for (auto* c : std::initializer_list<juce::Component*>{ &soundViewButton_, &modulationViewButton_,
        &sourceMenuButton_, &workspaceMenuButton_, &closeToolButton_, &panicButton_, &detailsButton_,
        &sliceButton_, &auditionModeCombo_, &browserScopeCombo_, &browserLoadButton_, &browserStopButton_,
        &browserCloseButton_, &browserDockButton_, &browserHelpLabel_, &browserAutoPreview_, &browserPreviewLevel_,
        &soundIdentityLabel_, &sourceIdentityLabel_, &workspaceStatusLabel_, &presetBrowserList_ })
        addAndMakeVisible(c);
    for (auto* label : { &soundIdentityLabel_, &sourceIdentityLabel_, &workspaceStatusLabel_, &browserHelpLabel_ })
    {
        label->setColour(juce::Label::textColourId, juce::Colour(text));
        label->setFont(juce::Font(juce::FontOptions(14.0f)));
    }
    soundIdentityLabel_.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
    workspaceStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(muted));
    masterVolumeDial_.useHorizontalControl();
    addAndMakeVisible(masterVolumeDial_);
    addAndMakeVisible(outputLevelMeter_);
    for (int i = 0; i < 2; ++i)
    {
        auto& macro = modulationPanel_.macroControl(i);
        macro.useHorizontalControl();
        addAndMakeVisible(macro);
    }
    soundViewButton_.onClick = [this] { showWorkspace(0); };
    modulationViewButton_.onClick = [this] { showWorkspace(0, true); };
    detailsButton_.onClick = [this] { showWorkspace(0, false, !soundDetailsView_); };
    sampleControlsContent_.addChildComponent(editZonesButton_);
    sampleControlsContent_.addChildComponent(zoneScopeLabel_);
    zoneScopeLabel_.setFont(juce::Font(juce::FontOptions(13.0f)));
    editZonesButton_.onClick = [this] { showWorkspace(2); };
    for (auto* dial : { &reverbMixDial_, &delayMixDial_, &saturationDriveDial_ }) dial->useHorizontalControl();
    closeToolButton_.onClick = [this] { showWorkspace(0); };
    workspaceMenuButton_.onClick = [this] { showWorkspaceMenu(); };
    panicButton_.onClick = [this] { processor_.panicAllAudio(); playerKeyboardState_.allNotesOff(0); };
    auditionModeCombo_.addItemList({ "Auto", "Keyboard", "Pads", "Hidden" }, 1);
    auditionModeCombo_.setSelectedId(processor_.getAuditionViewMode(), juce::dontSendNotification);
    auditionModeCombo_.setTooltip("Audition surface. Auto chooses pads for sliced sources and a keyboard for pitched sounds.");
    auditionModeCombo_.onChange = [this] { processor_.setAuditionViewMode(auditionModeCombo_.getSelectedId()); resized(); };
    sampleBrowserRailToggleButton_.setButtonText("Browse");
    sampleBrowserRailToggleButton_.onClick = [this] { browserOpen_ = !browserOpen_; if (!browserOpen_) processor_.stopAuditionPreview(); resized(); };
    browserCloseButton_.onClick = [this] { browserOpen_ = false; processor_.stopAuditionPreview(); resized(); };
    browserDockButton_.onClick = [this] { browserDocked_ = !browserDocked_; resized(); };
    browserDockButton_.setTooltip("Keep the browser beside Sound at widths of 1400 pixels or more. Smaller windows use an overlay.");
    browserAutoPreview_.setToggleState(true, juce::dontSendNotification);
    browserAutoPreview_.onClick = [this] { if (!browserAutoPreview_.getToggleState()) processor_.stopAuditionPreview(); resized(); };
    browserPreviewLevel_.setSliderStyle(juce::Slider::LinearHorizontal);
    browserPreviewLevel_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 22);
    browserPreviewLevel_.setRange(0, 100, 1); browserPreviewLevel_.setTextValueSuffix(" %");
    browserPreviewLevel_.setName("Preview level"); browserPreviewLevel_.setTooltip("Preview level. Master output still applies.");
    browserPreviewLevel_.setValue(processor_.getAuditionPreviewGain() * 100, juce::dontSendNotification);
    browserPreviewLevel_.onValueChange = [this] { processor_.setAuditionPreviewGain(static_cast<float>(browserPreviewLevel_.getValue() / 100)); };
    browserScopeCombo_.addItemList({ "Presets", "Samples", "Instruments" }, 1);
    browserScopeCombo_.setSelectedId(1, juce::dontSendNotification);
    browserScopeCombo_.onChange = [this] { rebuildVisibleSampleList(); refreshPresetList(currentPresetName_); resized(); };
    sampleBrowserChooseRootButton_.setButtonText("Add folder");
    sampleBrowserFavoriteButton_.setButtonText("Favorite");
    auto favoriteAction = sampleBrowserFavoriteButton_.onClick;
    sampleBrowserFavoriteButton_.onClick = [this, favoriteAction]
    {
        if (browserScopeCombo_.getSelectedId() != 1) { favoriteAction(); return; }
        const int row = presetBrowserList_.getSelectedRow();
        if (juce::isPositiveAndBelow(row, visiblePresetFiles_.size()))
        {
            const auto file = visiblePresetFiles_[row];
            processor_.setLibraryFavorite(file.getFullPathName(), !processor_.isLibraryFavorite(file.getFullPathName()));
            refreshPresetList(currentPresetName_);
        }
    };
    sampleBrowserFavoritesOnlyToggle_.onClick = [this] { rebuildVisibleSampleList(); refreshPresetList(currentPresetName_); };
    sampleBrowserRecentOnlyToggle_.onClick = [this] { rebuildVisibleSampleList(); refreshPresetList(currentPresetName_); };
    presetBrowserModel_.count = [this] { return presetCombo_.getNumItems(); };
    presetBrowserModel_.selectionChanged = [this]
    {
        presetCombo_.setSelectedItemIndex(presetBrowserList_.getSelectedRow(), juce::dontSendNotification);
        updatePresetActionButtons(); updateBrowserLibraryControls();
    };
    presetBrowserModel_.paintRow = [this](int row, juce::Graphics& g, int w, int h, bool selected)
    {
        if (selected) g.fillAll(juce::Colour(0xff294854));
        g.setColour(juce::Colour(text)); g.setFont(14.0f);
        g.drawFittedText(presetCombo_.getItemText(row), 10, 4, w - 20, 23, juce::Justification::centredLeft, 1);
        g.setColour(juce::Colour(muted)); g.setFont(12.0f);
        const bool valid = juce::isPositiveAndBelow(row, visiblePresetFiles_.size());
        auto description = valid && isFactoryPresetFile(visiblePresetFiles_[row]) ? juce::String("Factory sound") : juce::String("My sound");
        if (valid && processor_.isLibraryFavorite(visiblePresetFiles_[row].getFullPathName())) description += "  |  Favorite";
        g.drawText(description, 10, 28, w - 20, h - 30, juce::Justification::centredLeft);
    };
    presetBrowserModel_.load = [this](int row)
    {
        if (juce::isPositiveAndBelow(row, visiblePresetFiles_.size())) loadPresetFile(visiblePresetFiles_[row]);
    };
    presetBrowserList_.setRowHeight(56);
    browserLoadButton_.onClick = [this]
    {
        if (browserScopeCombo_.getSelectedId() == 1) presetBrowserModel_.load(presetBrowserList_.getSelectedRow());
        else loadSampleFromBrowserRow(sampleBrowserListBox_.getSelectedRow());
    };
    browserStopButton_.onClick = [this]
    {
        if (processor_.isSamplePreviewPlaying()) processor_.stopAuditionPreview();
        else previewSampleFromBrowserRow(sampleBrowserListBox_.getSelectedRow(), true);
    };
    sliceButton_.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Convert source to slices (keeps previous sound)",
            !processor_.hasImportedProgram() && processor_.getLoadedSamplePath().isNotEmpty());
        if (processor_.hasImportedProgram()) menu.addItem(2, "Edit mapped zones in Advanced mapping");
        else if (processor_.getLoadedSamplePath().isEmpty()) menu.addSectionHeader("Slicing requires an imported audio file");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&sliceButton_),
            [safe = juce::Component::SafePointer<AudiocityAudioProcessorEditor>(this)](int id)
            { if (safe && id == 1) safe->autoSliceLoadedSample(); if (safe && id == 2) safe->showWorkspace(2); });
    };
    sourceMenuButton_.onClick = [this]
    {
        if (backgroundImportInProgress_) { cancelBackgroundInstrumentLoad(); return; }
        juce::PopupMenu menu;
        menu.addItem(1, "Replace sample / import instrument...");
        menu.addItem(2, "Record sample...");
        menu.addItem(3, "Generate waveform...");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&sourceMenuButton_),
            [safe = juce::Component::SafePointer<AudiocityAudioProcessorEditor>(this)](int id)
            {
                if (!safe) return;
                if (id == 1) safe->openSampleChooser();
                if (id == 2) safe->showWorkspace(5);
                if (id == 3) safe->showWorkspace(4);
            });
    };
    generateLoadAsSampleButton_.setButtonText("Use sample");
    captureLoadAsSampleButton_.setButtonText("Use sample");
    capturePlayButton_.setButtonText("Preview");
    generateFrequencyLabel_.setText("Root note", juce::dontSendNotification);
    generatePulseWidthLabel_.setText("Pulse width", juce::dontSendNotification);
    presetSaveButton_.onClick = [this]
    {
        if (currentSoundFile_ != juce::File{} && !isFactoryPresetFile(currentSoundFile_)) savePresetToFile(currentSoundFile_);
        else promptSavePreset();
    };
    pitchLfoRateDial_.setDisplayName("Vibrato rate");
    pitchLfoDepthDial_.setDisplayName("Vibrato depth");
    filterResDial_.setDisplayName("Resonance");
    filterEnvAmtDial_.setDisplayName("Envelope amt");
    ampLfoRateDial_.setDisplayName("Tremolo rate");
    ampLfoDepthDial_.setDisplayName("Tremolo depth");
    setResizable(true, true);
    setResizeLimits(980, 720, 1800, 1600);
    // Old sessions may have closed on a utility page; always reopen as an instrument.
    currentTabIndex_ = 0;
    processor_.setEditorTabIndex(0);
    workspaceReady_ = true;
    const auto identity = processor_.getWorkspaceSoundIdentity();
    if (identity.name.isNotEmpty()) currentPresetName_ = identity.name;
    if (identity.savePath.isNotEmpty()) currentSoundFile_ = juce::File(identity.savePath);
    markSoundSaved();
    if (identity.savedParameters.size() == savedParameterValues_.size()) savedParameterValues_ = identity.savedParameters;
    soundEdited_ = identity.edited;
    resized();
}

void AudiocityAudioProcessorEditor::showWorkspace(int page, bool modulation, bool details)
{
    processor_.stopAuditionPreview();
    if (page == 4 || page == 5) cancelBackgroundInstrumentLoad();
    if (currentTabIndex_ == 4 || currentTabIndex_ == 5)
    {
        processor_.stopAuditionPreview();
        processor_.stopGeneratedWaveformPreview();
        processor_.stopInputCapture();
    }
    currentTabIndex_ = page; modulationView_ = modulation; soundDetailsView_ = details;
    tabBar_.setCurrentTabIndex(page);
    browserOpen_ = false;
    processor_.setEditorTabIndex(page);
    sampleControlsViewport_.setViewPosition(0, 0);
    resized(); repaint();
}

void AudiocityAudioProcessorEditor::setWorkspaceSnapshotState(bool modulation, bool details, bool browser, int scope, int audition)
{
    modulationView_ = modulation; soundDetailsView_ = details; browserOpen_ = browser;
    browserScopeCombo_.setSelectedId(scope, juce::dontSendNotification);
    auditionModeCombo_.setSelectedId(audition, juce::dontSendNotification);
    resized();
}

void AudiocityAudioProcessorEditor::showWorkspaceMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Save preset as...");
    menu.addItem(2, "Restore previous sound", previousSoundState_.getSize() > 0);
    menu.addSeparator();
    menu.addItem(3, "Sample details and playback settings...");
    menu.addItem(4, "Advanced mapping / export...", processor_.hasImportedProgram());
    menu.addItem(5, "About Audiocity");
    menu.addSeparator();
    menu.addItem(6, "Rename selected user preset...", presetRenameButton_.isEnabled());
    menu.addItem(7, "Delete selected user preset...", presetDeleteButton_.isEnabled());
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&workspaceMenuButton_),
        [safe = juce::Component::SafePointer<AudiocityAudioProcessorEditor>(this)](int id)
        {
            if (!safe) return;
            switch (id)
            {
                case 1: safe->promptSavePreset(); break;
                case 2: safe->restorePreviousSound(); break;
                case 3: safe->showWorkspace(0, false, true); break;
                case 4: safe->showWorkspace(2); break;
                case 5: safe->showWorkspace(6); break;
                case 6: safe->renameSelectedPreset(); break;
                case 7: safe->deleteSelectedPreset(); break;
                default: break;
            }
        });
}

void AudiocityAudioProcessorEditor::updateSoundIdentity()
{
    if (!workspaceReady_) return;
    if (savedSettingsSnapshot_)
    {
        auto settings = captureSettingsSnapshot();
        // Preparing a recording changes acquisition preferences, not the saved instrument.
        settings.captureTargetSampleRate = savedSettingsSnapshot_->captureTargetSampleRate;
        settings.captureChannelMode = savedSettingsSnapshot_->captureChannelMode;
        settings.captureBitDepth = savedSettingsSnapshot_->captureBitDepth;
        settings.captureInputGain = savedSettingsSnapshot_->captureInputGain;
        if (settings != *savedSettingsSnapshot_) soundEdited_ = true;
    }
    const auto& parameters = processor_.getParameters();
    for (int i = 0; i < parameters.size() && i < static_cast<int>(savedParameterValues_.size()); ++i)
        if (parameters[i]->getValue() != savedParameterValues_[static_cast<std::size_t>(i)]) soundEdited_ = true;
    const auto source = processor_.hasImportedProgram() ? processor_.getImportedProgramName()
        : processor_.getLoadedSamplePath().isNotEmpty() ? juce::File(processor_.getLoadedSamplePath()).getFileName()
        : processor_.isGeneratedWaveformLoaded() ? juce::String("Generated waveform")
        : processor_.isCapturedAudioLoaded() ? juce::String("Recorded sample") : juce::String("Drop a sample or browse factory sounds");
    soundIdentityLabel_.setText((currentPresetName_.isNotEmpty() ? currentPresetName_ : juce::String("Untitled sound"))
        + (soundEdited_ ? "  *" : ""), juce::dontSendNotification);
    const bool multi = processor_.hasImportedProgram();
    sourceIdentityLabel_.setText(source + (multi ? "  |  Instrument - shaping affects all notes" : "  |  Single sample"), juce::dontSendNotification);
    soundIdentityLabel_.setTooltip(soundEdited_ ? "Edited sound. Save to keep these changes." : "Current sound");
    const auto routing = processor_.getModulationRoutingSettings();
    for (int i = 0; i < 2; ++i)
    {
        const auto& route = routing.macros[static_cast<std::size_t>(i)];
        const bool pitch = std::abs(route.toPitchCents) > 0.01f;
        const bool filter = std::abs(route.toFilterHz) > 0.01f;
        const bool amp = std::abs(route.toAmp) > 0.0001f;
        auto name = juce::String("Macro ") + juce::String(i + 1);
        if (filter && !pitch && !amp) name += " - Brightness";
        else if (pitch && !filter && !amp) name += " - Pitch";
        else if (amp && !pitch && !filter) name += " - Level";
        else if (!pitch && !filter && !amp) name += " - Unassigned";
        else name += " - Multiple destinations";
        modulationPanel_.macroControl(i).setDisplayName(name);
    }
    masterVolumeDial_.setValue(processor_.getMasterVolume() * 100.0f, juce::dontSendNotification);
    sourceMenuButton_.setButtonText(backgroundImportInProgress_ ? "Cancel load" : "Source...");
    browserStopButton_.setButtonText(processor_.isSamplePreviewPlaying() ? "Stop preview" : "Preview");
    processor_.setWorkspaceSoundIdentity({currentPresetName_, currentSoundFile_ == juce::File{} ? juce::String{} : currentSoundFile_.getFullPathName(), soundEdited_, savedParameterValues_});
}

void AudiocityAudioProcessorEditor::markSoundSaved()
{
    savedSettingsSnapshot_ = captureSettingsSnapshot();
    savedParameterValues_.clear();
    for (auto* parameter : processor_.getParameters()) savedParameterValues_.push_back(parameter->getValue());
    soundEdited_ = false;
}

void AudiocityAudioProcessorEditor::recordPendingSettingsChange()
{
    const auto current = captureSettingsSnapshot();
    if (lastSettingsSnapshot_ && current != *lastSettingsSnapshot_)
    {
        int changedParameter = -1;
        const auto& before = lastSettingsSnapshot_->parameterValues;
        for (std::size_t i = 0; i < before.size() && i < current.parameterValues.size(); ++i)
            if (before[i] != current.parameterValues[i]) changedParameter = changedParameter == -1 ? static_cast<int>(i) : -2;
        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastSettingsEditTime_ > 500 || changedParameter != lastChangedParameter_) ++settingsCoalesceKey_;
        editorUndoHistory_.recordSettingsChange(*lastSettingsSnapshot_, current, settingsCoalesceKey_, "Shape sound");
        lastSettingsEditTime_ = now; lastChangedParameter_ = changedParameter;
    }
    lastSettingsSnapshot_ = current;
}

void AudiocityAudioProcessorEditor::layoutInstrumentBrowser(juce::Rectangle<int> area)
{
    workspaceBrowserBounds_ = area;
    browserBackdrop_.setBounds(area);
    browserBackdrop_.setVisible(true);
    browserBackdrop_.toFront(false);
    area.reduce(12, 12);
    const auto show = [](juce::Component& c, juce::Rectangle<int> b) { c.setBounds(b); c.setVisible(true); c.toFront(false); };
    auto row = area.removeFromTop(30);
    show(browserCloseButton_, row.removeFromRight(60)); row.removeFromRight(6);
    show(browserDockButton_, row.removeFromRight(64)); row.removeFromRight(6);
    browserDockButton_.setToggleState(browserDocked_, juce::dontSendNotification);
    browserDockButton_.setEnabled(getWidth() >= 1400);
    show(browserScopeCombo_, row);
    area.removeFromTop(8);
    const bool presets = browserScopeCombo_.getSelectedId() == 1;
    if (presets) show(presetFilterEditor_, area.removeFromTop(30));
    else show(sampleBrowserFilterEditor_, area.removeFromTop(30));
    area.removeFromTop(8);
    row = area.removeFromTop(28);
    show(sampleBrowserFavoritesOnlyToggle_, row.removeFromLeft(95));
    show(sampleBrowserRecentOnlyToggle_, row.removeFromLeft(76));
    show(sampleBrowserFavoriteButton_, row);
    if (!presets)
    {
        area.removeFromTop(8); row = area.removeFromTop(28);
        show(sampleBrowserChooseRootButton_, row.removeFromLeft(105)); row.removeFromLeft(6);
        show(sampleBrowserRefreshButton_, row.removeFromLeft(76));
        if (sampleScanInProgress_) show(sampleBrowserCancelButton_, row);
        area.removeFromTop(5); show(sampleBrowserRootLabel_, area.removeFromTop(22));
    }
    area.removeFromTop(8);
    if (browserScopeCombo_.getSelectedId() == 2)
    {
        row = area.removeFromTop(28);
        show(browserAutoPreview_, row.removeFromLeft(130)); show(browserPreviewLevel_, row);
        area.removeFromTop(8);
    }
    auto footer = area.removeFromBottom(96);
    browserHelpLabel_.setText(presets ? "Select, then Load sound. Presets do not preview."
        : browserScopeCombo_.getSelectedId() == 2 ? (browserAutoPreview_.getToggleState()
            ? "Click to preview. Replace sample commits the choice." : "Preview auditions. Replace sample commits the choice.")
        : "Select, then Load instrument to replace the sound.", juce::dontSendNotification);
    show(browserHelpLabel_, footer.removeFromBottom(28));
    if (presets) { show(presetCountLabel_, footer.removeFromBottom(24)); show(presetBrowserList_, area); }
    else { show(sampleBrowserCountLabel_, footer.removeFromBottom(24)); show(sampleBrowserListBox_, area); }
    auto actions = footer.removeFromTop(30);
    browserLoadButton_.setButtonText(presets ? "Load sound" : browserScopeCombo_.getSelectedId() == 2 ? "Replace sample" : "Load instrument");
    show(browserLoadButton_, actions.removeFromLeft(actions.getWidth() / 2)); actions.removeFromLeft(6);
    if (browserScopeCombo_.getSelectedId() == 2) show(browserStopButton_, actions);
    updateBrowserLibraryControls();
}

void AudiocityAudioProcessorEditor::resized()
{
    if (!workspaceReady_) return;
    updateTabVisibility();
    browserBackdrop_.setVisible(false);
    browserDockButton_.setVisible(false); browserHelpLabel_.setVisible(false);
    browserAutoPreview_.setVisible(false); browserPreviewLevel_.setVisible(false);
    mappingControlsViewport_.setVisible(false);
    // The layout owns visibility. No width-dependent reparenting or card promotion.
    for (auto* c : sampleControlsContent_.getChildren()) { c->setVisible(false); c->setBounds({}); }
    for (auto* c : std::initializer_list<juce::Component*>{ &tabBar_, &presetFilterEditor_, &presetCountLabel_, &presetCombo_,
        &presetRenameButton_, &presetDeleteButton_, &loadButton_, &sampleInspectorRailToggleButton_, &diagnosticsToggleButton_,
        &waveformView_, &waveformResetRangesButton_, &waveformInteractionSummaryLabel_, &sampleControlsViewport_,
        &playerKeyboardLabel_, &playerPadsLabel_, &playerKeyboardViewport_, &playerStatusDisplay_, &playerOpenButton_,
        &sampleBrowserRootLabel_, &sampleBrowserChooseRootButton_, &sampleBrowserRefreshButton_, &sampleBrowserCancelButton_,
        &sampleBrowserFilterEditor_, &sampleBrowserSortCombo_, &sampleBrowserFavoriteButton_, &sampleBrowserFavoritesOnlyToggle_,
        &sampleBrowserRecentOnlyToggle_, &sampleBrowserBookmarkCombo_, &sampleBrowserAddBookmarkButton_, &sampleBrowserRemoveBookmarkButton_,
        &sampleBrowserTagFilterCombo_, &sampleBrowserTagsEditor_, &sampleBrowserApplyTagsButton_, &sampleBrowserListBox_,
        &sampleBrowserCountLabel_, &sampleBrowserPreviewLabel_, &browserScopeCombo_, &browserLoadButton_, &browserStopButton_,
        &browserCloseButton_, &presetBrowserList_, &closeToolButton_, &detailsButton_, &sliceButton_, &sourceMenuButton_, &sourceIdentityLabel_ })
        c->setVisible(false);
    for (int i = 0; i < kPlayerPadCount; ++i) { playerPadButtons_[i].setVisible(false); playerPadAssignButtons_[i].setVisible(false); }
    groupBoxes_.clear(); workspaceBrowserBounds_ = {};
    sampleInspectorInfoBounds_ = {}; sampleInspectorProgramMapBounds_ = {}; sampleInspectorOutputBounds_ = {};
    sampleInspectorFilterModBounds_ = {}; sampleInspectorEffectsBounds_ = {};
    auto place = [](juce::Component& c, juce::Rectangle<int> b) { c.setBounds(b); c.setVisible(!b.isEmpty()); };
    auto area = getLocalBounds().reduced(14);
    auto header = area.removeFromTop(52);
    place(sampleBrowserRailToggleButton_, header.removeFromLeft(76).withSizeKeepingCentre(76, 30)); header.removeFromLeft(12);
    place(workspaceMenuButton_, header.removeFromRight(60).withSizeKeepingCentre(60, 30)); header.removeFromRight(8);
    place(presetSaveButton_, header.removeFromRight(64).withSizeKeepingCentre(64, 30)); header.removeFromRight(12);
    place(masterVolumeDial_, header.removeFromRight(132)); header.removeFromRight(10);
    place(outputLevelMeter_, header.removeFromRight(104).withSizeKeepingCentre(104, 40)); header.removeFromRight(12);
    place(soundIdentityLabel_, header);
    area.removeFromTop(8); auto navigation = area.removeFromTop(32);
    place(soundViewButton_, navigation.removeFromLeft(84)); navigation.removeFromLeft(8);
    place(modulationViewButton_, navigation.removeFromLeft(112)); navigation.removeFromLeft(8);
    place(closeToolButton_, navigation.removeFromRight(132)); closeToolButton_.setVisible(currentTabIndex_ != 0 || soundDetailsView_);
    soundViewButton_.setToggleState(currentTabIndex_ == 0 && !modulationView_ && !soundDetailsView_, juce::dontSendNotification);
    modulationViewButton_.setToggleState(currentTabIndex_ == 0 && modulationView_, juce::dontSendNotification);
    area.removeFromTop(10);
    const bool dockBrowser = browserOpen_ && browserDocked_ && getWidth() >= 1400;
    juce::Rectangle<int> dockBounds;
    if (dockBrowser) { dockBounds = area.removeFromLeft(390); area.removeFromLeft(14); }
    const bool compact = getHeight() < 800;
    const bool acquisition = currentTabIndex_ == 4 || currentTabIndex_ == 5;
    auto footer = area.removeFromBottom(acquisition || auditionModeCombo_.getSelectedId() == 4 ? 30 : compact ? 64 : 88);
    auto status = footer.removeFromTop(26);
    place(panicButton_, status.removeFromRight(118)); status.removeFromRight(10);
    place(auditionModeCombo_, status.removeFromRight(104)); status.removeFromRight(10);
    place(workspaceStatusLabel_, status);
    auditionModeCombo_.setVisible(!acquisition);
    const bool slices = processor_.getImportedProgramSliceMarkerSamples().size() > 1;
    const bool pads = auditionModeCombo_.getSelectedId() == 3 || (auditionModeCombo_.getSelectedId() == 1 && slices);
    footer.removeFromTop(6);
    if (!acquisition && auditionModeCombo_.getSelectedId() != 4)
    {
        if (pads)
        {
            const int width = (footer.getWidth() - 7 * 8) / 8;
            for (int i = 0; i < kPlayerPadCount; ++i) { place(playerPadButtons_[i], footer.removeFromLeft(width)); footer.removeFromLeft(8); }
        }
        else { place(playerKeyboardViewport_, footer); updatePlayerKeyboardSizing(); }
    }
    auto expression = acquisition ? juce::Rectangle<int>{} : area.removeFromBottom(compact ? 42 : 62); area.removeFromBottom(8);
    auto firstMacro = expression.removeFromLeft((expression.getWidth() - 24) / 2); expression.removeFromLeft(24);
    place(modulationPanel_.macroControl(0), firstMacro); place(modulationPanel_.macroControl(1), expression);
    if (acquisition) { modulationPanel_.macroControl(0).setVisible(false); modulationPanel_.macroControl(1).setVisible(false); }
    if (currentTabIndex_ == 1) { currentTabIndex_ = 0; browserOpen_ = true; tabBar_.setCurrentTabIndex(0); }
    if (currentTabIndex_ == 3) { currentTabIndex_ = 0; tabBar_.setCurrentTabIndex(0); }
    if (currentTabIndex_ == 2)
    {
        place(mappingControlsViewport_, area);
        mappingControlsContent_.setSize(area.getWidth()-14, juce::jmax(620, area.getHeight()));
        mappingPage_.layout(mappingControlsContent_.getLocalBounds());
    }
    else if (currentTabIndex_ == 4) { place(generatePage_, area); closeToolButton_.setButtonText("Cancel / return"); }
    else if (currentTabIndex_ == 5) { place(capturePage_, area); closeToolButton_.setButtonText("Cancel / return"); }
    else if (currentTabIndex_ == 6) place(aboutPage_, area);
    else
    {
        closeToolButton_.setButtonText("Back to Sound");
        auto source = area.removeFromTop(30);
        place(sourceMenuButton_, source.removeFromRight(100)); source.removeFromRight(8);
        place(sliceButton_, source.removeFromRight(70)); source.removeFromRight(8);
        place(detailsButton_, source.removeFromRight(74)); source.removeFromRight(8);
        place(sourceIdentityLabel_, source); area.removeFromTop(8);
        if (!modulationView_ && !soundDetailsView_)
        {
            place(waveformView_, area.removeFromTop(compact ? 96 : juce::jlimit(108, 156, getHeight() / 6)));
            auto hint = area.removeFromTop(28);
            place(waveformResetRangesButton_, hint.removeFromRight(64).reduced(0, 2));
            place(waveformInteractionSummaryLabel_, hint);
        }
        place(sampleControlsViewport_, area);
        const int width = juce::jmax(1, area.getWidth() - 12);
        int y = 0;
        const auto row = [&](int height) { auto r = juce::Rectangle<int>(0, y, width, height); y += height + 8; return r; };
        const auto group = [&](const juce::String& title, juce::Rectangle<int> r)
        {
            groupBoxes_.push_back({{}, title, r, true, false});
            return r.withTrimmedTop(24).reduced(8, 3);
        };
        const auto cells = [&](juce::Rectangle<int> r, std::initializer_list<juce::Component*> controls)
        {
            const int cellWidth = (r.getWidth() - 6 * (static_cast<int>(controls.size()) - 1)) / static_cast<int>(controls.size());
            for (auto* c : controls)
            {
                auto cell = r.removeFromLeft(cellWidth);
                if (c == &rootNoteCombo_) place(rootNoteLabel_, cell.removeFromTop(18));
                if (dynamic_cast<juce::Button*>(c) || dynamic_cast<juce::ComboBox*>(c))
                    cell = cell.withSizeKeepingCentre(cell.getWidth(), juce::jmin(30, cell.getHeight()));
                place(*c, cell); r.removeFromLeft(6);
            }
        };
        if (modulationView_)
        {
            place(modulationPanel_, row(420));
            auto r = group("Pitch and amplitude movement", row(120));
            cells(r, { &pitchLfoRateDial_, &pitchLfoDepthDial_, &ampLfoRateDial_, &ampLfoDepthDial_, &ampLfoShapeCombo_ });
            r = group("Filter envelope", row(140));
            place(filterEnvelopeGraph_, r.removeFromRight(r.getWidth()/3).reduced(8));
            cells(r, { &filterAttackDial_, &filterDecayDial_, &filterSustainDial_, &filterReleaseDial_ });
            r = group("Filter movement", row(120));
            cells(r, { &filterKeytrackDial_, &filterVelDial_, &filterLfoRateDial_, &filterLfoAmtDial_, &filterLfoShapeCombo_ });
            r = group("LFO timing", row(70));
            cells(r, { &filterLfoRetriggerToggle_, &filterLfoTempoSyncToggle_, &filterLfoDivisionCombo_, &filterLfoUnipolarToggle_ });
            r = group("LFO expression", row(120));
            cells(r, { &filterLfoRateKeyDial_, &filterLfoAmtKeyDial_, &filterLfoStartPhaseDial_, &filterLfoStartRandDial_, &filterLfoFadeInDial_ });
            r = group("Keyboard tracking", row(70));
            cells(r, { &filterLfoRateKeySyncToggle_, &filterLfoKeytrackLinearToggle_, &filterKeytrackSnapCombo_ });
        }
        else if (soundDetailsView_)
        {
            auto r = group("Sample regions - exact sample frames", row(120));
            cells(r, { &playbackStartDial_, &playbackEndDial_, &loopStartDial_, &loopEndDial_, &loopCrossfadeDial_, &fadeInDial_, &fadeOutDial_ });
            r = group("Voice and expression settings", row(115));
            cells(r, { &legatoToggle_, &glideDial_, &polyphonyDial_, &pitchBendRangeDial_, &velocityCurveCombo_, &panDial_ });
            r = group("Effects detail", row(120));
            cells(r, { &delayTimeDial_, &delayFeedbackDial_, &delayTempoSyncToggle_, &autopanRateDial_, &autopanDepthDial_, &saturationModeCombo_ });
            r = group("Playback quality and maintenance", row(115));
            cells(r, { &preloadDial_, &qualityCpuButton_, &qualityFidelityButton_, &qualityUltraButton_, &dcFilterEnabledToggle_, &dcFilterCutoffDial_ });
            r = group("Source information", row(130));
            place(sampleInfoSourceValue_, r.removeFromTop(26));
            cells(r.removeFromTop(24), { &sampleInfoRateLabel_, &sampleInfoChannelsLabel_, &sampleInfoDurationLabel_, &sampleInfoFileSizeLabel_ });
            cells(r, { &sampleInfoRateValue_, &sampleInfoChannelsValue_, &sampleInfoDurationValue_, &sampleInfoFileSizeValue_ });
            r = group("Import diagnostics", row(100));
            place(copyImportDiagnosticsButton_, r.removeFromRight(110)); place(diagnosticsLabel_, r);
        }
        else
        {
            auto r = group("Playback and tuning", row(92));
            if (processor_.hasImportedProgram())
            {
                auto scope = r.removeFromLeft(r.getWidth() / 2); r.removeFromLeft(12);
                place(editZonesButton_, scope.removeFromRight(100).withSizeKeepingCentre(100, 30));
                zoneScopeLabel_.setText("Roots, regions and playback belong to each zone.", juce::dontSendNotification);
                place(zoneScopeLabel_, scope.reduced(4));
                cells(r, { &tuneCoarseDial_, &tuneFineDial_, &monoToggle_, &reverseToggle_ });
            }
            else cells(r, { &playbackModeGateButton_, &playbackModeOneShotButton_, &playbackModeLoopButton_, &rootNoteCombo_,
                &tuneCoarseDial_, &tuneFineDial_, &monoToggle_, &reverseToggle_ });
            auto shaping = row(compact ? 126 : 142); auto amp = shaping.removeFromLeft((width - 12) / 2); shaping.removeFromLeft(12);
            r = group("Amp envelope", amp);
            place(ampEnvelopeGraph_, r.removeFromTop(compact ? 26 : 36));
            cells(r, { &ampAttackDial_, &ampDecayDial_, &ampSustainDial_, &ampReleaseDial_ });
            r = group("Filter", shaping);
            place(filterResponseGraph_, r.removeFromTop(compact ? 26 : 36));
            cells(r, { &filterCutoffDial_, &filterResDial_, &filterEnvAmtDial_, &filterTypeCombo_ });
            r = group("Effects", row(compact ? 72 : 92));
            cells(r, { &reverbMixDial_, &delayMixDial_, &saturationDriveDial_ });
        }
        sampleControlsContent_.setSize(width, juce::jmax(area.getHeight(), y));
    }
    mappingNewLibraryButton_.setVisible(false); mappingCreateZoneButton_.setVisible(false);
    mappingDuplicateZoneButton_.setVisible(false); mappingSplitZoneButton_.setVisible(false);
    mappingAddSampleButton_.setVisible(false); mappingRefreshButton_.setVisible(false);
    mappingSaveLibraryButton_.setButtonText("Export instrument");
    for (auto* control : std::initializer_list<juce::Component*>{ &mappingEditVelocityFadeInLabel_, &mappingEditVelocityFadeInLowSlider_, &mappingEditVelocityFadeInHighSlider_,
        &mappingEditVelocityFadeOutLabel_, &mappingEditVelocityFadeOutLowSlider_, &mappingEditVelocityFadeOutHighSlider_,
        &mappingEditRoundRobinGroupLabel_, &mappingEditRoundRobinGroupSlider_, &mappingEditRoundRobinPositionLabel_, &mappingEditRoundRobinPositionSlider_,
        &mappingEditRoundRobinModeLabel_, &mappingEditRoundRobinModeCombo_, &mappingEditChokeLabel_, &mappingEditChokeSlider_ }) control->setVisible(false);
    updateSoundIdentity();
    rootNoteCombo_.setEnabled(!processor_.hasImportedProgram());
    const bool mapped = processor_.hasImportedProgram();
    const bool editableSlices = processor_.getImportedProgramFormat() == audiocity::plugin::ImportedProgramFormat::sampleSlices;
    waveformView_.setEnabled(!mapped || editableSlices);
    waveformResetRangesButton_.setVisible(!mapped && !modulationView_ && !soundDetailsView_ && currentTabIndex_ == 0);
    for (auto* control : { &playbackStartDial_, &playbackEndDial_, &loopStartDial_, &loopEndDial_, &loopCrossfadeDial_, &fadeInDial_, &fadeOutDial_ })
    {
        control->setEnabled(!mapped);
        control->setTooltip(mapped ? "Mapped regions belong to zones. Use Advanced mapping to repair a selected zone." : "Exact region boundary in sample frames.");
    }
    rootNoteCombo_.setTooltip(processor_.hasImportedProgram() ? "Instrument root notes are edited in Advanced mapping." : "Root note: the original pitch of this sample.");
    for (auto* button : { &playbackModeGateButton_, &playbackModeOneShotButton_, &playbackModeLoopButton_ })
    {
        button->setEnabled(!processor_.hasImportedProgram());
        button->setTooltip(processor_.hasImportedProgram() ? "Each mapped zone has its own playback behavior. Open Advanced mapping." : "Playback behavior for this sample.");
    }
    if (browserOpen_)
    {
        // A temporary browser keeps the underlying controls in their learned locations.
        auto browser = getLocalBounds().reduced(14).withTrimmedTop(68).withTrimmedBottom(100);
        browser.setWidth(juce::jmin(390, browser.getWidth()));
        layoutInstrumentBrowser(dockBrowser ? dockBounds : browser);
    }
    sampleBrowserRailToggleButton_.setToggleState(browserOpen_, juce::dontSendNotification);
    repaint();
}

void AudiocityAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(background));
    g.setColour(juce::Colour(border));
    g.drawHorizontalLine(73, 14.0f, static_cast<float>(getWidth()-14));
    if (!workspaceBrowserBounds_.isEmpty())
    {
        g.setColour(juce::Colour(panel)); g.fillRoundedRectangle(workspaceBrowserBounds_.toFloat(), 8.0f);
        g.setColour(juce::Colour(border)); g.drawRoundedRectangle(workspaceBrowserBounds_.toFloat(), 8.0f, 1.0f);
    }
    if (isHoveringValidDrop_)
    {
        g.setColour(juce::Colour(0x3372d5df)); g.fillRect(getLocalBounds().reduced(10));
    }
}

void AudiocityAudioProcessorEditor::rememberSoundBeforeReplacement()
{
    captureSoundRecovery(previousSoundState_);
    previousSoundName_ = currentPresetName_;
}

void AudiocityAudioProcessorEditor::captureSoundRecovery(juce::MemoryBlock& destination)
{
    processor_.getStateInformation(destination);
    if (processor_.hasImportedProgram() || processor_.getLoadedSamplePath().isEmpty()) return;
    // Reuse preset embedding so a loaded single sample remains recoverable even
    // if its original file is moved during the editing session.
    const auto preset = juce::parseXML(processor_.createPlaybackPresetXml());
    const auto stateXml = juce::AudioProcessor::getXmlFromBinary(destination.getData(), static_cast<int>(destination.getSize()));
    if (!preset || !stateXml) return;
    auto state = juce::ValueTree::fromXml(*stateXml);
    const auto source = juce::ValueTree::fromXml(*preset);
    for (int i = 0; i < source.getNumProperties(); ++i)
    {
        const auto key = source.getPropertyName(i);
        if (key.toString().startsWith("embeddedSample")) state.setProperty(key, source.getProperty(key), nullptr);
    }
    if (const auto xml = state.createXml()) juce::AudioProcessor::copyXmlToBinary(*xml, destination);
}

void AudiocityAudioProcessorEditor::restorePreviousSound()
{
    if (previousSoundState_.getSize() == 0) return;
    juce::MemoryBlock current;
    captureSoundRecovery(current);
    const auto currentName = currentPresetName_;
    processor_.setStateInformation(previousSoundState_.getData(), static_cast<int>(previousSoundState_.getSize()));
    previousSoundState_.swapWith(current);
    currentPresetName_ = previousSoundName_; previousSoundName_ = currentName;
    currentSoundFile_ = {}; soundEdited_ = true;
    editorUndoHistory_.clear(); lastSettingsSnapshot_ = captureSettingsSnapshot();
    refreshUI(true); showWorkspace(0);
}

bool AudiocityAudioProcessorEditor::savePresetToFile(const juce::File& file)
{
    const auto xml = processor_.createPlaybackPresetXml();
    if (xml.isEmpty() || !file.getParentDirectory().createDirectory() || !file.replaceWithText(xml))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Save sound",
            "Could not save to " + file.getFullPathName() + ". Your sound is still open.");
        return false;
    }
    currentSoundFile_ = file;
    currentPresetName_ = file.getFileNameWithoutExtension();
    markSoundSaved();
    processor_.markLibraryRecent(file.getFullPathName());
    refreshPresetList(currentPresetName_); updateSoundIdentity();
    return true;
}

void AudiocityAudioProcessorEditor::loadPresetFile(const juce::File& file)
{
    if (file == juce::File{}) return;
    const auto result = replaceSoundFromPreset(file);
    if (result.failed()) showPresetLoadErrorAndOfferDelete(file, result.getErrorMessage());
}

juce::Result AudiocityAudioProcessorEditor::replaceSoundFromPreset(const juce::File& file)
{
    const auto xml = file.loadFileAsString();
    if (xml.isEmpty()) return juce::Result::fail("The file is missing or unreadable.");
    cancelBackgroundInstrumentLoad();
    juce::MemoryBlock before;
    captureSoundRecovery(before);
    juce::String error;
    if (!processor_.loadPlaybackPresetXml(xml, error)) return juce::Result::fail(error);
    if (processor_.hasPendingImportedAssetRelink()
        || processor_.getLastStateRestoreSourceLabel() == "none")
    {
        processor_.setStateInformation(before.getData(), static_cast<int>(before.getSize()));
        refreshUI(true);
        return juce::Result::fail(error.isNotEmpty() ? error : "Referenced source samples could not be restored.");
    }
    previousSoundState_.swapWith(before); previousSoundName_ = currentPresetName_;
    currentSoundFile_ = file; currentPresetName_ = file.getFileNameWithoutExtension();
    markSoundSaved();
    editorUndoHistory_.clear(); lastSettingsSnapshot_ = savedSettingsSnapshot_;
    processor_.markLibraryRecent(file.getFullPathName());
    refreshPresetList(currentPresetName_); refreshUI(true); showWorkspace(0);
    return juce::Result::ok();
}
