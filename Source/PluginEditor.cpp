#include "PluginEditor.h"

#ifndef JucePlugin_VersionString
 #define JucePlugin_VersionString "0.5.0"
#endif

using juce::Colour;
using juce::Point;
using juce::Rectangle;

namespace
{
    Colour rgb (int r, int g, int b) { return Colour::fromRGB ((juce::uint8) r, (juce::uint8) g, (juce::uint8) b); }
    Colour rgba (int r, int g, int b, float a) { return Colour::fromRGB ((juce::uint8) r, (juce::uint8) g, (juce::uint8) b).withAlpha (a); }
    juce::PathStrokeType roundStroke (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); }

    struct EditorLaf : juce::LookAndFeel_V4
    {
        EditorLaf()
        {
            setColour (juce::BubbleComponent::backgroundColourId, rgb (41, 43, 58));
            setColour (juce::BubbleComponent::outlineColourId,    rgb (119, 115, 157));
            setColour (juce::TooltipWindow::textColourId,         rgb (217, 219, 234));
        }
    };
}

//==============================================================================
namespace
{
    // В JUCE 8 функция создания шрифта из памяти имеет разные сигнатуры в разных версиях:
    // (const void*, size_t) или (Span<const std::byte>). Подбираем доступную на этапе компиляции.
    template <typename T = juce::Typeface>
    auto makeTypefaceImpl (const void* d, size_t n, int) -> decltype (T::createSystemTypefaceFor (d, n))
    {
        return T::createSystemTypefaceFor (d, n);
    }

    template <typename T = juce::Typeface>
    auto makeTypefaceImpl (const void* d, size_t n, long)
        -> decltype (T::createSystemTypefaceFor (juce::Span<const std::byte> (static_cast<const std::byte*> (d), n)))
    {
        return T::createSystemTypefaceFor (juce::Span<const std::byte> (static_cast<const std::byte*> (d), n));
    }

    juce::Typeface::Ptr makeTypeface (const char* data, int size)
    {
        return makeTypefaceImpl (static_cast<const void*> (data), (size_t) size, 0);
    }
}

UiRes::UiRes()
{
    regular = makeTypeface (BinaryData::Inter_Regular_otf, BinaryData::Inter_Regular_otfSize);
    medium  = makeTypeface (BinaryData::Inter_Medium_otf,  BinaryData::Inter_Medium_otfSize);
    bg      = juce::ImageCache::getFromMemory (BinaryData::bg_png,    BinaryData::bg_pngSize);
    noise   = juce::ImageCache::getFromMemory (BinaryData::noise_png, BinaryData::noise_pngSize);

    // размеры шрифтов подбираются по ширине эталонных надписей из референса
    hTitle    = fitHeight (true,  "EXPitch",           127.5f);
    hKnob     = fitHeight (true,  "Formanto",          107.05f);
    hLabel    = fitHeight (false, "Output",            56.8f);
    hPreserve = fitHeight (false, "Preserve formants", 155.4f);
    hValue    = fitHeight (false, "100%",              42.5f);
    hPill     = fitHeight (false, "Voice range",       96.5f);
    hPreset   = fitHeight (false, "Default",           55.5f);
}

float UiRes::fitHeight (bool med_, const juce::String& ref, float targetWidth) const
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (juce::Font (juce::FontOptions().withTypeface (med_ ? medium : regular).withHeight (100.0f)), ref, 0.0f, 0.0f);
    const float w = ga.getBoundingBox (0, -1, true).getWidth();
    return w > 1.0f ? 100.0f * targetWidth / w : 16.0f;
}

void drawTextAt (juce::Graphics& g, const juce::Font& f, const juce::String& text, float left, float baseline,
                 Colour c, float alpha)
{
    if (alpha <= 0.003f) return;
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, text, left, baseline);
    g.setColour (c.withMultipliedAlpha (alpha));
    ga.draw (g);
}

//==============================================================================
StaticLayer::StaticLayer (const UiRes& r) : res (r)
{
    setInterceptsMouseClicks (false, false);
    setBufferedToImage (true);
}

void StaticLayer::drawKnobBase (juce::Graphics& g, Point<float> c) const
{
    // тень под ручкой
    {
        juce::Path p; p.addEllipse (c.x - 79.5f, c.y - 79.5f, 159.0f, 159.0f);
        juce::DropShadow (rgba (4, 4, 10, 0.55f), 14, { 0, 4 }).drawForPath (g, p);
    }
    // кольцо-желоб
    g.setGradientFill (juce::ColourGradient (rgb (40, 40, 60), c.x, c.y - 80.0f, rgb (12, 12, 17), c.x, c.y + 80.0f, false));
    g.fillEllipse (c.x - 79.5f, c.y - 79.5f, 159.0f, 159.0f);
    // тёмная внутренняя тень у корпуса
    g.setGradientFill (juce::ColourGradient (rgba (2, 2, 6, 0.30f), c.x, c.y - 80.0f, rgba (2, 2, 6, 0.52f), c.x, c.y + 80.0f, false));
    g.drawEllipse (c.x - 71.0f, c.y - 71.0f, 142.0f, 142.0f, 8.0f);
    // блик на внешней кромке кольца (сверху-слева)
    g.setGradientFill (juce::ColourGradient (rgba (160, 160, 205, 0.60f), c.x - 56.0f, c.y - 56.0f,
                                             rgba (160, 160, 205, 0.04f), c.x + 56.0f, c.y + 56.0f, false));
    g.drawEllipse (c.x - 78.4f, c.y - 78.4f, 156.8f, 156.8f, 1.6f);
}

