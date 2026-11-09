// ============================================================================
//  EXPitch Setup: установщик в стиле плагина (фиолетовая палитра, Inter, пружинные анимации).
//  Плагин (EXPitch.vst3) встроен в exe как payload.zip и распаковывается в выбранную папку.
// ============================================================================
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"
#include "../Source/Anim.h"
#include <atomic>

bool isRunningElevated();     // Admin.cpp
bool relaunchElevated();      // Admin.cpp

namespace
{
    using Rect = juce::Rectangle<float>;
    using juce::Colour;

    Colour rgb (int r, int g, int b) { return Colour::fromRGB ((juce::uint8) r, (juce::uint8) g, (juce::uint8) b); }
    Colour rgba (int r, int g, int b, float a) { return rgb (r, g, b).withAlpha (a); }
    Colour dz_accent() { return rgb (138, 106, 255); }
    juce::PathStrokeType roundStroke (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); }

    const char* const kVersion = "0.5.0";
    const char* const kPluginFolder = "EXPitch.vst3";

    constexpr float kMargin = 20.0f, kPW = 720.0f, kPH = 420.0f;

    // создание шрифта из памяти: подбираем доступную сигнатуру JUCE 8
    template <typename T = juce::Typeface>
    auto makeTypefaceImpl (const void* d, size_t n, int) -> decltype (T::createSystemTypefaceFor (d, n))
    { return T::createSystemTypefaceFor (d, n); }

    template <typename T = juce::Typeface>
    auto makeTypefaceImpl (const void* d, size_t n, long)
        -> decltype (T::createSystemTypefaceFor (juce::Span<const std::byte> (static_cast<const std::byte*> (d), n)))
    { return T::createSystemTypefaceFor (juce::Span<const std::byte> (static_cast<const std::byte*> (d), n)); }

    juce::Typeface::Ptr makeTypeface (const char* data, int size)
    { return makeTypefaceImpl (static_cast<const void*> (data), (size_t) size, 0); }

    struct Fonts
    {
        Fonts()
        {
            regular = makeTypeface (BinaryData::Inter_Regular_otf, BinaryData::Inter_Regular_otfSize);
            medium  = makeTypeface (BinaryData::Inter_Medium_otf,  BinaryData::Inter_Medium_otfSize);
            // калибровка: "em-размер" Inter -> высота шрифта JUCE (по ширине эталонной строки)
            kReg = calib (regular, 813.09f);
            kMed = calib (medium, 825.88f);
        }
        float calib (juce::Typeface::Ptr tf, float widthAt100Em) const
        {
            juce::GlyphArrangement ga;
            ga.addLineOfText (juce::Font (juce::FontOptions().withTypeface (tf).withHeight (100.0f)), "Hamburgefonstiv", 0.0f, 0.0f);
            const float w = ga.getBoundingBox (0, -1, true).getWidth();
            return w > 1.0f ? widthAt100Em / w : 1.0f;     // высота JUCE = em * (widthAt100Em / ширина при высоте 100)
        }
        juce::Font reg (float em) const { return juce::Font (juce::FontOptions().withTypeface (regular).withHeight (em * kReg)); }
        juce::Font med (float em) const { return juce::Font (juce::FontOptions().withTypeface (medium).withHeight (em * kMed)); }

        juce::Typeface::Ptr regular, medium;
        float kReg = 1.0f, kMed = 1.0f;
    };

    float textWidth (const juce::Font& f, const juce::String& s)
    {
        juce::GlyphArrangement ga; ga.addLineOfText (f, s, 0.0f, 0.0f);
        return ga.getBoundingBox (0, -1, true).getWidth();
    }

    void drawText (juce::Graphics& g, const juce::Font& f, const juce::String& s, float x, float baseline, Colour c, float alpha = 1.0f)
    {
        if (alpha <= 0.003f) return;
        juce::GlyphArrangement ga; ga.addLineOfText (f, s, x, baseline);
        g.setColour (c.withMultipliedAlpha (alpha));
        ga.draw (g);
    }

    void drawCentered (juce::Graphics& g, const juce::Font& f, const juce::String& s, float cx, float baseline, Colour c, float alpha = 1.0f)
    {
        drawText (g, f, s, cx - textWidth (f, s) * 0.5f, baseline, c, alpha);
    }

    // длинный путь сокращается посередине: C:\Program Files\...\VST3
    juce::String elide (const juce::Font& f, const juce::String& s, float maxW)
    {
        if (textWidth (f, s) <= maxW) return s;
        const int n = s.length();
        for (int keep = n - 2; keep > 6; --keep)
        {
            const int head = keep / 2, tail = keep - head;
            const auto t = s.substring (0, head) + juce::String::fromUTF8 ("\xe2\x80\xa6") + s.substring (n - tail);
            if (textWidth (f, t) <= maxW) return t;
        }
        return s;
    }

    juce::String defaultInstallPath()
    {
        const auto common = juce::SystemStats::getEnvironmentVariable ("CommonProgramFiles", "C:\\Program Files\\Common Files");
        return common + "\\VST3";
    }
}

