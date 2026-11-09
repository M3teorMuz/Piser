// Пружинные анимации в стиле Apple: плавное движение без рывков, лёгкий "отскок" там, где нужно.
#pragma once
#include <juce_graphics/juce_graphics.h>
#include <algorithm>
#include <cmath>

struct Spring
{
    float x = 0.0f, v = 0.0f, target = 0.0f;
    float omega = 26.0f;      // жёсткость: больше = быстрее (26 ~ 150 мс)
    float zeta  = 1.0f;       // 1 = без перелёта, <1 = лёгкий отскок

    Spring() = default;
    Spring (float start, float w, float z) : x (start), target (start), omega (w), zeta (z) {}

    void snap (float t) noexcept { x = target = t; v = 0.0f; }
    bool settled() const noexcept { return x == target && v == 0.0f; }

    // возвращает true, если значение изменилось (нужна перерисовка)
    bool step (float dt) noexcept
    {
        if (settled()) return false;
        dt = std::min (dt, 0.05f);
        const int n = std::max (1, (int) std::ceil (dt / 0.004f));
        const float h = dt / (float) n, k = omega * omega, c = 2.0f * zeta * omega;
        for (int i = 0; i < n; ++i)
        {
            const float a = -k * (x - target) - c * v;
            v += a * h;
            x += v * h;
        }
        if (std::abs (x - target) < 2.0e-4f && std::abs (v) < 2.0e-3f) { x = target; v = 0.0f; }
        return true;
    }
};

inline float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float easeOutCubic (float t) noexcept { t = clamp01 (t); const float u = 1.0f - t; return 1.0f - u * u * u; }
inline juce::Colour mixColour (juce::Colour a, juce::Colour b, float t) noexcept { return a.interpolatedWith (b, clamp01 (t)); }
