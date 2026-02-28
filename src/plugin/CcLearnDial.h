#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>

// A compact label + rotary dial composite with MIDI CC learn support.
// Right-click the label to arm CC learn; next incoming CC binds to this dial.
class CcLearnDial final : public juce::Component
{
public:
    // Construct with display name, range, and optional step/suffix/tooltip.
    CcLearnDial(const juce::String& name,
                double rangeMin, double rangeMax,
                double step = 0.0,
                const juce::String& suffix = {},
                double defaultValue = 0.0);

    ~CcLearnDial() override = default;

    void resized() override;

    // Value access
    void setValue(double newValue, juce::NotificationType notification = juce::dontSendNotification);
    [[nodiscard]] double getValue() const noexcept;

    // Callbacks
    std::function<void()> onValueChange;

    // CC learn
    void armCcLearn();
    void disarmCcLearn();
    [[nodiscard]] bool isCcLearnArmed() const noexcept { return ccLearnArmed_; }
    void assignCc(int ccNumber);
    void clearCc();
    [[nodiscard]] int getAssignedCc() const noexcept { return assignedCc_; }

    // Called by editor when a CC message arrives while this dial is armed,
    // or when the mapped CC moves.
    void handleCcValue(int ccValue);

    // Range info for CC scaling
    [[nodiscard]] double getRangeMin() const noexcept { return slider_.getMinimum(); }
    [[nodiscard]] double getRangeMax() const noexcept { return slider_.getMaximum(); }

    void setEnabled(bool enabled);

    // Set a tooltip on the label that describes this control
    void setLabelTooltip(const juce::String& tooltip);

    // Set a custom LookAndFeel for the slider
    void setDialLookAndFeel(juce::LookAndFeel* laf);

private:
    class CcLabel final : public juce::Label
    {
    public:
        CcLabel(CcLearnDial& owner, const juce::String& text);
        void mouseDown(const juce::MouseEvent& event) override;
        void paint(juce::Graphics& g) override;

    private:
        CcLearnDial& owner_;
    };

    CcLabel label_;
    juce::Slider slider_;

    int assignedCc_ = -1;
    bool ccLearnArmed_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CcLearnDial)
};