void StaticLayer::paint (juce::Graphics& g)
{
    const auto bounds = Rectangle<float> (0.0f, 0.0f, dz::W, dz::H);
    g.fillAll (rgb (15, 16, 22));                               // за скруглёнными углами

    juce::Path panel;
    panel.addRoundedRectangle (0.0f, 0.0f, dz::W, dz::H, dz::kPanelRadius);

    g.saveState();
    g.reduceClipRegion (panel, juce::AffineTransform());

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (res.bg, bounds, juce::RectanglePlacement::stretchToFit);

    // дизеринг: убирает полосы на плавных градиентах
    g.setTiledImageFill (res.noise, 0, 0, 0.9f);
    g.fillRect (bounds);

    // мягкая подсветка сверху
    {
        juce::ColourGradient gr (juce::Colours::white.withAlpha (0.045f), 0.0f, 0.0f, juce::Colours::transparentWhite, 0.0f, 90.0f, false);
        gr.addColour (0.5, juce::Colours::white.withAlpha (0.011f));
        g.setGradientFill (gr);
        g.fillRect (0.0f, 0.0f, dz::W, 90.0f);
    }

    // внутреннее свечение кромки и тонкая рамка
    {
        juce::Path glow; glow.addRoundedRectangle (3.0f, 3.0f, dz::W - 6.0f, dz::H - 6.0f, 58.0f);
        g.setColour (rgba (150, 140, 225, 0.10f));
        g.strokePath (glow, juce::PathStrokeType (5.5f));
        juce::Path frame; frame.addRoundedRectangle (0.7f, 0.7f, dz::W - 1.4f, dz::H - 1.4f, 60.3f);
        g.setColour (rgba (155, 150, 210, 0.70f));
        g.strokePath (frame, juce::PathStrokeType (1.4f));
    }

    // основания ручек
    drawKnobBase (g, dz::pivotPitch());
    drawKnobBase (g, dz::pivotFormant());
    drawKnobBase (g, dz::pivotFine());

    // подписи
    drawTextAt (g, res.med (res.hTitle), "EXPitch",  418.3f, 51.7f,  dz::colTitle());
    drawTextAt (g, res.med (res.hKnob),  "Pitch",    171.3f, 122.3f, dz::colKnobLabel());
    drawTextAt (g, res.med (res.hKnob),  "Formanto", 430.2f, 122.3f, dz::colKnobLabel());
    drawTextAt (g, res.med (res.hKnob),  "Fine",     744.9f, 122.3f, dz::colKnobLabel());
    drawTextAt (g, res.reg (res.hLabel), "Mix",      137.9f, 330.6f, dz::colSmallLabel());
    drawTextAt (g, res.reg (res.hLabel), "Mix",      421.2f, 330.6f, dz::colSmallLabel());
    drawTextAt (g, res.reg (res.hLabel), "Output",   685.6f, 330.6f, dz::colSmallLabel());

    // разделители значений
    g.setColour (rgba (190, 192, 215, 0.30f));
    for (float x : { 197.5f, 481.0f, 764.5f })
        g.fillRoundedRectangle (x - 0.55f, 310.0f, 1.1f, 29.5f, 0.55f);

    g.restoreState();
}

//==============================================================================
KnobComponent::KnobComponent (Point<float> pivotInPanel, float introDelay)
    : pivot (pivotInPanel), pos (0.0f, 8.5f, 1.0f), hover (0.0f, 22.0f, 1.0f), press (0.0f, 36.0f, 0.85f), delay (introDelay)
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setMouseDragSensitivity (240);
    setPopupDisplayEnabled (true, true, nullptr);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

bool KnobComponent::hitTest (int x, int y)
{
    const auto c = pivot - getPosition().toFloat();
    const float dx = (float) x - c.x, dy = (float) y - c.y;
    return dx * dx + dy * dy <= 80.0f * 80.0f;
}

void KnobComponent::mouseEnter (const juce::MouseEvent& e) { juce::Slider::mouseEnter (e); hover.target = 1.0f; }
void KnobComponent::mouseExit (const juce::MouseEvent& e)  { juce::Slider::mouseExit (e);  hover.target = 0.0f; }
void KnobComponent::mouseDown (const juce::MouseEvent& e)  { juce::Slider::mouseDown (e);  press.target = 1.0f; }
void KnobComponent::mouseUp (const juce::MouseEvent& e)    { juce::Slider::mouseUp (e);    press.target = 0.0f; }
void KnobComponent::valueChanged() {}

bool KnobComponent::tick (float dt)
{
    introClock += dt;
    if (introClock < delay) return false;

    const float target = (float) valueToProportionOfLength (getValue());
    pos.target = target;
    if (! introDone && std::abs (pos.x - target) < 0.012f)
    {
        introDone = true;                 // после «въезда» стрелки переходим на быстрый отклик
        pos.omega = 34.0f;
        pos.zeta = 0.92f;
    }
    bool changed = pos.step (dt);
    changed |= hover.step (dt);
    changed |= press.step (dt);
    return changed;
}

void KnobComponent::drawArc (juce::Graphics& g, Point<float> c, float endDeg) const
{
    const float start = dz::kArcStart, len = endDeg - start;
    if (len < 0.6f) return;

    const int n = std::max (4, (int) std::ceil (len / 3.0f));
    struct Pass { float width, alpha; };
    const Pass passes[] = { { 18.0f, 0.07f + 0.03f * hover.x }, { 12.0f, 0.12f + 0.04f * hover.x }, { 8.5f, 0.20f + 0.05f * hover.x }, { dz::kArcW, 1.0f } };

    for (const auto& ps : passes)
    {
        const float outer = dz::kArcR + ps.width * 0.5f, inner = dz::kArcR - ps.width * 0.5f;
        const float prop = inner / outer;
        for (int i = 0; i < n; ++i)
        {
            const float t = ((float) i + 0.5f) / (float) n;
            const float ramp = 1.0f - t;                    // плотно в начале -> прозрачно к концу
            const float a0 = start + len * (float) i / (float) n - (i > 0 ? 0.25f : 0.0f);
            const float a1 = start + len * (float) (i + 1) / (float) n;
            juce::Path seg;
            seg.addPieSegment (c.x - outer, c.y - outer, outer * 2.0f, outer * 2.0f,
                               juce::degreesToRadians (a0), juce::degreesToRadians (a1), prop);
            g.setColour (mixColour (rgb (124, 90, 246), rgb (156, 128, 255), t).withAlpha (ps.alpha * ramp));
            g.fillPath (seg);
        }
    }
    // круглый плотный старт дуги
    const float sa = juce::degreesToRadians (start);
    const Point<float> sp (c.x + dz::kArcR * std::sin (sa), c.y - dz::kArcR * std::cos (sa));
    g.setColour (rgb (124, 90, 246));
    g.fillEllipse (sp.x - dz::kArcW * 0.5f, sp.y - dz::kArcW * 0.5f, dz::kArcW, dz::kArcW);
}