//==============================================================================
// Распаковка плагина в отдельном потоке (чтобы окно не зависало)
class InstallThread : public juce::Thread
{
public:
    using Done = std::function<void (bool ok, juce::String message, bool needsAdmin)>;

    InstallThread (juce::File targetFolder, Done d) : juce::Thread ("EXPitch install"), target (std::move (targetFolder)), done (std::move (d)) {}
    ~InstallThread() override { stopThread (3000); }

    std::atomic<float> progress { 0.0f };
    juce::String currentFile() const { const juce::ScopedLock sl (lock); return current; }

    void run() override
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();

        juce::MemoryInputStream in (BinaryData::payload_zip, (size_t) BinaryData::payload_zipSize, false);
        juce::ZipFile zip (in);
        const int n = zip.getNumEntries();
        if (n <= 0) { finish (false, "The installer package is damaged. Please download the installer again.", false); return; }

        if (! target.exists() && ! target.createDirectory().wasOk())
        { finish (false, "Cannot create the folder:\n" + target.getFullPathName(), true); return; }

        const auto probe = target.getChildFile (".expitch_write_test");
        if (! probe.replaceWithText ("x"))
        { finish (false, "No permission to write to this folder.\nRun the installer as administrator.", true); return; }
        probe.deleteFile();

        const auto dest = target.getChildFile (kPluginFolder);
        if (dest.exists() && ! dest.deleteRecursively())
        { finish (false, "Cannot replace the existing EXPitch.vst3.\nClose FL Studio (or any DAW using the plugin) and try again.", false); return; }

        for (int i = 0; i < n; ++i)
        {
            if (threadShouldExit()) return;
            if (auto* e = zip.getEntry (i))
            {
                { const juce::ScopedLock sl (lock); current = e->filename; }
                const auto r = zip.uncompressEntry (i, target, true);
                if (r.failed()) { finish (false, "Could not write a file:\n" + r.getErrorMessage(), false); return; }
            }
            progress = (float) (i + 1) / (float) n;
            wait (60);                               // чтобы прогресс-бар был виден и на маленьких файлах
        }

        // минимальная длительность, чтобы анимация не «мигала»
        while (juce::Time::getMillisecondCounterHiRes() - t0 < 1500.0 && ! threadShouldExit())
            wait (30);

        finish (true, {}, false);
    }

private:
    void finish (bool ok, juce::String msg, bool admin)
    {
        auto cb = done;
        juce::MessageManager::callAsync ([cb, ok, msg, admin] { cb (ok, msg, admin); });
    }

    juce::File target;
    Done done;
    juce::CriticalSection lock;
    juce::String current;
};

