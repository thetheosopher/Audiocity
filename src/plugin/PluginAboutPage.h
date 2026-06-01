#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class PluginAboutPage final : public juce::Component
{
public:
    PluginAboutPage();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::TextButton gitHubButton_{ "GitHub" };
    juce::TextButton coffeeButton_{ "Buy Me a Coffee" };
    juce::Image iconImage_;
};