void KnobComponent::paint (juce::Graphics& g)
{
    const auto c = pivot - getPosition().toFloat();
    const float ang = (std::max (-0.03f, std::min (1.03f, pos.x)) - 0.5f) * dz::kArcSpan;
    const float h = hover.x, pr = press.x;

    // дуга значения
    drawArc (g, c, ang);

    // корпус: градиент вдоль диагонали, как у референса
    const float faceR = dz::kFaceR * (1.0f - 0.015f * pr);
    {
        const float ux = 0.66f, uy = 0.75f;
        const float rs[] = { -62, -46, -30, -14, 0, 18, 34, 58 };
        const int cols[][3] = { { 90, 90, 122 }, { 71, 71, 96 }, { 60, 61, 81 }, { 50, 52, 66 }, { 41, 43, 56 }, { 36, 36, 48 }, { 30, 30, 40 }, { 23, 23, 31 } };
        const float gain = (1.0f + 0.06f * h) * (1.0f - 0.05f * pr);
        auto col = [&] (int i) { return Colour::fromFloatRGBA (std::min (1.0f, cols[i][0] * gain / 255.0f), std::min (1.0f, cols[i][1] * gain / 255.0f), std::min (1.0f, cols[i][2] * gain / 255.0f), 1.0f); };
        juce::ColourGradient gr (col (0), c.x + ux * rs[0], c.y + uy * rs[0], col (7), c.x + ux * rs[7], c.y + uy * rs[7], false);
        for (int i = 1; i < 7; ++i) gr.addColour ((rs[i] - rs[0]) / (rs[7] - rs[0]), col (i));
        g.setGradientFill (gr);
        g.fillEllipse (c.x - faceR, c.y - faceR, faceR * 2.0f, faceR * 2.0f);

        // мягкий блик на поверхности
        juce::Path face; face.addEllipse (c.x - faceR, c.y - faceR, faceR * 2.0f, faceR * 2.0f);
        g.saveState();
        g.reduceClipRegion (face, juce::AffineTransform());
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.05f + 0.02f * h), c.x - 22.0f, c.y - 30.0f,
                                                 juce::Colours::transparentWhite, c.x - 22.0f + 42.0f, c.y - 30.0f, true));
        g.fillRect (c.x - faceR, c.y - faceR, faceR * 2.0f, faceR * 2.0f);
        g.restoreState();

        // светлая кромка сверху-слева
        g.setGradientFill (juce::ColourGradient (rgba (200, 202, 232, 0.95f + 0.05f * h), c.x - 30.0f, c.y - 50.0f,
                                                 rgba (200, 202, 232, 0.10f), c.x + 40.0f, c.y + 55.0f, false));
        g.drawEllipse (c.x - (faceR - 1.1f), c.y - (faceR - 1.1f), (faceR - 1.1f) * 2.0f, (faceR - 1.1f) * 2.0f, 1.8f);
    }

    // стрелка
    {
        const float a = juce::degreesToRadians (ang);
        const float ux = std::sin (a), uy = -std::cos (a);
        const Point<float> p0 (c.x + 27.0f * ux, c.y + 27.0f * uy), p1 (c.x + 63.5f * ux, c.y + 63.5f * uy);
        juce::Path line; line.startNewSubPath (p0); line.lineTo (p1);
        juce::Path shadow; shadow.startNewSubPath (p0.x, p0.y + 1.0f); shadow.lineTo (p1.x, p1.y + 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.strokePath (shadow, roundStroke (3.8f));
        g.setColour (rgba (235, 235, 255, 0.18f + 0.06f * h));
        g.strokePath (line, roundStroke (7.0f));
        g.setColour (rgb (244, 245, 252));
        g.strokePath (line, roundStroke (3.4f));
    }
}

//==============================================================================
ReadoutField::ReadoutField (juce::RangedAudioParameter& p, std::function<juce::String (float)> fmt,
                            float textLeftInPanel, const UiRes& r)
    : param (p),
      attachment (p, [this] (float v) { value = v; }),
      format (std::move (fmt)),
      res (r),
      textLeft (textLeftInPanel),
      shown (0.0f, 22.0f, 1.0f),
      hot (0.0f, 20.0f, 1.0f)
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    attachment.sendInitialUpdate();
    shown.snap (value);
}

bool ReadoutField::tick (float dt)
{
    shown.target = value;
    hot.target = (hovering || dragging) ? 1.0f : 0.0f;
    bool c = shown.step (dt);
    c |= hot.step (dt);
    return c;
}

void ReadoutField::paint (juce::Graphics& g)
{
    drawTextAt (g, res.reg (res.hValue), format (shown.x),
                textLeft - (float) getX(), dz::kValueBaseline - (float) getY(),
                mixColour (dz::colValue(), dz::colValueHot(), hot.x));
}

void ReadoutField::mouseDown (const juce::MouseEvent&)
{
    dragging = true;
    startNorm = param.convertTo0to1 (value);
    attachment.beginGesture();
}

void ReadoutField::mouseDrag (const juce::MouseEvent& e)
{
    const float sens = e.mods.isShiftDown() ? 800.0f : 200.0f;
    const float n = juce::jlimit (0.0f, 1.0f, startNorm - (float) e.getDistanceFromDragStartY() / sens);
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (n));
}

void ReadoutField::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    attachment.endGesture();
}

void ReadoutField::mouseDoubleClick (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}

void ReadoutField::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float n = juce::jlimit (0.0f, 1.0f, param.convertTo0to1 (value) + w.deltaY * 0.05f);
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (n));
}

//==============================================================================
PillButton::PillButton (juce::RangedAudioParameter& p, Rectangle<float> pillRect, Icon icon,
                        float tLeft, float tBase, float chX, float chY,
                        juce::StringArray items_, juce::StringArray texts, bool presetStyle, const UiRes& r)
    : pillInPanel (pillRect),
      param (p),
      attachment (p, [this] (float v)
                  {
                      const int idx = juce::jlimit (0, pillTexts.size() - 1, juce::roundToInt (v));
                      if (idx != index)
                      {
                          prevText = pillTexts[index];
                          index = idx;
                          textAnim.x = 0.0f; textAnim.v = 0.0f; textAnim.target = 1.0f;
                      }
                  }),
      iconType (icon), textLeft (tLeft), textBaseline (tBase), chevX (chX), chevY (chY),
      menuItems (std::move (items_)), pillTexts (std::move (texts)), preset (presetStyle), res (r),
      hover (0.0f, 22.0f, 1.0f), press (0.0f, 38.0f, 0.9f), open (0.0f, 22.0f, 0.9f), textAnim (1.0f, 24.0f, 1.0f)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    attachment.sendInitialUpdate();
    textAnim.snap (1.0f);
}