//==============================================================================
class SetupWindow : public juce::Component
{
public:
    SetupWindow();
    ~SetupWindow() override;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum class Screen { setup, installing, done, error };
    enum Btn { bClose, bMin, bBrowse, bInstall, bOpen, bFinish, bBack, bAdmin, bCount };

    struct Button { Rect r; Spring hover { 0.0f, 22.0f, 1.0f }, press { 0.0f, 38.0f, 0.9f }; };

    void tick();
    bool visibleIn (int b) const;
    int hitButton (juce::Point<float> p) const;
    void activate (int b);
    void startInstall();
    void setScreen (Screen s);

    void drawPrimary (juce::Graphics&, const Button&, const juce::String&, float a) const;
    void drawSecondary (juce::Graphics&, const Button&, const juce::String&, float a) const;
    void drawCircleButton (juce::Graphics&, const Button&, bool close) const;
    void drawCheck (juce::Graphics&, juce::Point<float> c, float t, bool error, float a) const;

    Fonts fonts;
    juce::Image bg, noise, shadowImg;
    Button btn[bCount];
    Screen screen = Screen::setup;
    Spring sc[4];                                    // прозрачность экранов
    Spring progressShown { 0.0f, 12.0f, 1.0f }, checkT { 0.0f, 9.0f, 1.0f }, intro { 0.0f, 10.0f, 1.0f };

    juce::String path = defaultInstallPath(), errorText, doneCurrent;
    bool errorNeedsAdmin = false;
    float timeS = 0.0f;
    int pressedBtn = -1, hoverBtn = -1;

    std::unique_ptr<InstallThread> thread;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ComponentDragger dragger;
    bool dragging = false;
    double lastTime = 0.0;
    juce::VBlankAttachment vblank { this, [this] { tick(); } };
};

SetupWindow::SetupWindow()
{
    bg    = juce::ImageCache::getFromMemory (BinaryData::bg_png,    BinaryData::bg_pngSize);
    noise = juce::ImageCache::getFromMemory (BinaryData::noise_png, BinaryData::noise_pngSize);

    btn[bClose].r   = { kPW - 46.0f, 24.0f, 24.0f, 24.0f };
    btn[bMin].r     = { kPW - 78.0f, 24.0f, 24.0f, 24.0f };
    btn[bBrowse].r  = { 496.0f, 208.0f, 108.0f, 38.0f };
    btn[bInstall].r = { 250.0f, 322.0f, 220.0f, 56.0f };
    btn[bOpen].r    = { 168.0f, 340.0f, 184.0f, 50.0f };
    btn[bFinish].r  = { 368.0f, 340.0f, 184.0f, 50.0f };
    btn[bBack].r    = { 168.0f, 340.0f, 184.0f, 50.0f };
    btn[bAdmin].r   = { 368.0f, 340.0f, 184.0f, 50.0f };

    sc[0].omega = sc[1].omega = sc[2].omega = sc[3].omega = 14.0f;
    sc[0].snap (1.0f);

    setName ("EXPitch Setup");
    setOpaque (false);
    setSize ((int) (kPW + 2 * kMargin), (int) (kPH + 2 * kMargin));
    centreWithSize (getWidth(), getHeight());
    addToDesktop (juce::ComponentPeer::windowAppearsOnTaskbar | juce::ComponentPeer::windowIsSemiTransparent);
    setVisible (true);
    lastTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    intro.target = 1.0f;
}

SetupWindow::~SetupWindow() { thread.reset(); }

bool SetupWindow::visibleIn (int b) const
{
    switch (b)
    {
        case bClose: case bMin: return true;
        case bBrowse: case bInstall: return screen == Screen::setup;
        case bOpen: case bFinish: return screen == Screen::done;
        case bBack: return screen == Screen::error;
        case bAdmin: return screen == Screen::error && errorNeedsAdmin;
        default: return false;
    }
}

