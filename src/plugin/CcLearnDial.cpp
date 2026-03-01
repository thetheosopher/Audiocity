#include "CcLearnDial.h"

// ─── CcLabel ───────────────────────────────────────────────────────────────────

CcLearnDial::CcLabel::CcLabel(CcLearnDial& owner, const juce::String& text)
    : juce::Label({}, text), owner_(owner)
{
    setJustificationType(juce::Justification::centred);
    setInterceptsMouseClicks(true, false);
}

void CcLearnDial::CcLabel::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isRightButtonDown())
    {
        juce::Label::mouseDown(event);
        return;
    }

    juce::PopupMenu menu;

    if (owner_.ccLearnArmed_)
    {
        menu.addItem(1, "Cancel CC Learn");
    }
    else
    {
        menu.addItem(2, "MIDI CC Learn");
        if (owner_.assignedCc_ >= 0)
            menu.addItem(3, "Clear CC " + juce::String(owner_.assignedCc_));
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
        [this](int result)
        {
            if (result == 1)
                owner_.disarmCcLearn();
            else if (result == 2)
                owner_.armCcLearn();
            else if (result == 3)
                owner_.clearCc();
        });
}

void CcLearnDial::CcLabel::paint(juce::Graphics& g)
{
    if (owner_.ccLearnArmed_)
    {
        g.fillAll(juce::Colours::orange.withAlpha(0.3f));
        g.setColour(juce::Colours::orange);
        g.drawRect(getLocalBounds(), 1);
    }
    else if (owner_.assignedCc_ >= 0)
    {
        g.fillAll(juce::Colours::deepskyblue.withAlpha(0.12f));
    }

    juce::Label::paint(g);
}

// ─── CcLearnDial ──────────────────────────────────────────────────────────────

CcLearnDial::CcLearnDial(const juce::String& name,
                           const double rangeMin, const double rangeMax,
                           const double step, const juce::String& suffix,
                           const double defaultValue)
    : label_(*this, name)
{
    label_.setColour(juce::Label::textColourId, juce::Colour(0xffb0b0c0));
    label_.setFont(juce::Font(juce::FontOptions(10.5f)));
    addAndMakeVisible(label_);

    slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 13);
    slider_.setRange(rangeMin, rangeMax, step);
    slider_.setShiftWheelFineFactor(8.0);
    slider_.setValue(defaultValue, juce::dontSendNotification);

    if (suffix.isNotEmpty())
        slider_.setTextValueSuffix(" " + suffix);

    slider_.onValueChange = [this]
    {
        if (onValueChange)
            onValueChange();
    };

    addAndMakeVisible(slider_);
}

void CcLearnDial::resized()
{
    auto area = getLocalBounds();
    label_.setBounds(area.removeFromTop(14));
    slider_.setBounds(area);
}

void CcLearnDial::setValue(const double newValue, const juce::NotificationType notification)
{
    slider_.setValue(newValue, notification);
}

double CcLearnDial::getValue() const noexcept
{
    return slider_.getValue();
}

void CcLearnDial::setRange(const double rangeMin, const double rangeMax, const double step)
{
    slider_.setRange(rangeMin, rangeMax, step);
}

void CcLearnDial::armCcLearn()
{
    ccLearnArmed_ = true;
    label_.repaint();
}

void CcLearnDial::disarmCcLearn()
{
    ccLearnArmed_ = false;
    label_.repaint();
}

void CcLearnDial::assignCc(const int ccNumber)
{
    assignedCc_ = ccNumber;
    ccLearnArmed_ = false;

    const auto base = label_.getText().upToFirstOccurrenceOf(" [", false, true);
    label_.setText(base + " [CC" + juce::String(ccNumber) + "]", juce::dontSendNotification);
    label_.repaint();
}

void CcLearnDial::clearCc()
{
    assignedCc_ = -1;
    ccLearnArmed_ = false;

    const auto base = label_.getText().upToFirstOccurrenceOf(" [", false, true);
    label_.setText(base, juce::dontSendNotification);
    label_.repaint();
}

void CcLearnDial::handleCcValue(const int ccValue)
{
    const auto normalized = static_cast<double>(juce::jlimit(0, 127, ccValue)) / 127.0;
    const auto mapped = slider_.getMinimum() + normalized * (slider_.getMaximum() - slider_.getMinimum());
    slider_.setValue(mapped, juce::sendNotificationSync);
}

void CcLearnDial::setEnabled(const bool enabled)
{
    slider_.setEnabled(enabled);
    label_.setEnabled(enabled);
    setAlpha(enabled ? 1.0f : 0.5f);
}

void CcLearnDial::setLabelTooltip(const juce::String& tooltip)
{
    label_.setTooltip(tooltip);
}

void CcLearnDial::setDialLookAndFeel(juce::LookAndFeel* laf)
{
    slider_.setLookAndFeel(laf);
}

void CcLearnDial::setShiftWheelFineFactor(const double factor)
{
    slider_.setShiftWheelFineFactor(factor);
}