bool PillButton::hitTest (int x, int y)
{
    return pillInPanel.translated ((float) -getX(), (float) -getY()).contains ((float) x, (float) y);
}

void PillButton::mouseDown (const juce::MouseEvent&)
{
    pressed = true;
    if (onClick) onClick (this);
}

void PillButton::mouseUp (const juce::MouseEvent&) { pressed = false; }

void PillButton::choose (int idx) { attachment.setValueAsCompleteGesture ((float) idx); }

bool PillButton::tick (float dt)
{
    hover.target = hovering ? 1.0f : 0.0f;
    press.target = pressed ? 1.0f : 0.0f;
    open.target  = isOpen ? 1.0f : 0.0f;
    bool c = hover.step (dt);
    c |= press.step (dt);
    c |= open.step (dt);
    c |= textAnim.step (dt);
    return c;
}

void PillButton::drawIcon (juce::Graphics& g, Point<float> off) const
{
    g.setColour (dz::colIcon());
    if (iconType == Icon::wave)
    {
        const Point<float> P[] = { { 246.7f, 398.7f }, { 253.3f, 398.7f }, { 258.75f, 388.3f }, { 262.9f, 405.8f }, { 268.3f, 394.6f }, { 273.75f, 398.4f } };
        const int np = 6;
        juce::Path path;
        path.startNewSubPath (P[0] + off);
        for (int i = 0; i < np - 1; ++i)
        {
            const Point<float> p0 = P[std::max (0, i - 1)], p1 = P[i], p2 = P[i + 1], p3 = P[std::min (np - 1, i + 2)];
            for (int k = 1; k <= 14; ++k)
            {
                const float t = (float) k / 14.0f, t2 = t * t, t3 = t2 * t;
                const float x = 0.5f * ((2 * p1.x) + (-p0.x + p2.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3);
                const float y = 0.5f * ((2 * p1.y) + (-p0.y + p2.y) * t + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 + (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3);
                path.lineTo (x + off.x, y + off.y);
            }
        }
        g.strokePath (path, roundStroke (2.3f));
    }
    else if (iconType == Icon::face)
    {
        const float cx = 543.75f + off.x, cy = 397.0f + off.y;
        for (float s : { -1.0f, 1.0f })                                   // уши
            g.drawEllipse (cx + s * 9.6f - 2.2f, cy + 0.5f - 2.9f, 4.4f, 5.8f, 1.8f);
        g.drawEllipse (cx - 8.7f, cy + 0.8f - 9.6f, 17.4f, 19.2f, 1.9f);   // голова
        g.setColour (rgb (16, 17, 24));
        g.fillRoundedRectangle (cx - 6.6f, cy - 3.6f, 13.2f, 11.2f, 4.5f); // лицо
        g.setColour (dz::colIcon());
        for (float s : { -1.0f, 1.0f }) g.fillEllipse (cx + s * 3.3f - 1.05f, cy - 0.6f - 1.05f, 2.1f, 2.1f);
        g.fillRoundedRectangle (cx - 2.3f, cy + 4.1f, 4.6f, 1.9f, 0.95f);
        juce::Path hair;
        hair.addCentredArc (cx, cy - 1.0f, 7.0f, 4.2f, 0.0f, -juce::MathConstants<float>::pi * 0.55f, juce::MathConstants<float>::pi * 0.55f, true);
        g.setColour (dz::colIcon().withAlpha (0.9f));
        g.strokePath (hair, roundStroke (1.6f));
    }
}

void PillButton::paint (juce::Graphics& g)
{
    const auto off = Point<float> ((float) -getX(), (float) -getY());
    const auto local = pillInPanel.translated (off.x, off.y);

    if (shadowImg.isNull() || shadowImg.getWidth() != getWidth() || shadowImg.getHeight() != getHeight())
    {
        shadowImg = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
        juce::Graphics gi (shadowImg);
        juce::Path p; p.addRoundedRectangle (local, local.getHeight() * 0.5f);
        juce::DropShadow (rgba (3, 3, 8, 0.38f), 10, { 0, 3 }).drawForPath (gi, p);
    }
    g.drawImageAt (shadowImg, 0, 0);

    const float h = hover.x, o = open.x, pr = press.x;
    const float sc = 1.0f - 0.012f * pr;
    const auto r = local.withSizeKeepingCentre (local.getWidth() * sc, local.getHeight() * sc);
    const int lift = (int) (8.0f * (h + 0.6f * o));

    g.setGradientFill (juce::ColourGradient (rgb (36 + lift, 38 + lift, 54 + lift), 0.0f, r.getY(),
                                             rgb (27 + lift, 29 + lift, 42 + lift), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);

    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.20f + 0.10f * h + 0.05f * o), 0.0f, r.getY(),
                                             juce::Colours::white.withAlpha (0.06f + 0.10f * h + 0.05f * o), 0.0f, r.getBottom(), false));
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f - 0.5f, 1.0f);

    drawIcon (g, off);

    // подпись: при смене режима старый текст уезжает вверх и гаснет, новый выезжает снизу
    juce::Font font = preset ? res.reg (res.hPreset) : res.reg (res.hPill);
    auto fitted = [&] (const juce::String& s)
    {
        if (! preset) return font;
        juce::GlyphArrangement ga; ga.addLineOfText (font, s, 0.0f, 0.0f);
        const float w = ga.getBoundingBox (0, -1, true).getWidth();
        return w > 72.0f ? font.withHeight (font.getHeight() * 72.0f / w) : font;
    };
    const Colour tc = preset ? rgb (208, 210, 226) : dz::colPillText();
    const float tx = textLeft + off.x, ty = textBaseline + off.y;
    const float ta = clamp01 (textAnim.x);
    if (ta < 0.999f && prevText.isNotEmpty())
        drawTextAt (g, fitted (prevText), prevText, tx, ty - 6.0f * ta, tc, 1.0f - ta);
    drawTextAt (g, fitted (pillTexts[index]), pillTexts[index], tx, ty + 6.0f * (1.0f - ta), tc, ta);

    // шеврон: переворачивается при открытом меню
    {
        const float cx = chevX + off.x, cy = chevY + off.y;
        juce::Path ch;
        ch.startNewSubPath (cx - 6.0f, cy - 2.9f); ch.lineTo (cx, cy + 2.9f); ch.lineTo (cx + 6.0f, cy - 2.9f);
        ch.applyTransform (juce::AffineTransform::rotation (o * juce::MathConstants<float>::pi, cx, cy));
        g.setColour (mixColour (dz::colChevron(), dz::colIcon(), h));
        g.strokePath (ch, roundStroke (1.9f));
    }
}

