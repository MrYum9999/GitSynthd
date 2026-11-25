#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

//==============================================================================
// Small, allocation-free building blocks shared by every GitSynth'd! module.
// Everything here runs per sample on the audio thread: no locks, no heap use
// after prepare().
//==============================================================================
namespace gs
{
    constexpr float pi    = juce::MathConstants<float>::pi;
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

    inline float semitonesToRatio (float st) noexcept { return std::exp2 (st / 12.0f); }

    //==========================================================================
    // Deterministic xorshift RNG (repeatable reverb tap patterns, noise, etc.)
    struct Rng
    {
        uint32_t s = 0x9E3779B9u;
        explicit Rng (uint32_t seed = 0x9E3779B9u) : s (seed ? seed : 1u) {}
        uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        float uni() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }     // [0,1)
        float bi()  noexcept { return uni() * 2.0f - 1.0f; }                              // [-1,1)
    };

    //==========================================================================
    // One-pole lowpass smoother with a time constant in milliseconds.
    struct OnePole
    {
        float a = 0.0f, z = 0.0f;
        void setTime (float ms, double sr) noexcept
        {
            a = ms <= 0.0f ? 0.0f : (float) std::exp (-1.0 / (0.001 * ms * sr));
        }
        void reset (float v = 0.0f) noexcept { z = v; }
        float process (float x) noexcept { z = x + a * (z - x); return z; }
    };

    //==========================================================================
    // DC blocker (leaky differentiator), ~10 Hz corner.
    struct DCBlocker
    {
        float x1 = 0.0f, y1 = 0.0f, r = 0.9986f;
        void prepare (double sr) noexcept { r = (float) std::exp (-twoPi * 10.0 / sr); }
        void reset() noexcept { x1 = y1 = 0.0f; }
        float process (float x) noexcept { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
    };

    //==========================================================================
    // Topology-preserving-transform state variable filter (Zavalishin).
    // Stable under per-sample cutoff modulation, which the synth voices need.
    struct SVF
    {
        float g = 0.0f, k = 1.414f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        float ic1 = 0.0f, ic2 = 0.0f;
        float lp = 0.0f, bp = 0.0f, hp = 0.0f;
        double sr = 44100.0;

        void prepare (double sampleRate) noexcept { sr = sampleRate; reset(); }
        void reset() noexcept { ic1 = ic2 = lp = bp = hp = 0.0f; }

        void set (float cutoffHz, float q) noexcept
        {
            cutoffHz = juce::jlimit (5.0f, (float) (sr * 0.49), cutoffHz);
            g  = std::tan (pi * cutoffHz / (float) sr);
            k  = 1.0f / std::max (0.05f, q);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        void process (float x) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            lp = v2; bp = v1; hp = x - k * v1 - v2;
        }
    };

    //==========================================================================
    // Plain biquad (RBJ cookbook), used for fixed filters like the tracker's
    // anti-alias stage and the reverb's tone shaping.
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        void reset() noexcept { z1 = z2 = 0.0f; }

        void setLowpass (double sr, double f, double q) noexcept
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * f / sr;
            const double c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
            b0 = (float) ((1.0 - c) * 0.5 / a0); b1 = (float) ((1.0 - c) / a0); b2 = b0;
            a1 = (float) (-2.0 * c / a0);        a2 = (float) ((1.0 - al) / a0);
        }

        void setHighpass (double sr, double f, double q) noexcept
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * f / sr;
            const double c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
            b0 = (float) ((1.0 + c) * 0.5 / a0); b1 = (float) (-(1.0 + c) / a0); b2 = b0;
            a1 = (float) (-2.0 * c / a0);         a2 = (float) ((1.0 - al) / a0);
        }

        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    //==========================================================================
    // Circular delay line with fractional (cubic Hermite) reads.
    struct DelayLine
    {
        std::vector<float> buf;
        int mask = 0, w = 0;

        void prepare (int maxSamples)
        {
            int n = 1;
            while (n < maxSamples + 4) n <<= 1;
            buf.assign ((size_t) n, 0.0f);
            mask = n - 1;
            w = 0;
        }
        void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }

        void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }

        // Integer delay: 0 returns the most recently pushed sample.
        float tap (int d) const noexcept { return buf[(size_t) ((w - 1 - d) & mask)]; }

        float tapFrac (float d) const noexcept
        {
            const int   i = (int) d;
            const float f = d - (float) i;
            const float xm1 = tap (i - 1), x0 = tap (i), x1 = tap (i + 1), x2 = tap (i + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }
    };

    //==========================================================================
    // PolyBLEP residual for band-limited saw / pulse edges.
    inline float polyBlep (float t, float dt) noexcept
    {
        if (t < dt)        { t /= dt;             return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    // Equal-power crossfade gains for a 0..1 mix amount.
    inline void equalPower (float mix, float& dryGain, float& wetGain) noexcept
    {
        mix = juce::jlimit (0.0f, 1.0f, mix);
        dryGain = std::cos (mix * pi * 0.5f);
        wetGain = std::sin (mix * pi * 0.5f);
    }
}
