#pragma once

#include "PluginProcessor.h"
#include "DesignSpec.h"
#include "Anim.h"
#include "BinaryData.h"

//==============================================================================
//  EXPitch UI: векторный интерфейс 960x512 в стиле Apple (фиолетовая палитра).
//  Все детали рисуются кодом (чёткие на любом масштабе), все движения - на пружинах.
//==============================================================================

// Общие ресурсы: шрифты Inter (встроены в плагин) и размеры, подогнанные под референс
struct UiRes
{
    UiRes();
    juce::Font reg (float h) const { return juce::Font (juce::FontOptions().withTypeface (regular).withHeight (h)); }
    juce::Font med (float h) const { return juce::Font (juce::FontOptions().withTypeface (medium).withHeight (h)); }
    float fitHeight (bool medium_, const juce::String& ref, float targetWidth) const;

    juce::Typeface::Ptr regular, medium;
    juce::Image bg, noise;
    float hTitle = 30, hKnob = 24, hLabel = 19, hPreserve = 20, hValue = 17, hPill = 19, hPreset = 17;
};

void drawTextAt (juce::Graphics&, const juce::Font&, const juce::String&, float left, float baseline,
                 juce::Colour, float alpha = 1.0f);

//------------------------------------------------------------------------------
// Неподвижный слой: фон, рамка, подписи, основания ручек. Кэшируется в картинку.
class StaticLayer : public juce::Component
{
public:
    explicit StaticLayer (const UiRes&);
    void paint (juce::Graphics&) override;
private:
    void drawKnobBase (juce::Graphics&, juce::Point<float> c) const;
    const UiRes& res;
};

//------------------------------------------------------------------------------
// Ручка: корпус, плавающая стрелка и дуга с затуханием; пружинное следование за значением
class KnobComponent : public juce::Slider
{
public:
    KnobComponent (juce::Point<float> pivotInPanel, float introDelay);
    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void valueChanged() override;

    bool tick (float dt);
    void introStart (float seconds) { introClock = -seconds; }

    juce::Point<float> pivot;
private:
    void drawArc (juce::Graphics&, juce::Point<float> c, float endDeg) const;

    Spring pos, hover, press;
    float introClock = 0.0f, delay = 0.0f;
    bool introDone = false;
};

//------------------------------------------------------------------------------
// Числовое значение под ручкой (Mix / Output): тянется мышью, число плавно "перекатывается"
class ReadoutField : public juce::Component
{
public:
    ReadoutField (juce::RangedAudioParameter&, std::function<juce::String (float)> fmt,
                  float textLeftInPanel, const UiRes&);
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; }
    void mouseExit (const juce::MouseEvent&) override  { hovering = false; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool tick (float dt);
private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    std::function<juce::String (float)> format;
    const UiRes& res;
    float textLeft;
    float value = 0.0f, startNorm = 0.0f;
    Spring shown, hot;
    bool hovering = false, dragging = false;
};

//------------------------------------------------------------------------------
// Пилюля-выбор (Quality / Voice range / Default): подпись меняется по выбранному режиму
class PillButton : public juce::Component
{
public:
    enum class Icon { none, wave, face };
    PillButton (juce::RangedAudioParameter&, juce::Rectangle<float> pillRect, Icon icon,
                float textLeft, float textBaseline, float chevronX, float chevronY,
                juce::StringArray menuItems, juce::StringArray pillTexts, bool presetStyle, const UiRes&);
    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; }
    void mouseExit (const juce::MouseEvent&) override  { hovering = false; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool tick (float dt);

    void setOpenState (bool o) { isOpen = o; }
    void choose (int index);
    int getIndex() const { return index; }
    const juce::StringArray& items() const { return menuItems; }
    juce::Rectangle<float> pillInPanel;                 // видимая часть в координатах панели
    std::function<void (PillButton*)> onClick;
private:
    void drawIcon (juce::Graphics&, juce::Point<float> origin) const;

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    Icon iconType;
    float textLeft, textBaseline, chevX, chevY;
    juce::StringArray menuItems, pillTexts;
    bool preset;
    const UiRes& res;
    int index = 0;
    juce::String prevText;
    Spring hover, press, open, textAnim;
    bool hovering = false, pressed = false, isOpen = false;
    juce::Image shadowImg;
};