//==============================================================================
ToggleSwitch::ToggleSwitch (juce::RangedAudioParameter& p, const UiRes& r)
    : param (p),
      attachment (p, [this] (float v) { on = v > 0.5f; t.target = on ? 1.0f : 0.0f; }),
      res (r),
      t (1.0f, 24.0f, 0.62f), press (0.0f, 40.0f, 0.9f), hover (0.0f, 20.0f, 1.0f)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    attachment.sendInitialUpdate();
    t.snap (on ? 1.0f : 0.0f);
}

void ToggleSwitch::mouseDown (const juce::MouseEvent&) { pressed = true; }
void ToggleSwitch::mouseUp (const juce::MouseEvent& e)
{
    pressed = false;
    if (contains (e.getPosition()))
        attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

bool ToggleSwitch::tick (float dt)
{
    hover.target = hovering ? 1.0f : 0.0f;
    press.target = pressed ? 1.0f : 0.0f;
    bool c = t.step (dt);
    c |= press.step (dt);
    c |= hover.step (dt);
    return c;
}

void ToggleSwitch::paint (juce::Graphics& g)
{
    const auto off = Point<float> ((float) -getX(), (float) -getY());
    const auto tr = dz::toggleTrack().translated (off.x, off.y);
    const float tv = std::max (-0.04f, std::min (1.04f, t.x)), tc = clamp01 (t.x), pr = press.x, hv = hover.x;

    // свечение трека при включении
    if (tc > 0.01f)
    {
        juce::Image glow (juce::Image::ARGB, getWidth(), getHeight(), true);
        {
            juce::Graphics gi (glow);
            juce::Path p; p.addRoundedRectangle (tr, tr.getHeight() * 0.5f);
            juce::DropShadow (dz::colAccent().withAlpha (0.95f), 14, { 0, 0 }).drawForPath (gi, p);
        }
        g.setOpacity (0.55f * tc);
        g.drawImageAt (glow, 0, 0);
    }

    // трек
    g.setGradientFill (juce::ColourGradient (mixColour (rgb (66, 68, 88), rgb (146, 116, 255), tc), 0.0f, tr.getY(),
                                             mixColour (rgb (52, 54, 72), rgb (112, 82, 236), tc), 0.0f, tr.getBottom(), false));
    g.fillRoundedRectangle (tr, tr.getHeight() * 0.5f);
    g.setColour (mixColour (rgb (122, 124, 152), rgb (190, 170, 255), tc).withAlpha (0.65f + 0.15f * hv));
    g.drawRoundedRectangle (tr.reduced (0.7f), tr.getHeight() * 0.5f - 0.7f, 1.4f);

    // ручка: пружинный ход + «сжатие» при нажатии, как у iOS
    const float d = 25.0f, wk = d + 4.0f * pr;
    const float cxk = (tr.getX() + 17.0f) + ((tr.getRight() - 17.0f) - (tr.getX() + 17.0f)) * tv + 2.0f * pr * (tc - 0.5f) * 2.0f;
    const float cyk = tr.getCentreY();
    const Rectangle<float> kr (cxk - wk * 0.5f, cyk - d * 0.5f, wk, d);
    {
        juce::Path kp; kp.addRoundedRectangle (kr, d * 0.5f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.38f), 4, { 0, 2 }).drawForPath (g, kp);
    }
    g.setGradientFill (juce::ColourGradient (mixColour (rgb (214, 215, 234), rgb (255, 255, 255), tc), 0.0f, kr.getY(),
                                             mixColour (rgb (180, 181, 204), rgb (232, 232, 248), tc), 0.0f, kr.getBottom(), false));
    g.fillRoundedRectangle (kr, d * 0.5f);

    drawTextAt (g, res.reg (res.hPreserve), "Preserve formants", 439.2f + off.x, 473.2f + off.y,
                mixColour (rgb (160, 163, 182), rgb (214, 216, 232), tc));
}

//==============================================================================
DropdownOverlay::DropdownOverlay (const UiRes& r) : res (r), open (0.0f, 24.0f, 0.82f), highlightY (0.0f, 30.0f, 1.0f), highlightA (0.0f, 26.0f, 1.0f)
{
    setVisible (false);
}

Rectangle<float> DropdownOverlay::panelRect() const { return panel; }

Rectangle<float> DropdownOverlay::itemRect (int i) const
{
    return { panel.getX() + 6.0f, panel.getY() + 6.0f + 38.0f * (float) i, panel.getWidth() - 12.0f, 38.0f };
}

void DropdownOverlay::openFor (PillButton* p)
{
    anchor = p;
    items = p->items();
    selected = p->getIndex();
    hoverIndex = -1;

    const auto pill = p->pillInPanel;
    const float width = std::max (pill.getWidth(), 270.0f);
    const float height = 38.0f * (float) items.size() + 12.0f;
    upward = pill.getCentreY() > dz::H * 0.5f;
    float x = (pill.getRight() > dz::W - 60.0f) ? pill.getRight() - width : pill.getX();
    x = juce::jlimit (8.0f, dz::W - width - 8.0f, x);
    const float y = upward ? pill.getY() - 8.0f - height : pill.getBottom() + 8.0f;
    panel = { x, y, width, height };

    highlightY.snap (itemRect (selected).getY());
    highlightA.snap (0.0f);
    setVisible (true);
    toFront (false);
    open.target = 1.0f;
}

void DropdownOverlay::closeMenu() { open.target = 0.0f; }