int SetupWindow::hitButton (juce::Point<float> p) const
{
    for (int b = bCount - 1; b >= 0; --b)
        if (visibleIn (b) && btn[b].r.expanded (b <= bMin ? 4.0f : 0.0f).contains (p)) return b;
    return -1;
}

void SetupWindow::setScreen (Screen s)
{
    screen = s;
    for (int i = 0; i < 4; ++i) sc[i].target = (i == (int) s) ? 1.0f : 0.0f;
    if (s == Screen::done) { checkT.snap (0.0f); checkT.target = 1.0f; }
    if (s == Screen::error) { checkT.snap (0.0f); checkT.target = 1.0f; }
}

void SetupWindow::startInstall()
{
    progressShown.snap (0.0f);
    progressShown.target = 0.0f;
    setScreen (Screen::installing);
    juce::Component::SafePointer<SetupWindow> safe (this);
    thread = std::make_unique<InstallThread> (juce::File (path), [safe] (bool ok, juce::String msg, bool admin)
    {
        if (safe == nullptr) return;
        safe->errorText = msg;
        safe->errorNeedsAdmin = admin;
        safe->progressShown.target = ok ? 1.0f : safe->progressShown.target;
        safe->setScreen (ok ? Screen::done : Screen::error);
    });
    thread->startThread();
}

void SetupWindow::activate (int b)
{
    switch (b)
    {
        case bClose: juce::JUCEApplication::quit(); break;
        case bMin:   if (auto* p = getPeer()) p->setMinimised (true); break;
        case bBrowse:
            chooser = std::make_unique<juce::FileChooser> ("Choose where to install EXPitch.vst3", juce::File (path), "", true);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      const auto r = fc.getResult();
                                      if (r != juce::File()) { path = r.getFullPathName(); repaint(); }
                                  });
            break;
        case bInstall: startInstall(); break;
        case bOpen:    juce::File (path).getChildFile (kPluginFolder).revealToUser(); break;
        case bFinish:  juce::JUCEApplication::quit(); break;
        case bBack:    setScreen (Screen::setup); break;
        case bAdmin:   if (relaunchElevated()) juce::JUCEApplication::quit(); break;
        default: break;
    }
}

void SetupWindow::mouseMove (const juce::MouseEvent& e)
{
    hoverBtn = hitButton (e.position - juce::Point<float> (kMargin, kMargin));
    setMouseCursor (hoverBtn >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void SetupWindow::mouseExit (const juce::MouseEvent&) { hoverBtn = -1; }

void SetupWindow::mouseDown (const juce::MouseEvent& e)
{
    const int b = hitButton (e.position - juce::Point<float> (kMargin, kMargin));
    pressedBtn = b;
    if (b < 0 && e.position.y < kMargin + 110.0f) { dragger.startDraggingComponent (this, e); dragging = true; }
}

void SetupWindow::mouseDrag (const juce::MouseEvent& e) { if (dragging) dragger.dragComponent (this, e, nullptr); }

void SetupWindow::mouseUp (const juce::MouseEvent& e)
{
    dragging = false;
    const int b = hitButton (e.position - juce::Point<float> (kMargin, kMargin));
    const int was = pressedBtn;
    pressedBtn = -1;
    if (b >= 0 && b == was) activate (b);
}

void SetupWindow::tick()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = (float) std::min (0.1, std::max (0.0, now - lastTime));
    lastTime = now;
    timeS += dt;

    bool changed = true;                               // окно маленькое: перерисовываем каждый кадр
    for (int b = 0; b < bCount; ++b)
    {
        btn[b].hover.target = (b == hoverBtn) ? 1.0f : 0.0f;
        btn[b].press.target = (b == pressedBtn && b == hoverBtn) ? 1.0f : 0.0f;
        btn[b].hover.step (dt); btn[b].press.step (dt);
    }
    for (auto& s : sc) s.step (dt);
    if (thread != nullptr && screen == Screen::installing) progressShown.target = thread->progress.load();
    progressShown.step (dt); checkT.step (dt); intro.step (dt);
    if (changed) repaint();
}

//------------------------------------------------------------------------------
void SetupWindow::drawPrimary (juce::Graphics& g, const Button& b, const juce::String& text, float a) const
{
    if (a <= 0.003f) return;
    const float h = b.hover.x, p = b.press.x;
    auto r = b.r.withSizeKeepingCentre (b.r.getWidth() * (1.0f + 0.02f * h - 0.03f * p), b.r.getHeight() * (1.0f + 0.02f * h - 0.03f * p));
    juce::Path pp; pp.addRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setOpacity (a);
    juce::DropShadow (dz_accent().withAlpha (0.45f + 0.2f * h), 18, { 0, 6 }).drawForPath (g, pp);
    g.setGradientFill (juce::ColourGradient (mixColour (rgb (150, 122, 255), rgb (168, 142, 255), h), 0.0f, r.getY(),
                                             mixColour (rgb (110, 82, 228), rgb (122, 94, 240), h), 0.0f, r.getBottom(), false));
    g.fillPath (pp);
    g.setColour (juce::Colours::white.withAlpha (0.22f * a));
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f - 0.5f, 1.0f);
    const auto f = fonts.med (19.0f);
    drawCentered (g, f, text, r.getCentreX(), r.getCentreY() + 6.8f, rgb (255, 255, 255), a);
}

