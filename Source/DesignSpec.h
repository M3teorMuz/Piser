// Единая спецификация дизайна EXPitch (координаты в логических px панели 960x512).
// Все числа сняты с референса EXPitch_Reference_Pack; предпросмотр на Python использует те же значения.
#pragma once
#include <juce_graphics/juce_graphics.h>

namespace dz
{
    constexpr float W = 960.0f, H = 512.0f, kPanelRadius = 61.0f;

    // ручки
    inline juce::Point<float> pivotPitch()   { return { 199.0f, 215.0f }; }
    inline juce::Point<float> pivotFormant() { return { 484.0f, 214.5f }; }
    inline juce::Point<float> pivotFine()    { return { 764.5f, 215.0f }; }
    constexpr float kFaceR = 66.5f, kRingR = 79.5f, kArcR = 75.0f, kArcW = 5.2f;
    constexpr float kArcStart = -135.0f, kArcSpan = 270.0f;     // градусы от 12 часов по часовой

    // пилюли (видимая часть без тени)
    inline juce::Rectangle<float> pillPreset()  { return { 785.5f, 19.5f, 137.5f, 41.0f }; }
    inline juce::Rectangle<float> pillQuality() { return { 217.5f, 370.5f, 241.0f, 52.5f }; }
    inline juce::Rectangle<float> pillRange()   { return { 504.3f, 370.5f, 241.0f, 52.5f }; }

    // переключатель
    inline juce::Rectangle<float> toggleTrack() { return { 361.5f, 450.0f, 58.0f, 33.5f }; }
    inline juce::Rectangle<float> toggleZone()  { return { 350.0f, 440.0f, 262.0f, 56.0f }; }

    // значения под ручками: левый край текста и нижняя линия (baseline)
    constexpr float kValueBaseline = 329.5f;
    constexpr float kValueX1 = 222.0f, kValueX2 = 505.0f, kValueX3 = 788.5f;

    // палитра
    inline juce::Colour colTitle()      { return juce::Colour::fromRGB (212, 213, 231); }
    inline juce::Colour colKnobLabel()  { return juce::Colour::fromRGB (197, 198, 215); }
    inline juce::Colour colSmallLabel() { return juce::Colour::fromRGB (194, 194, 207); }
    inline juce::Colour colValue()      { return juce::Colour::fromRGB (167, 170, 190); }
    inline juce::Colour colValueHot()   { return juce::Colour::fromRGB (222, 224, 240); }
    inline juce::Colour colPillText()   { return juce::Colour::fromRGB (205, 207, 225); }
    inline juce::Colour colIcon()       { return juce::Colour::fromRGB (208, 210, 228); }
    inline juce::Colour colChevron()    { return juce::Colour::fromRGB (172, 175, 195); }
    inline juce::Colour colAccent()     { return juce::Colour::fromRGB (138, 106, 255); }
    inline juce::Colour colAccentDark() { return juce::Colour::fromRGB (106, 80, 217); }
}
