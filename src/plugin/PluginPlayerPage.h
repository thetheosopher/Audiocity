#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

class PluginPlayerPage final
{
public:
    static constexpr std::size_t kPadCount = 8;

    PluginPlayerPage(juce::Label& keyboardLabel,
                     juce::Component& statusDisplay,
                     std::function<void(bool)> setCompactHeaderOverlayEnabled,
                     juce::Component& openButton,
                     juce::Viewport& keyboardViewport,
                     juce::Label& padsLabel,
                     std::array<juce::TextButton*, kPadCount> padButtons,
                     std::array<juce::TextButton*, kPadCount> padAssignButtons,
                     std::function<void()> onKeyboardViewportLayoutChanged);

    void layout(juce::Rectangle<int> area, bool compactLayout);
    void paint(juce::Graphics& g, juce::Rectangle<int> area, bool compactLayout) const;

private:
    [[nodiscard]] static int computeKeyboardPanelHeight(int availableWidth) noexcept;

    juce::Label& keyboardLabel_;
    juce::Component& statusDisplay_;
    std::function<void(bool)> setCompactHeaderOverlayEnabled_;
    juce::Component& openButton_;
    juce::Viewport& keyboardViewport_;
    juce::Label& padsLabel_;
    std::array<juce::TextButton*, kPadCount> padButtons_;
    std::array<juce::TextButton*, kPadCount> padAssignButtons_;
    std::function<void()> onKeyboardViewportLayoutChanged_;
};
