#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

// Custom LookAndFeel for rotary dials: large diameter knob, thin arc track.
class DialLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    DialLookAndFeel()
    {
        setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff78d7ff));
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff334252));
        setColour(juce::Slider::thumbColourId, juce::Colour(0xfff5fbff));
        setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xff9ba7b9));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::trackColourId, juce::Colour(0xff78d7ff));
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xff2a3340));

        setColour(juce::ToggleButton::textColourId, juce::Colour(0xffcbd5e5));
        setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff78d7ff));
        setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff2a3340));

        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1d2530));
        setColour(juce::ComboBox::textColourId, juce::Colour(0xffd7e1f2));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff334252));
        setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff78d7ff));
        setColour(juce::ComboBox::buttonColourId, juce::Colour(0xff1d2530));

        setColour(juce::Label::textColourId, juce::Colour(0xffcbd5e5));

        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff202833));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff31516a));
        setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdfe8f7));
        setColour(juce::TextButton::textColourOnId, juce::Colour(0xfff5fbff));

        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff1a202a));
        setColour(juce::PopupMenu::textColourId, juce::Colour(0xffd7e1f2));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff31516a));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);

        setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xfff3e6c8));
        setColour(juce::TooltipWindow::textColourId, juce::Colour(0xff2d2b28));
        setColour(juce::TooltipWindow::outlineColourId, juce::Colour(0xffbfae8e));

        setColour(juce::ScrollBar::backgroundColourId, juce::Colour(0xff121821));
        setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xff334252));
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
        const auto isPanDial = slider.getName().equalsIgnoreCase("Pan");
        if (isPanDial)
        {
            const auto sliderMin = slider.getMinimum();
            const auto sliderMax = slider.getMaximum();
            const auto sliderValue = slider.getValue();
            const auto sliderRange = sliderMax - sliderMin;

            if (sliderRange > 0.0 && sliderMin < 0.0 && sliderMax > 0.0)
            {
                const auto zeroPosProportional = static_cast<float>(juce::jlimit(0.0, 1.0, (0.0 - sliderMin) / sliderRange));
                const auto zeroAngle = rotaryStartAngle + zeroPosProportional * (rotaryEndAngle - rotaryStartAngle);

                if (std::abs(sliderValue) > 1.0e-6)
                {
                    const auto fillStart = juce::jmin(zeroAngle, angle);
                    const auto fillEnd = juce::jmax(zeroAngle, angle);

                    juce::Path fill;
                    fill.addCentredArc(centreX, centreY, radius - trackWidth * 0.5f, radius - trackWidth * 0.5f,
                                       0.0f, fillStart, fillEnd, true);
                    g.setColour(fillColour);
                    g.strokePath(fill, juce::PathStrokeType(trackWidth, juce::PathStrokeType::curved,
                                                             juce::PathStrokeType::rounded));
                }
            }
        }
        else if (sliderPosProportional > 0.0f)
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

            g.setColour(juce::Colour(0x22000000));
            g.fillEllipse(knobBounds.translated(0.0f, 1.4f));

            auto grad = juce::ColourGradient(juce::Colour(0xff334050), centreX, centreY - knobRadius,
                                             juce::Colour(0xff1a2029), centreX, centreY + knobRadius, false);
            grad.addColour(0.42, juce::Colour(0xff273241));
            g.setGradientFill(grad);
            g.fillEllipse(knobBounds);

            g.setColour(juce::Colour(0xff46556a));
            g.drawEllipse(knobBounds, 1.0f);
        }

        {
            const float pointerLength = knobRadius * 0.7f;
            const float pointerThickness = 2.5f;
            juce::Path pointer;
            pointer.addRoundedRectangle(-pointerThickness * 0.5f, -pointerLength, pointerThickness, pointerLength, 1.0f);
            pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
            g.setColour(fillColour.brighter(0.18f));
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

    // ── Buttons ──────────────────────────────────────────────────────────
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override
    {
        return juce::Font(juce::FontOptions(juce::jlimit(11.5f, 13.5f, buttonHeight * 0.42f)))
                   .withStyle(juce::Font::plain);
    }

    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool isMouseOverButton,
                              bool isButtonDown) override
    {
        const auto area = button.getLocalBounds().toFloat().reduced(0.5f);
        const float corner = juce::jmin(7.0f, area.getHeight() * 0.32f);
        const bool enabled = button.isEnabled();
        const bool isOn = button.getToggleState();

        // Detect "segmented" radio context (TextButtons in a radio group rendered as a connected bar).
        const bool isSegment = button.getRadioGroupId() != 0;

        auto baseFill = backgroundColour;
        if (isOn)
            baseFill = button.findColour(juce::TextButton::buttonOnColourId);

        if (!enabled)
            baseFill = baseFill.withMultipliedAlpha(0.45f);
        else if (isButtonDown)
            baseFill = baseFill.brighter(0.06f);
        else if (isMouseOverButton)
            baseFill = baseFill.brighter(0.10f);

        // Subtle vertical lift gradient for depth.
        juce::ColourGradient grad(baseFill.brighter(0.04f), area.getCentreX(), area.getY(),
                                  baseFill.darker(0.08f),  area.getCentreX(), area.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(area, corner);

        // Border: brighter when on, accent-tinted when in segmented "on" state.
        auto border = juce::Colour(0xff313d4c);
        if (isOn)
            border = juce::Colour(0xff78d7ff).withAlpha(isSegment ? 0.65f : 0.45f);
        if (!enabled)
            border = border.withMultipliedAlpha(0.45f);

        g.setColour(border);
        g.drawRoundedRectangle(area, corner, 1.0f);

        // Top inner highlight to make raised buttons feel premium.
        if (enabled && !isButtonDown)
        {
            g.setColour(juce::Colours::white.withAlpha(0.045f));
            g.drawLine(area.getX() + corner, area.getY() + 1.0f,
                       area.getRight() - corner, area.getY() + 1.0f, 1.0f);
        }
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool /*isMouseOverButton*/, bool /*isButtonDown*/) override
    {
        const auto font = getTextButtonFont(button, button.getHeight());
        g.setFont(font);

        auto text = button.findColour(button.getToggleState()
                                          ? juce::TextButton::textColourOnId
                                          : juce::TextButton::textColourOffId);
        if (!button.isEnabled())
            text = text.withMultipliedAlpha(0.45f);

        g.setColour(text);

        const int yIndent  = juce::jmin(4, button.proportionOfHeight(0.3f));
        const int leftPad  = juce::jmin(button.getHeight(), button.getWidth()) / 4;
        const int rightPad = leftPad;
        const int textW    = button.getWidth() - leftPad - rightPad;

        if (textW > 0)
            g.drawFittedText(button.getButtonText(),
                             leftPad, yIndent, textW, button.getHeight() - yIndent * 2,
                             juce::Justification::centred, 1, 0.85f);
    }

    // ── Toggle button (checkbox / radio / segmented) ─────────────────────
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool isMouseOverButton, bool isButtonDown) override
    {
        // When a ToggleButton is in a radio group, render it as a segmented pill
        // (matches behaviour of CPU/Fidelity/Ultra and similar exclusive groups).
        if (button.getRadioGroupId() != 0)
        {
            const auto area = button.getLocalBounds().toFloat().reduced(0.5f);
            const float corner = juce::jmin(6.0f, area.getHeight() * 0.32f);
            const bool isOn   = button.getToggleState();
            const bool enabled = button.isEnabled();

            auto baseFill = isOn ? juce::Colour(0xff31516a) : juce::Colour(0xff202833);
            if (!enabled)         baseFill = baseFill.withMultipliedAlpha(0.45f);
            else if (isButtonDown) baseFill = baseFill.brighter(0.06f);
            else if (isMouseOverButton) baseFill = baseFill.brighter(0.10f);

            juce::ColourGradient grad(baseFill.brighter(0.04f), area.getCentreX(), area.getY(),
                                      baseFill.darker(0.08f),  area.getCentreX(), area.getBottom(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(area, corner);

            auto border = isOn ? juce::Colour(0xff78d7ff).withAlpha(0.55f)
                               : juce::Colour(0xff313d4c);
            if (!enabled) border = border.withMultipliedAlpha(0.45f);
            g.setColour(border);
            g.drawRoundedRectangle(area, corner, 1.0f);

            auto textColour = isOn ? juce::Colour(0xfff5fbff) : juce::Colour(0xffcbd5e5);
            if (!enabled) textColour = textColour.withMultipliedAlpha(0.55f);
            g.setColour(textColour);
            g.setFont(juce::Font(juce::FontOptions(juce::jlimit(11.5f, 13.0f, area.getHeight() * 0.42f))));
            g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(6, 2),
                             juce::Justification::centred, 1);
            return;
        }

        // Standard checkbox: small rounded box + tick + text.
        const auto bounds = button.getLocalBounds();
        const auto tickSize = juce::jmin(16, bounds.getHeight() - 2);
        const auto boxBounds = juce::Rectangle<float>(static_cast<float>(bounds.getX()),
                                                      static_cast<float>(bounds.getY() + (bounds.getHeight() - tickSize) / 2),
                                                      static_cast<float>(tickSize),
                                                      static_cast<float>(tickSize));

        const bool isOn = button.getToggleState();
        const bool enabled = button.isEnabled();

        auto fill = juce::Colour(0xff1d2530);
        if (isMouseOverButton && enabled) fill = fill.brighter(0.10f);
        if (!enabled) fill = fill.withMultipliedAlpha(0.55f);

        g.setColour(fill);
        g.fillRoundedRectangle(boxBounds, 3.5f);
        g.setColour(juce::Colour(0xff313d4c).withMultipliedAlpha(enabled ? 1.0f : 0.5f));
        g.drawRoundedRectangle(boxBounds, 3.5f, 1.0f);

        if (isOn)
        {
            auto tick = button.findColour(juce::ToggleButton::tickColourId);
            if (!enabled) tick = tick.withMultipliedAlpha(0.5f);
            g.setColour(tick);

            // Draw a clean check mark.
            juce::Path p;
            const auto b = boxBounds.reduced(boxBounds.getWidth() * 0.22f);
            p.startNewSubPath(b.getX(),         b.getCentreY());
            p.lineTo(b.getCentreX() - b.getWidth() * 0.05f, b.getBottom() - b.getHeight() * 0.18f);
            p.lineTo(b.getRight(),  b.getY()    + b.getHeight() * 0.10f);
            g.strokePath(p, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        }

        auto textColour = button.findColour(juce::ToggleButton::textColourId);
        if (!enabled) textColour = textColour.withMultipliedAlpha(0.55f);
        g.setColour(textColour);
        g.setFont(juce::Font(juce::FontOptions(juce::jlimit(11.5f, 13.0f, bounds.getHeight() * 0.42f))));
        const auto textArea = juce::Rectangle<int>(bounds.getX() + tickSize + 6, bounds.getY(),
                                                   bounds.getWidth() - tickSize - 6, bounds.getHeight());
        g.drawFittedText(button.getButtonText(), textArea, juce::Justification::centredLeft, 1);

        juce::ignoreUnused(isButtonDown);
    }

    // ── Combo box ────────────────────────────────────────────────────────
    void drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                      int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                      juce::ComboBox& box) override
    {
        const auto area = juce::Rectangle<float>(0.0f, 0.0f,
                                                 static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
        const float corner = juce::jmin(6.0f, height * 0.32f);
        const bool enabled = box.isEnabled();

        auto fill = box.findColour(juce::ComboBox::backgroundColourId);
        if (!enabled) fill = fill.withMultipliedAlpha(0.55f);

        juce::ColourGradient grad(fill.brighter(0.04f), area.getCentreX(), area.getY(),
                                  fill.darker(0.06f),  area.getCentreX(), area.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(area, corner);

        auto border = box.findColour(juce::ComboBox::outlineColourId);
        if (!enabled) border = border.withMultipliedAlpha(0.5f);
        g.setColour(border);
        g.drawRoundedRectangle(area, corner, 1.0f);

        auto arrow = box.findColour(juce::ComboBox::arrowColourId);
        if (!enabled) arrow = arrow.withMultipliedAlpha(0.5f);
        g.setColour(arrow);

        const float arrowH = 4.0f;
        const float arrowW = 7.0f;
        const float cx = static_cast<float>(width) - 12.0f;
        const float cy = static_cast<float>(height) * 0.5f;
        juce::Path p;
        p.startNewSubPath(cx - arrowW * 0.5f, cy - arrowH * 0.5f);
        p.lineTo(cx,                          cy + arrowH * 0.5f);
        p.lineTo(cx + arrowW * 0.5f,          cy - arrowH * 0.5f);
        g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(8, 1, box.getWidth() - 24, box.getHeight() - 2);
        label.setFont(juce::Font(juce::FontOptions(juce::jlimit(11.5f, 13.0f, box.getHeight() * 0.42f))));
    }

    // ── Linear slider (used in Mapping pane) ─────────────────────────────
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical)
        {
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height,
                                                    sliderPos, minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const bool horizontal = style == juce::Slider::LinearHorizontal;
        const float trackThickness = 4.0f;
        const auto area = juce::Rectangle<int>(x, y, width, height).toFloat();
        const auto centre = area.getCentre();

        juce::Rectangle<float> track;
        if (horizontal)
            track = juce::Rectangle<float>(area.getX(), centre.y - trackThickness * 0.5f,
                                           area.getWidth(), trackThickness);
        else
            track = juce::Rectangle<float>(centre.x - trackThickness * 0.5f, area.getY(),
                                           trackThickness, area.getHeight());

        g.setColour(slider.findColour(juce::Slider::backgroundColourId));
        g.fillRoundedRectangle(track, trackThickness * 0.5f);

        // Filled portion.
        juce::Rectangle<float> filled = track;
        if (horizontal)
            filled.setRight(juce::jlimit(track.getX(), track.getRight(), sliderPos));
        else
            filled.setTop(juce::jlimit(track.getY(), track.getBottom(), sliderPos));

        const auto trackColour = slider.findColour(juce::Slider::trackColourId);
        g.setColour(slider.isEnabled() ? trackColour : trackColour.withMultipliedAlpha(0.5f));
        g.fillRoundedRectangle(filled, trackThickness * 0.5f);

        // Thumb.
        const float thumbR = juce::jmax(6.0f, juce::jmin(area.getHeight(), area.getWidth()) * 0.45f);
        juce::Point<float> thumbPos = horizontal ? juce::Point<float>(sliderPos, centre.y)
                                                  : juce::Point<float>(centre.x, sliderPos);

        // Soft drop shadow.
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillEllipse(thumbPos.x - thumbR + 0.5f, thumbPos.y - thumbR + 1.5f, thumbR * 2.0f, thumbR * 2.0f);

        juce::ColourGradient thumbGrad(juce::Colour(0xfff5fbff), thumbPos.x, thumbPos.y - thumbR,
                                       juce::Colour(0xffd0dae5), thumbPos.x, thumbPos.y + thumbR, false);
        g.setGradientFill(thumbGrad);
        g.fillEllipse(thumbPos.x - thumbR, thumbPos.y - thumbR, thumbR * 2.0f, thumbR * 2.0f);
        g.setColour(juce::Colour(0xff404e60));
        g.drawEllipse(thumbPos.x - thumbR, thumbPos.y - thumbR, thumbR * 2.0f, thumbR * 2.0f, 1.0f);

        juce::ignoreUnused(minSliderPos, maxSliderPos);
    }

    int getSliderThumbRadius(juce::Slider&) override { return 7; }

    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
        if (label != nullptr)
        {
            label->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
            label->setColour(juce::Label::outlineColourId,    juce::Colours::transparentBlack);
            label->setColour(juce::Label::backgroundWhenEditingColourId, juce::Colour(0xff1d2530));
            label->setColour(juce::Label::outlineWhenEditingColourId,    juce::Colour(0xff78d7ff).withAlpha(0.8f));
            label->setColour(juce::Label::textColourId, juce::Colour(0xff9ba7b9));
            label->setColour(juce::Label::textWhenEditingColourId, juce::Colour(0xffedf3ff));
            label->setJustificationType(juce::Justification::centred);
            label->setFont(juce::Font(juce::FontOptions(11.0f)));
        }
        return label;
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DialLookAndFeel)
};