//------------------------------------------------------------------------------
// Переключатель Preserve formants: пружинная ручка, цвет и свечение трека
class ToggleSwitch : public juce::Component
{
public:
    ToggleSwitch (juce::RangedAudioParameter&, const UiRes&);
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; }
    void mouseExit (const juce::MouseEvent&) override  { hovering = false; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool tick (float dt);
private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    const UiRes& res;
    bool on = true, hovering = false, pressed = false;
    Spring t, press, hover;
};

//------------------------------------------------------------------------------
// Выпадающее меню в стиле Apple: появляется с лёгким масштабированием и прозрачностью
class DropdownOverlay : public juce::Component
{
public:
    explicit DropdownOverlay (const UiRes&);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverIndex = -1; }
    void mouseDown (const juce::MouseEvent&) override;

    void openFor (PillButton*);
    void closeMenu();
    bool isOpenFor (PillButton* p) const { return anchor == p && open.target > 0.5f; }
    bool tick (float dt);

    std::function<void()> onClosed;
private:
    juce::Rectangle<float> panelRect() const;
    juce::Rectangle<float> itemRect (int i) const;

    const UiRes& res;
    PillButton* anchor = nullptr;
    juce::StringArray items;
    int selected = 0, hoverIndex = -1;
    bool upward = false;
    Spring open, highlightY, highlightA;
    juce::Rectangle<float> panel;
};

//------------------------------------------------------------------------------
// Кнопка-кружок слева сверху: открывает окно «О плагине»
class AboutButton : public juce::Component
{
public:
    AboutButton();
    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; }
    void mouseExit (const juce::MouseEvent&) override  { hovering = false; }
    void mouseDown (const juce::MouseEvent&) override  { pressed = true; if (onClick) onClick(); }
    void mouseUp (const juce::MouseEvent&) override    { pressed = false; }
    bool tick (float dt);
    void setOpenState (bool o) { isOpen = o; }

    std::function<void()> onClick;
private:
    Spring hover, press, open;
    bool hovering = false, pressed = false, isOpen = false;
};

//------------------------------------------------------------------------------
// Окно «О плагине»: версия, контакты, ссылка на Telegram-канал
class AboutOverlay : public juce::Component
{
public:
    explicit AboutOverlay (const UiRes&);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverRow = -1; highlightA.target = 0.0f; }
    void mouseDown (const juce::MouseEvent&) override;

    void openMenu();
    void closeMenu() { open.target = 0.0f; }
    bool isOpen() const { return open.target > 0.5f; }
    bool tick (float dt);

private:
    juce::Rectangle<float> rowRect (int i) const;

    const UiRes& res;
    juce::Rectangle<float> panel;
    Spring open, highlightY, highlightA;
    int hoverRow = -1;
    float copiedClock = 0.0f;
};

//------------------------------------------------------------------------------
class PitcherAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit PitcherAudioProcessorEditor (PitcherAudioProcessor&);
    ~PitcherAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    void tick();
    void pillClicked (PillButton*);

    PitcherAudioProcessor& proc;
    UiRes res;

    StaticLayer staticLayer { res };
    AboutButton aboutButton;
    KnobComponent pitchKnob  { dz::pivotPitch(),   0.00f };
    KnobComponent formantKnob{ dz::pivotFormant(), 0.08f };
    KnobComponent fineKnob   { dz::pivotFine(),    0.16f };
    std::unique_ptr<SliderAtt> pitchAtt, formantAtt, fineAtt;

    std::unique_ptr<ReadoutField> pitchMix, formantMix, outputField;
    std::unique_ptr<PillButton> presetPill, qualityPill, rangePill;
    std::unique_ptr<ToggleSwitch> preserveToggle;
    DropdownOverlay dropdown { res };
    AboutOverlay about { res };

    double lastTime = 0.0, startTime = 0.0;
    juce::VBlankAttachment vblank { this, [this] { tick(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitcherAudioProcessorEditor)
};
