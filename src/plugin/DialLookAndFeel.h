#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Custom LookAndFeel for rotary dials: large diameter knob, thin arc track.
class DialLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    DialLookAndFeel()
    {
        // Dark theme colours
        setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff4fc3f7));   // bright blue arc
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff3a3a52)); // dark track
        setColour(juce::Slider::thumbColourId, juce::Colours::white);
        setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffc0c0d0));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff252538));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

        // Toggle buttons
        setColour(juce::ToggleButton::textColourId, juce::Colour(0xffc0c0d0));
        setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff4fc3f7));

        // ComboBox
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff252538));
        setColour(juce::ComboBox::textColourId, juce::Colour(0xffc0c0d0));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff3a3a52));
        setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff4fc3f7));

        // Label
        setColour(juce::Label::textColourId, juce::Colour(0xffc0c0d0));

        // TextButton
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d44));
        setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc0c0d0));

        // PopupMenu
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff252538));
        setColour(juce::PopupMenu::textColourId, juce::Colour(0xffc0c0d0));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff4fc3f7));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    }

    ~DialLookAndFeel() override = default;

    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        const auto fillColour = slider.findColour(juce::Slider::rotarySliderFillColourId);
        const auto outlineColour = slider.findColour(juce::Slider::rotarySliderOutlineColourId);

        const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(2.0f);
        const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
        const auto centreX = bounds.getCentreX();
        const auto centreY = bounds.getCentreY();
        const auto rx = centreX - radius;
        const auto ry = centreY - radius;
        const auto rw = radius * 2.0f;

        const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        constexpr float trackWidth = 3.0f;

        // Background arc (track)
        {
            juce::Path track;
            track.addCentredArc(centreX, centreY, radius - trackWidth * 0.5f, radius - trackWidth * 0.5f,
                                0.0f, rotaryStartAngle, rotaryEndAngle, true);
            g.setColour(outlineColour);
            g.strokePath(track, juce::PathStrokeType(trackWidth, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        }

        // Value arc (fill)
        if (sliderPosProportional > 0.0f)
        {
            juce::Path fill;
            fill.addCentredArc(centreX, centreY, radius - trackWidth * 0.5f, radius - trackWidth * 0.5f,
                               0.0f, rotaryStartAngle, angle, true);
            g.setColour(fillColour);
            g.strokePath(fill, juce::PathStrokeType(trackWidth, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        // Knob body
        const float knobRadius = radius * 0.62f;
        {
            auto knobBounds = juce::Rectangle<float>(rw, rw).withCentre({ centreX, centreY })
                                  .reduced((radius - knobRadius));

            // Gradient fill for 3D look
            auto grad = juce::ColourGradient(juce::Colour(0xff404058), centreX, centreY - knobRadius,
                                             juce::Colour(0xff28283c), centreX, centreY + knobRadius, false);
            g.setGradientFill(grad);
            g.fillEllipse(knobBounds);

            // Subtle border
            g.setColour(juce::Colour(0xff505068));
            g.drawEllipse(knobBounds, 1.0f);
        }

        // Pointer line
        {
            const float pointerLength = knobRadius * 0.7f;
            const float pointerThickness = 2.5f;
            juce::Path pointer;
            pointer.addRoundedRectangle(-pointerThickness * 0.5f, -pointerLength, pointerThickness, pointerLength, 1.0f);
            pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
            g.setColour(fillColour.brighter(0.3f));
            g.fillPath(pointer);
        }

        juce::ignoreUnused(rx, ry);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DialLookAndFeel)
};

