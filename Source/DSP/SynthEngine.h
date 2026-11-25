#pragma once

#include "Common.h"

//==============================================================================
// Band-limited oscillator (PolyBLEP saw / pulse, plus sine).
//==============================================================================
struct Osc
{
    float phase = 0.0f;
    void reset (float p = 0.0f) noexcept { phase = p; }

    // Advances and returns the requested shapes for this sample.
    // dt = frequency / sampleRate.
    inline void tick (float dt, float pw, float& saw, float& pulse, float& sine) noexcept
    {
        dt = juce::jlimit (1.0e-6f, 0.45f, dt);
        const float t = phase;

        saw = 2.0f * t - 1.0f - gs::polyBlep (t, dt);

        float t2 = t + (1.0f - pw);
        if (t2 >= 1.0f) t2 -= 1.0f;
        pulse = (t < pw ? 1.0f : -1.0f) + gs::polyBlep (t, dt) - gs::polyBlep (t2, dt);

        sine = std::sin (gs::twoPi * t);

        phase += dt;
        if (phase >= 1.0f) phase -= 1.0f;
    }
};

//==============================================================================
// Two-stage (S-curve) attack/release envelope. Cascading two one-poles gives
// the soft, "rounded" analogue-style edges the voices call for.
//==============================================================================
struct SmoothEnv
{
    gs::OnePole s1, s2;
    double sr = 44100.0;
    float attackMs = 5.0f, releaseMs = 100.0f;
    bool lastGate = false;

    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset() { s1.reset(); s2.reset(); lastGate = false; apply (false); }
    void setTimes (float a, float r) { attackMs = a; releaseMs = r; apply (lastGate); }

    float process (bool gate) noexcept
    {
        if (gate != lastGate) { lastGate = gate; apply (gate); }
        return s2.process (s1.process (gate ? 1.0f : 0.0f));
    }

private:
    void apply (bool gate)
    {
        const float t = (gate ? attackMs : releaseMs) * 0.5f;
        s1.setTime (t, sr);
        s2.setTime (t, sr);
    }
};