bool DropdownOverlay::tick (float dt)
{
    if (! isVisible()) return false;
    bool c = open.step (dt);
    c |= highlightY.step (dt);
    c |= highlightA.step (dt);
    if (open.target == 0.0f && open.x <= 0.002f)
    {
        open.snap (0.0f);
        setVisible (false);
        anchor = nullptr;
        if (onClosed) onClosed();
        c = true;
    }
    return c;
}

void DropdownOverlay::mouseMove (const juce::MouseEvent& e)
{
    int idx = -1;
    for (int i = 0; i < items.size(); ++i)
        if (itemRect (i).contains (e.position)) idx = i;
    hoverIndex = idx;
    highlightA.target = idx >= 0 ? 1.0f : 0.0f;
    if (idx >= 0)
    {
        if (highlightA.x < 0.05f) highlightY.snap (itemRect (idx).getY());
        highlightY.target = itemRect (idx).getY();
    }
}

void DropdownOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (open.target < 0.5f) return;
    for (int i = 0; i < items.size(); ++i)
        if (itemRect (i).contains (e.position))
        {
            if (anchor != nullptr) anchor->choose (i);
            closeMenu();
            return;
        }
    closeMenu();                                  // клик вне меню закрывает его
}

void DropdownOverlay::paint (juce::Graphics& g)
{
    const float o = open.x;
    if (o < 0.003f) return;

    const float s = 0.94f + 0.06f * std::min (1.05f, o);
    const float px = panel.getCentreX(), py = upward ? panel.getBottom() : panel.getY();
    g.beginTransparencyLayer (clamp01 (o));
    g.addTransform (juce::AffineTransform::translation (0.0f, (1.0f - o) * (upward ? 8.0f : -8.0f)));
    g.addTransform (juce::AffineTransform::scale (s, s, px, py));

    juce::Path pp; pp.addRoundedRectangle (panel, 20.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 26, { 0, 10 }).drawForPath (g, pp);
    g.setGradientFill (juce::ColourGradient (rgba (40, 42, 60, 0.985f), 0.0f, panel.getY(), rgba (30, 32, 47, 0.985f), 0.0f, panel.getBottom(), false));
    g.fillPath (pp);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (panel.reduced (0.5f), 19.5f, 1.0f);

    // подсветка наведённого пункта (плавно перетекает между пунктами)
    if (highlightA.x > 0.01f)
    {
        g.setColour (dz::colAccent().withAlpha (0.24f * clamp01 (highlightA.x)));
        g.fillRoundedRectangle (panel.getX() + 6.0f, highlightY.x + 2.0f, panel.getWidth() - 12.0f, 34.0f, 14.0f);
    }

    const auto font = res.reg (res.hPill);
    for (int i = 0; i < items.size(); ++i)
    {
        const auto r = itemRect (i);
        const bool hot = (i == hoverIndex), sel = (i == selected);
        const Colour col = sel ? rgb (244, 244, 252) : (hot ? rgb (232, 233, 246) : rgb (190, 192, 212));
        drawTextAt (g, font, items[i], r.getX() + 16.0f, r.getCentreY() + 6.8f, col);
        if (sel)
        {
            const float cx = r.getRight() - 24.0f, cy = r.getCentreY();
            juce::Path ck; ck.startNewSubPath (cx - 6.0f, cy + 0.5f); ck.lineTo (cx - 2.0f, cy + 4.5f); ck.lineTo (cx + 6.0f, cy - 4.5f);
            g.setColour (dz::colAccent());
            g.strokePath (ck, roundStroke (2.2f));
        }
    }
    g.endTransparencyLayer();
}

//==============================================================================
namespace
{
    const char* const kTelegramUrl  = "https://t.me/mattexeee";
    const char* const kTelegramText = "t.me/mattexeee";
    constexpr float kDotX = 52.5f, kDotY = 37.5f, kDotR = 12.2f;
}

AboutButton::AboutButton() : hover (0.0f, 22.0f, 1.0f), press (0.0f, 40.0f, 0.9f), open (0.0f, 22.0f, 0.9f)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

bool AboutButton::hitTest (int x, int y)
{
    const float dx = (float) (x + getX()) - kDotX, dy = (float) (y + getY()) - kDotY;
    return dx * dx + dy * dy <= 17.0f * 17.0f;
}

bool AboutButton::tick (float dt)
{
    hover.target = hovering ? 1.0f : 0.0f;
    press.target = pressed ? 1.0f : 0.0f;
    open.target  = isOpen ? 1.0f : 0.0f;
    bool c = hover.step (dt);
    c |= press.step (dt);
    c |= open.step (dt);
    return c;
}

void AboutButton::paint (juce::Graphics& g)
{
    const float cx = kDotX - (float) getX(), cy = kDotY - (float) getY();
    const float h = hover.x, pr = press.x, o = open.x;
    const float r = kDotR * (1.0f + 0.07f * h - 0.08f * pr);

    if (o > 0.01f)                                   // свечение, пока окно открыто
    {
        for (int i = 0; i < 3; ++i)
        {
            const float rr = r + 3.0f + 3.0f * (float) i;
            g.setColour (dz::colAccent().withAlpha (0.20f * o / (float) (i + 1)));
            g.fillEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f);
        }
    }
    const Colour top = mixColour (rgb (126, 124, 170), rgb (158, 150, 235), 0.55f * h + 0.8f * o);
    const Colour bot = mixColour (rgb (96, 93, 145),  rgb (116, 100, 215), 0.55f * h + 0.8f * o);
    g.setGradientFill (juce::ColourGradient (top.withAlpha (0.95f), cx, cy - r, bot.withAlpha (0.95f), cx, cy + r, false));
    g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);

    // значок «i» проявляется при наведении
    const float a = clamp01 (h + o);
    if (a > 0.01f)
    {
        g.setColour (rgba (24, 24, 40, 0.80f * a));
        g.fillEllipse (cx - 1.35f, cy - 5.6f, 2.7f, 2.7f);
        g.fillRoundedRectangle (cx - 1.3f, cy - 1.6f, 2.6f, 7.2f, 1.2f);
    }
}

//==============================================================================
AboutOverlay::AboutOverlay (const UiRes& r) : res (r), open (0.0f, 24.0f, 0.82f), highlightY (0.0f, 30.0f, 1.0f), highlightA (0.0f, 26.0f, 1.0f)
{
    panel = { 22.0f, 62.0f, 350.0f, 206.0f };
    setVisible (false);
}