void SetupWindow::drawSecondary (juce::Graphics& g, const Button& b, const juce::String& text, float a) const
{
    if (a <= 0.003f) return;
    const float h = b.hover.x, p = b.press.x;
    auto r = b.r.withSizeKeepingCentre (b.r.getWidth() * (1.0f - 0.025f * p), b.r.getHeight() * (1.0f - 0.025f * p));
    const int lift = (int) (9.0f * h);
    g.setGradientFill (juce::ColourGradient (rgba (36 + lift, 38 + lift, 54 + lift, a), 0.0f, r.getY(), rgba (27 + lift, 29 + lift, 42 + lift, a), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (juce::Colours::white.withAlpha ((0.16f + 0.10f * h) * a));
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f - 0.5f, 1.0f);
    drawCentered (g, fonts.med (18.0f), text, r.getCentreX(), r.getCentreY() + 6.4f, rgb (214, 216, 232), a);
}

void SetupWindow::drawCircleButton (juce::Graphics& g, const Button& b, bool close) const
{
    const float h = b.hover.x, p = b.press.x;
    const auto c = b.r.getCentre();
    const float r = 12.0f * (1.0f + 0.08f * h - 0.08f * p);
    g.setGradientFill (juce::ColourGradient (mixColour (rgb (126, 124, 170), rgb (158, 150, 235), h), c.x, c.y - r,
                                             mixColour (rgb (96, 93, 145), rgb (116, 100, 215), h), c.x, c.y + r, false));
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour (rgba (24, 24, 40, 0.35f + 0.5f * h));
    juce::Path ic;
    if (close) { ic.startNewSubPath (c.x - 3.6f, c.y - 3.6f); ic.lineTo (c.x + 3.6f, c.y + 3.6f); ic.startNewSubPath (c.x + 3.6f, c.y - 3.6f); ic.lineTo (c.x - 3.6f, c.y + 3.6f); }
    else       { ic.startNewSubPath (c.x - 4.0f, c.y); ic.lineTo (c.x + 4.0f, c.y); }
    g.strokePath (ic, roundStroke (1.8f));
}

void SetupWindow::drawCheck (juce::Graphics& g, juce::Point<float> c, float t, bool error, float a) const
{
    if (a <= 0.003f) return;
    const float pop = 0.82f + 0.18f * std::min (1.1f, easeOutCubic (t * 1.6f));
    const float r = 36.0f * pop;
    const Colour top = error ? rgb (255, 128, 140) : rgb (150, 122, 255), bot = error ? rgb (220, 80, 100) : rgb (110, 82, 228);
    juce::Path disc; disc.addEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setOpacity (a);
    juce::DropShadow ((error ? rgb (255, 100, 120) : dz_accent()).withAlpha (0.55f), 22, { 0, 6 }).drawForPath (g, disc);
    g.setGradientFill (juce::ColourGradient (top.withAlpha (a), c.x, c.y - r, bot.withAlpha (a), c.x, c.y + r, false));
    g.fillPath (disc);
    g.setColour (juce::Colours::white.withAlpha (0.25f * a));
    g.drawEllipse (c.x - r + 0.5f, c.y - r + 0.5f, r * 2.0f - 1.0f, r * 2.0f - 1.0f, 1.0f);

    // галочка «рисуется» по длине; для ошибки восклицательный знак
    juce::Path p;
    if (! error)
    {
        const juce::Point<float> a0 (c.x - 13.0f, c.y + 1.0f), a1 (c.x - 4.0f, c.y + 10.0f), a2 (c.x + 14.0f, c.y - 9.0f);
        const float l1 = a0.getDistanceFrom (a1), l2 = a1.getDistanceFrom (a2);
        float d = std::max (0.0f, std::min (1.0f, (t - 0.25f) / 0.6f)) * (l1 + l2);
        p.startNewSubPath (a0);
        if (d <= l1) p.lineTo (a0 + (a1 - a0) * (d / l1));
        else { p.lineTo (a1); p.lineTo (a1 + (a2 - a1) * ((d - l1) / l2)); }
    }
    else
    {
        p.startNewSubPath (c.x, c.y - 14.0f); p.lineTo (c.x, c.y + 3.0f);
        p.startNewSubPath (c.x, c.y + 11.0f); p.lineTo (c.x, c.y + 11.2f);
    }
    g.setColour (rgb (255, 255, 255).withAlpha (a));
    g.strokePath (p, roundStroke (4.4f));
}

void SetupWindow::paint (juce::Graphics& g)
{
    const float in = easeOutCubic (intro.x);
    g.beginTransparencyLayer (in);
    g.addTransform (juce::AffineTransform::scale (0.97f + 0.03f * in, 0.97f + 0.03f * in, (float) getWidth() * 0.5f, (float) getHeight() * 0.5f));
    g.setOrigin ((int) kMargin, (int) kMargin);

    const Rect panel (0.0f, 0.0f, kPW, kPH);
    juce::Path pp; pp.addRoundedRectangle (panel, 38.0f);

    if (shadowImg.isNull())
    {
        shadowImg = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
        juce::Graphics gi (shadowImg);
        gi.setOrigin ((int) kMargin, (int) kMargin);
        juce::DropShadow (rgba (0, 0, 0, 0.6f), 24, { 0, 10 }).drawForPath (gi, pp);
    }
    { juce::Graphics::ScopedSaveState ss (g); g.setOrigin (-(int) kMargin, -(int) kMargin); g.drawImageAt (shadowImg, 0, 0); }

    g.saveState();
    g.reduceClipRegion (pp, juce::AffineTransform());
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (bg, panel, juce::RectanglePlacement::stretchToFit);
    g.setTiledImageFill (noise, 0, 0, 0.9f);
    g.fillRect (panel);
    {
        juce::ColourGradient gr (juce::Colours::white.withAlpha (0.045f), 0.0f, 0.0f, juce::Colours::transparentWhite, 0.0f, 90.0f, false);
        gr.addColour (0.5, juce::Colours::white.withAlpha (0.011f));
        g.setGradientFill (gr);
        g.fillRect (0.0f, 0.0f, kPW, 90.0f);
    }
    g.restoreState();
    g.setColour (rgba (150, 140, 225, 0.10f));
    { juce::Path gl; gl.addRoundedRectangle (3.0f, 3.0f, kPW - 6.0f, kPH - 6.0f, 35.0f); g.strokePath (gl, juce::PathStrokeType (5.0f)); }
    g.setColour (rgba (155, 150, 210, 0.70f));
    { juce::Path fr; fr.addRoundedRectangle (0.7f, 0.7f, kPW - 1.4f, kPH - 1.4f, 37.3f); g.strokePath (fr, juce::PathStrokeType (1.4f)); }

    drawCircleButton (g, btn[bClose], true);
    drawCircleButton (g, btn[bMin], false);

    // шапка
    drawCentered (g, fonts.med (38.0f), "EXPitch", kPW * 0.5f, 92.0f, rgb (212, 213, 231));
    drawCentered (g, fonts.reg (15.0f), juce::String ("Installer  -  version ") + kVersion, kPW * 0.5f, 120.0f, rgb (150, 153, 175));

    auto layer = [this, &g] (int idx, auto&& body)
    {
        const float a = clamp01 (sc[idx].x);
        if (a <= 0.003f) return;
        g.beginTransparencyLayer (a);
        g.addTransform (juce::AffineTransform::translation (0.0f, (1.0f - a) * 12.0f));
        body();
        g.endTransparencyLayer();
    };

    // --- 1. выбор папки
    layer (0, [&]
    {
        drawText (g, fonts.reg (12.5f), "INSTALL LOCATION", 104.0f, 190.0f, rgb (130, 133, 158));
        const Rect field (100.0f, 200.0f, 520.0f, 54.0f);
        g.setGradientFill (juce::ColourGradient (rgb (36, 38, 54), 0.0f, field.getY(), rgb (27, 29, 42), 0.0f, field.getBottom(), false));
        g.fillRoundedRectangle (field, 27.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.20f), 0.0f, field.getY(), juce::Colours::white.withAlpha (0.06f), 0.0f, field.getBottom(), false));
        g.drawRoundedRectangle (field.reduced (0.5f), 26.5f, 1.0f);
        const auto pf = fonts.reg (16.5f);
        drawText (g, pf, elide (pf, path, 350.0f), 126.0f, 233.5f, rgb (205, 207, 225));
        const auto& b = btn[bBrowse];
        const float h = b.hover.x, p = b.press.x;
        auto r = b.r.withSizeKeepingCentre (b.r.getWidth() * (1.0f - 0.03f * p), b.r.getHeight() * (1.0f - 0.03f * p));
        g.setColour (dz_accent().withAlpha (0.18f + 0.16f * h));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (dz_accent().withAlpha (0.55f + 0.25f * h));
        g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f - 0.5f, 1.0f);
        drawCentered (g, fonts.med (16.0f), "Browse", r.getCentreX(), r.getCentreY() + 5.8f, rgb (214, 200, 255));
        drawText (g, fonts.reg (14.0f), "The VST3 folder. FL Studio scans it automatically.", 104.0f, 284.0f, rgb (140, 143, 166));
        drawPrimary (g, btn[bInstall], "Install", 1.0f);
    });

    // --- 2. установка
    layer (1, [&]
    {
        drawCentered (g, fonts.med (26.0f), "Installing", kPW * 0.5f, 206.0f, rgb (226, 227, 242));
        const Rect track (100.0f, 232.0f, 520.0f, 14.0f);
        g.setColour (rgba (255, 255, 255, 0.07f));
        g.fillRoundedRectangle (track, 7.0f);
        const float p = clamp01 (progressShown.x);
        const float w = std::max (14.0f, track.getWidth() * p);
        const Rect fill (track.getX(), track.getY(), w, track.getHeight());
        juce::Path fp; fp.addRoundedRectangle (fill, 7.0f);
        juce::DropShadow (dz_accent().withAlpha (0.55f), 14, { 0, 0 }).drawForPath (g, fp);
        g.setGradientFill (juce::ColourGradient (rgb (150, 122, 255), fill.getX(), 0.0f, rgb (112, 84, 232), fill.getRight(), 0.0f, false));
        g.fillPath (fp);
        {   // бегущий блик
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (fp, juce::AffineTransform());
            const float sx = fill.getX() + std::fmod (timeS * 160.0f, w + 120.0f) - 60.0f;
            g.setGradientFill (juce::ColourGradient (juce::Colours::transparentWhite, sx - 40.0f, 0.0f, juce::Colours::transparentWhite, sx + 40.0f, 0.0f, false));
            juce::ColourGradient sh (juce::Colours::transparentWhite, sx - 40.0f, 0.0f, juce::Colours::transparentWhite, sx + 40.0f, 0.0f, false);
            sh.addColour (0.5, juce::Colours::white.withAlpha (0.30f));
            g.setGradientFill (sh);
            g.fillRect (fill);
        }
        const auto sf = fonts.reg (14.0f);
        const juce::String cur = thread != nullptr ? thread->currentFile() : juce::String();
        drawText (g, sf, elide (sf, cur.isEmpty() ? juce::String ("Preparing...") : cur, 400.0f), 100.0f, 276.0f, rgb (140, 143, 166));
        const juce::String pct = juce::String (juce::roundToInt (p * 100.0f)) + "%";
        drawText (g, fonts.med (14.0f), pct, 620.0f - textWidth (fonts.med (14.0f), pct), 276.0f, rgb (190, 192, 212));
        drawCentered (g, fonts.reg (14.0f), "Please keep this window open", kPW * 0.5f, 372.0f, rgb (120, 123, 148));
    });

    // --- 3. готово
    layer (2, [&]
    {
        drawCheck (g, { kPW * 0.5f, 188.0f }, checkT.x, false, 1.0f);
        drawCentered (g, fonts.med (26.0f), "Installation complete", kPW * 0.5f, 266.0f, rgb (226, 227, 242));
        const auto pf = fonts.reg (14.0f);
        drawCentered (g, pf, elide (pf, juce::File (path).getChildFile (kPluginFolder).getFullPathName(), 520.0f), kPW * 0.5f, 292.0f, rgb (150, 153, 175));
        drawCentered (g, pf, "In FL Studio: Options > Manage plugins > Find installed plugins", kPW * 0.5f, 316.0f, rgb (130, 133, 158));
        drawSecondary (g, btn[bOpen], "Open folder", 1.0f);
        drawPrimary (g, btn[bFinish], "Finish", 1.0f);
    });

    // --- 4. ошибка
    layer (3, [&]
    {
        drawCheck (g, { kPW * 0.5f, 188.0f }, checkT.x, true, 1.0f);
        drawCentered (g, fonts.med (26.0f), "Installation failed", kPW * 0.5f, 266.0f, rgb (255, 214, 220));
        const auto pf = fonts.reg (14.0f);
        float y = 292.0f;
        for (const auto& line : juce::StringArray::fromLines (errorText))
        {
            drawCentered (g, pf, elide (pf, line, 560.0f), kPW * 0.5f, y, rgb (170, 173, 195));
            y += 22.0f;
        }
        drawSecondary (g, btn[bBack], "Back", 1.0f);
        if (errorNeedsAdmin) drawPrimary (g, btn[bAdmin], "Run as administrator", 1.0f);
    });

    g.endTransparencyLayer();
}

//==============================================================================
class SetupApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "EXPitch Setup"; }
    const juce::String getApplicationVersion() override { return kVersion; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        // установка в Program Files требует прав администратора: запрашиваем один раз при запуске
        if (! isRunningElevated() && relaunchElevated())
        {
            quit();
            return;
        }
        window = std::make_unique<SetupWindow>();
    }

    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<SetupWindow> window;
};

START_JUCE_APPLICATION (SetupApp)