//==============================================================================
// The resynthesis voice bank. One engine is active at a time; it follows the
// tracked fundamental continuously and produces a fixed-amplitude mono signal
// whose level does not depend on the input level (the gate only switches it
// on and off).
//==============================================================================
class SynthEngine
{
public:
    enum class Mode { Deep = 0, Vintage, Solo, Strings, Harmony };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& o : osc) o.reset();
        for (auto& f : filt) f.prepare (sampleRate);
        env.prepare (sampleRate);
        fadeIn.setTime (6.0f, sampleRate);
        reset();
        setMode (mode, true);
    }

    void reset()
    {
        for (size_t i = 0; i < osc.size(); ++i) osc[i].reset ((float) i * 0.137f);
        for (auto& f : filt) f.reset();
        env.reset();
        fadeIn.reset (1.0f);
        lfoPhase = 0.0f;
        heldHz = 220.0f;
    }

    void setMode (Mode m, bool force = false)
    {
        if (m == mode && ! force) return;
        mode = m;
        for (auto& f : filt) f.reset();
        fadeIn.reset (0.0f);   // short fade-in on engine change, avoids a click

        switch (mode)
        {
            case Mode::Deep:    env.setTimes (18.0f,  140.0f); break;
            case Mode::Vintage: env.setTimes ( 6.0f,  180.0f); break;
            case Mode::Solo:    env.setTimes ( 2.0f,   90.0f); break;
            case Mode::Strings: env.setTimes (220.0f, 480.0f); break;
            case Mode::Harmony: env.setTimes ( 5.0f,  150.0f); break;
        }
    }

    void setHarmony (float interval1St, float interval2St, bool interval2On) noexcept
    {
        ratio1 = gs::semitonesToRatio (interval1St);
        ratio2 = gs::semitonesToRatio (interval2St);
        useInterval2 = interval2On;
    }

    float getEnvelope() const noexcept { return lastEnv; }

    // hz: smoothed tracked pitch. gate: tracker holds a confident lock.
    float process (float hz, bool gate) noexcept
    {
        if (gate) heldHz = hz;                  // keep last pitch through the release
        const float f   = heldHz;
        const float inv = 1.0f / (float) sr;
        const float e   = env.process (gate);
        lastEnv = e;
        if (e < 1.0e-5f && ! gate) return 0.0f;

        float out = 0.0f, saw, pulse, sine;

        switch (mode)
        {
            case Mode::Deep:
            {
                // Fundamental sine + sub-octave square and sine, heavily low-passed.
                float s2, p2, sn2;
                osc[0].tick (f * inv, 0.5f, saw, pulse, sine);
                osc[1].tick (0.5f * f * inv, 0.5f, s2, p2, sn2);
                const float raw = 0.55f * sine + 0.45f * sn2 + 0.30f * p2;
                filt[0].set (juce::jlimit (120.0f, 900.0f, f * 3.0f), 0.71f);
                filt[0].process (raw);
                out = 0.85f * filt[0].lp;
                break;
            }
            case Mode::Vintage:
            {
                // Saw + slightly detuned square through a soft 12 dB low-pass.
                float s2, p2, sn2;
                osc[0].tick (f * inv, 0.5f, saw, pulse, sine);
                osc[1].tick (f * 1.0023f * inv, 0.5f, s2, p2, sn2);
                const float raw = 0.6f * saw + 0.4f * p2;
                filt[0].set (juce::jlimit (600.0f, 3500.0f, f * 6.0f), 0.8f);
                filt[0].process (raw);
                out = 0.75f * std::tanh (1.2f * filt[0].lp);
                break;
            }
            case Mode::Solo:
            {
                // Bright saw + slowly width-modulated pulse + a touch of octave-up square.
                lfoPhase += 0.45f * inv; if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
                const float pw = 0.32f + 0.12f * std::sin (gs::twoPi * lfoPhase);
                float s2, p2, sn2, s3, p3, sn3;
                osc[0].tick (f * inv, 0.5f, saw, pulse, sine);
                osc[1].tick (f * inv * 1.0009f, pw, s2, p2, sn2);
                osc[2].tick (2.0f * f * inv, 0.5f, s3, p3, sn3);
                const float raw = 0.5f * saw + 0.4f * p2 + 0.15f * p3;
                filt[0].set (9000.0f, 1.1f);
                filt[0].process (raw);
                out = 0.6f * filt[0].lp;
                break;
            }
            case Mode::Strings:
            {
                // Ensemble: three detuned saws plus a quiet octave, mellow low-pass.
                static constexpr float det[3] = { 0.99481f, 1.0f, 1.00521f };   // about -9 / 0 / +9 cents
                float acc = 0.0f;
                for (int i = 0; i < 3; ++i)
                {
                    osc[(size_t) i].tick (f * det[i] * inv, 0.5f, saw, pulse, sine);
                    acc += saw;
                }
                osc[3].tick (2.0f * f * inv, 0.5f, saw, pulse, sine);
                acc = acc * 0.33f + 0.2f * saw;
                filt[0].set (juce::jlimit (2500.0f, 6000.0f, f * 8.0f), 0.6f);
                filt[0].process (acc);
                out = 0.8f * filt[0].lp;
                break;
            }
            case Mode::Harmony:
            {
                // Fundamental plus Interval 1 (always on) and Interval 2 (toggle).
                const float r[3] = { 1.0f, ratio1, ratio2 };
                const int voices = useInterval2 ? 3 : 2;
                float acc = 0.0f;
                for (int v = 0; v < voices; ++v)
                {
                    osc[(size_t) v].tick (f * r[v] * inv, 0.5f, saw, pulse, sine);
                    acc += 0.6f * saw + 0.4f * pulse;
                }
                acc *= (voices == 3 ? 0.42f : 0.55f);
                filt[0].set (4200.0f, 0.7f);
                filt[0].process (acc);
                out = 0.85f * filt[0].lp;
                break;
            }
        }

        return out * e * fadeIn.process (1.0f);
    }

private:
    double sr = 44100.0;
    Mode mode = Mode::Vintage;
    std::array<Osc, 4> osc;
    std::array<gs::SVF, 1> filt;
    SmoothEnv env;
    gs::OnePole fadeIn;
    float ratio1 = 1.4983f, ratio2 = 2.0f, lfoPhase = 0.0f, heldHz = 220.0f, lastEnv = 0.0f;
    bool useInterval2 = false;
};