Rectangle<float> AboutOverlay::rowRect (int i) const
{
    return { panel.getX() + 8.0f, panel.getY() + 112.0f + 40.0f * (float) i, panel.getWidth() - 16.0f, 40.0f };
}

void AboutOverlay::openMenu()
{
    hoverRow = -1;
    copiedClock = 0.0f;
    highlightA.snap (0.0f);
    setVisible (true);
    toFront (false);
    open.target = 1.0f;
}

bool AboutOverlay::tick (float dt)
{
    if (! isVisible()) return false;
    bool c = open.step (dt);
    c |= highlightY.step (dt);
    c |= highlightA.step (dt);
    if (copiedClock > 0.0f) { copiedClock -= dt; c = true; }
    if (open.target == 0.0f && open.x <= 0.002f)
    {
        open.snap (0.0f);
        setVisible (false);
        c = true;
    }
    return c;
}

void AboutOverlay::mouseMove (const juce::MouseEvent& e)
{
    int idx = -1;
    for (int i = 0; i < 2; ++i)
        if (rowRect (i).contains (e.position)) idx = i;
    hoverRow = idx;
    highlightA.target = idx >= 0 ? 1.0f : 0.0f;
    if (idx >= 0)
    {
        if (highlightA.x < 0.05f) highlightY.snap (rowRect (idx).getY());
        highlightY.target = rowRect (idx).getY();
    }
}

void AboutOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (! isOpen()) return;
    if (rowRect (0).contains (e.position))
    {
        juce::URL (kTelegramUrl).launchInDefaultBrowser();
        closeMenu();
    }
    else if (rowRect (1).contains (e.position))
    {
        juce::SystemClipboard::copyTextToClipboard (kTelegramUrl);
        copiedClock = 1.4f;                          // «Copied» на короткое время
    }
    else
        closeMenu();                                 // клик вне окна закрывает его
}

void AboutOverlay::paint (juce::Graphics& g)
{
    const float o = open.x;
    if (o < 0.003f) return;

    const float s = 0.92f + 0.08f * std::min (1.05f, o);
    g.beginTransparencyLayer (clamp01 (o));
    g.addTransform (juce::AffineTransform::translation (0.0f, (1.0f - o) * -8.0f));
    g.addTransform (juce::AffineTransform::scale (s, s, kDotX, kDotY + 14.0f));        // вырастает из кнопки

    juce::Path pp; pp.addRoundedRectangle (panel, 22.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 26, { 0, 10 }).drawForPath (g, pp);
    g.setGradientFill (juce::ColourGradient (rgba (40, 42, 60, 0.985f), 0.0f, panel.getY(), rgba (30, 32, 47, 0.985f), 0.0f, panel.getBottom(), false));
    g.fillPath (pp);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (panel.reduced (0.5f), 21.5f, 1.0f);

    const float x0 = panel.getX() + 22.0f;
    drawTextAt (g, res.med (res.hKnob * 0.92f), "EXPitch", x0, panel.getY() + 40.0f, dz::colTitle());
    drawTextAt (g, res.reg (res.hLabel), juce::String ("Version ") + JucePlugin_VersionString, x0, panel.getY() + 64.0f, rgb (150, 153, 175));

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRect (panel.getX() + 22.0f, panel.getY() + 80.0f, panel.getWidth() - 44.0f, 1.0f);
    drawTextAt (g, res.reg (res.hValue), "CONTACT", x0, panel.getY() + 104.0f, rgb (130, 133, 158));

    if (highlightA.x > 0.01f)
    {
        g.setColour (dz::colAccent().withAlpha (0.22f * clamp01 (highlightA.x)));
        g.fillRoundedRectangle (panel.getX() + 8.0f, highlightY.x + 2.0f, panel.getWidth() - 16.0f, 36.0f, 14.0f);
    }

    const auto font = res.reg (res.hPill);
    const auto r0 = rowRect (0), r1 = rowRect (1);
    drawTextAt (g, font, "Telegram channel", r0.getX() + 14.0f, r0.getCentreY() + 6.8f, hoverRow == 0 ? rgb (244, 244, 252) : rgb (214, 216, 232));
    drawTextAt (g, res.reg (res.hLabel), kTelegramText, r0.getRight() - 168.0f, r0.getCentreY() + 6.2f, rgb (176, 160, 255));
    {   // стрелка «открыть ссылку»
        const float ax = r0.getRight() - 24.0f, ay = r0.getCentreY();
        juce::Path ar; ar.startNewSubPath (ax - 4.0f, ay + 4.0f); ar.lineTo (ax + 4.0f, ay - 4.0f);
        ar.startNewSubPath (ax - 2.0f, ay - 4.0f); ar.lineTo (ax + 4.0f, ay - 4.0f); ar.lineTo (ax + 4.0f, ay + 2.0f);
        g.setColour (rgb (176, 160, 255));
        g.strokePath (ar, roundStroke (1.8f));
    }
    const bool copied = copiedClock > 0.0f;
    drawTextAt (g, font, copied ? "Link copied" : "Copy link", r1.getX() + 14.0f, r1.getCentreY() + 6.8f,
                copied ? dz::colAccent() : (hoverRow == 1 ? rgb (244, 244, 252) : rgb (214, 216, 232)));
    g.endTransparencyLayer();
}

