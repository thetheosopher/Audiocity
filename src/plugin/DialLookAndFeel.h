#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

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

        // Tooltip
        setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xfff3e6c8));
        setColour(juce::TooltipWindow::textColourId, juce::Colour(0xff2d2b28));
        setColour(juce::TooltipWindow::outlineColourId, juce::Colour(0xffbfae8e));
    }

    ~DialLookAndFeel() override = default;

    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText,
                                          juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override
    {
        const auto font = juce::Font(juce::FontOptions(12.0f));
        constexpr int padX = 8;
        constexpr int padY = 5;
        constexpr int border = 4;
        constexpr int minTextWidth = 120;
        constexpr int maxTooltipWidth = 560;

        const auto availableWidth = juce::jmax(minTextWidth,
            juce::jmin(maxTooltipWidth, parentArea.getWidth() - (border + padX) * 2));

        const auto estimatedCharWidth = font.getHeight() * 0.58f;
        const auto singleLineWidth = static_cast<int>(std::ceil(estimatedCharWidth * static_cast<float>(tipText.length())));
        const auto targetTextWidth = juce::jlimit(minTextWidth, availableWidth, singleLineWidth);

        juce::AttributedString attributed;
        attributed.setJustification(juce::Justification::centredLeft);
        attributed.append(tipText, font, findColour(juce::TooltipWindow::textColourId));

        juce::TextLayout layout;
        layout.createLayout(attributed, static_cast<float>(targetTextWidth));

        const auto textHeight = static_cast<int>(std::ceil(layout.getHeight()));
        const auto width = targetTextWidth + (padX + border) * 2;
        const auto height = juce::jmax(22, textHeight + (padY + border) * 2);

        auto bounds = juce::Rectangle<int>(width, height).withPosition(screenPos.x + 18, screenPos.y + 22);
        return bounds.constrainedWithin(parentArea);
    }

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

    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override
    {
        constexpr int padX = 8;
        constexpr int padY = 5;
        constexpr int border = 4;

        const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));

        g.setColour(findColour(juce::TooltipWindow::backgroundColourId));
        g.fillRoundedRectangle(bounds.reduced(0.5f), 6.0f);

        g.setColour(findColour(juce::TooltipWindow::outlineColourId));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

        g.setColour(findColour(juce::TooltipWindow::textColourId));
        const auto font = juce::Font(juce::FontOptions(12.0f));

        juce::AttributedString attributed;
        attributed.setJustification(juce::Justification::centredLeft);
        attributed.append(text, font, findColour(juce::TooltipWindow::textColourId));

        juce::TextLayout layout;
        const auto textBounds = juce::Rectangle<int>(0, 0, width, height).reduced(padX + border, padY + border);
        layout.createLayout(attributed, static_cast<float>(textBounds.getWidth()));
        layout.draw(g, textBounds.toFloat());
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DialLookAndFeel)
};