//==============================================================================
PitcherAudioProcessorEditor::PitcherAudioProcessorEditor (PitcherAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    static EditorLaf laf;
    setLookAndFeel (&laf);

    addAndMakeVisible (staticLayer);
    staticLayer.setBounds (0, 0, 960, 512);

    for (auto* k : { &pitchKnob, &formantKnob, &fineKnob })
    {
        addAndMakeVisible (*k);
        k->setBounds (Rectangle<float> (k->pivot.x - 100.0f, k->pivot.y - 100.0f, 200.0f, 200.0f).toNearestInt());
    }
    pitchKnob.setTextValueSuffix (" st");   pitchKnob.setNumDecimalPlacesToDisplay (2);
    formantKnob.setTextValueSuffix (" st"); formantKnob.setNumDecimalPlacesToDisplay (2);
    fineKnob.setTextValueSuffix (" ct");    fineKnob.setNumDecimalPlacesToDisplay (1);
    pitchAtt   = std::make_unique<SliderAtt> (proc.apvts, ids::pitch,   pitchKnob);
    formantAtt = std::make_unique<SliderAtt> (proc.apvts, ids::formant, formantKnob);
    fineAtt    = std::make_unique<SliderAtt> (proc.apvts, ids::fine,    fineKnob);

    auto percent = [] (float v) { return juce::String (juce::roundToInt (v)) + "%"; };
    auto decibel = [] (float v) { return (std::abs (v) < 0.05f ? juce::String ("0.0") : juce::String (v, 1)) + " dB"; };
    pitchMix    = std::make_unique<ReadoutField> (*proc.apvts.getParameter (ids::mix),    percent, dz::kValueX1, res);
    formantMix  = std::make_unique<ReadoutField> (*proc.apvts.getParameter (ids::mix),    percent, dz::kValueX2, res);
    outputField = std::make_unique<ReadoutField> (*proc.apvts.getParameter (ids::output), decibel, dz::kValueX3, res);
    for (auto* r : { pitchMix.get(), formantMix.get(), outputField.get() })
    {
        addAndMakeVisible (*r);
        r->setBounds (juce::roundToInt (r == pitchMix.get() ? dz::kValueX1 : (r == formantMix.get() ? dz::kValueX2 : dz::kValueX3)) - 8,
                      juce::roundToInt (dz::kValueBaseline) - 24, 100, 34);
    }

    juce::StringArray voiceNames, voiceShort;
    for (int i = 0; i < pitcher::kNumVoices; ++i)
    {
        juce::String n (pitcher::voiceDef (i).name);
        voiceNames.add (n);
        voiceShort.add (n.replace (" voice", ""));
    }
    presetPill  = std::make_unique<PillButton> (*proc.apvts.getParameter (ids::voice), dz::pillPreset(), PillButton::Icon::none,
                                                807.5f, 46.0f, 901.0f, 40.25f, voiceNames, voiceShort, true, res);
    qualityPill = std::make_unique<PillButton> (*proc.apvts.getParameter (ids::quality), dz::pillQuality(), PillButton::Icon::wave,
                                                293.5f, 404.0f, 427.5f, 397.75f,
                                                juce::StringArray { "Live - low latency", "Smooth 2048", "Smooth 4096 - max quality" },
                                                juce::StringArray { "Live", "Smooth 2048", "Smooth 4096" }, false, res);
    rangePill   = std::make_unique<PillButton> (*proc.apvts.getParameter (ids::range), dz::pillRange(), PillButton::Icon::face,
                                                574.5f, 404.0f, 711.0f, 397.75f,
                                                juce::StringArray { "Low voice - bass / male", "Mid voice - typical", "High voice - female / child" },
                                                juce::StringArray { "Low voice", "Mid voice", "High voice" }, false, res);
    for (auto* pb : { presetPill.get(), qualityPill.get(), rangePill.get() })
    {
        addAndMakeVisible (*pb);
        pb->setBounds (pb->pillInPanel.expanded (16.0f).toNearestInt());
        pb->onClick = [this] (PillButton* b) { pillClicked (b); };
    }

    preserveToggle = std::make_unique<ToggleSwitch> (*proc.apvts.getParameter (ids::preserve), res);
    addAndMakeVisible (*preserveToggle);
    preserveToggle->setBounds (dz::toggleZone().toNearestInt());

    addAndMakeVisible (aboutButton);
    aboutButton.setBounds (Rectangle<float> (kDotX - 20.0f, kDotY - 20.0f, 40.0f, 40.0f).toNearestInt());
    aboutButton.onClick = [this] { if (about.isOpen()) about.closeMenu(); else about.openMenu(); };

    addChildComponent (dropdown);
    dropdown.setBounds (0, 0, 960, 512);
    addChildComponent (about);
    about.setBounds (0, 0, 960, 512);

    setResizable (false, false);
    setSize (960, 512);

    startTime = lastTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
}

PitcherAudioProcessorEditor::~PitcherAudioProcessorEditor()
{
    pitchAtt.reset(); formantAtt.reset(); fineAtt.reset();
    setLookAndFeel (nullptr);
}

void PitcherAudioProcessorEditor::paint (juce::Graphics& g) { g.fillAll (rgb (15, 16, 22)); }

void PitcherAudioProcessorEditor::pillClicked (PillButton* p)
{
    if (dropdown.isOpenFor (p)) dropdown.closeMenu();
    else                        dropdown.openFor (p);
}

void PitcherAudioProcessorEditor::tick()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = (float) std::min (0.1, std::max (0.0, now - lastTime));
    lastTime = now;
    const float t = (float) (now - startTime);

    // плавное появление элементов при открытии окна
    auto fade = [t] (juce::Component& c, float delay)
    {
        const float a = easeOutCubic ((t - delay) / 0.55f);
        if (std::abs (c.getAlpha() - a) > 0.002f) c.setAlpha (a);
    };
    if (t < 1.4f)
    {
        fade (staticLayer, 0.0f);
        fade (pitchKnob, 0.0f); fade (formantKnob, 0.06f); fade (fineKnob, 0.12f);
        fade (*pitchMix, 0.14f); fade (*formantMix, 0.2f); fade (*outputField, 0.26f);
        fade (*qualityPill, 0.20f); fade (*rangePill, 0.26f); fade (*presetPill, 0.10f);
        fade (*preserveToggle, 0.32f);
        fade (aboutButton, 0.04f);
    }

    if (pitchKnob.tick (dt))   pitchKnob.repaint();
    if (formantKnob.tick (dt)) formantKnob.repaint();
    if (fineKnob.tick (dt))    fineKnob.repaint();
    if (pitchMix->tick (dt))    pitchMix->repaint();
    if (formantMix->tick (dt))  formantMix->repaint();
    if (outputField->tick (dt)) outputField->repaint();

    for (auto* p : { presetPill.get(), qualityPill.get(), rangePill.get() })
    {
        p->setOpenState (dropdown.isOpenFor (p));
        if (p->tick (dt)) p->repaint();
    }
    if (preserveToggle->tick (dt)) preserveToggle->repaint();
    if (dropdown.tick (dt)) dropdown.repaint();
    aboutButton.setOpenState (about.isOpen());
    if (aboutButton.tick (dt)) aboutButton.repaint();
    if (about.tick (dt)) about.repaint();
}
