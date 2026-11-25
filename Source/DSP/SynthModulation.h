#pragma once

#include "Common.h"

//==============================================================================
// Micro pitch shifter: a delay line read by two taps that sweep in opposite
// phase, cross-faded with sin^2 windows (constant-sum). The read pointer moves
// at (1 - ratio) samples per sample, which shifts the pitch by 'ratio'.
//==============================================================================
class MicroShifter
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        window = (float) (0.015 * sampleRate);      // 15 ms: tight, chorus-like
        line.prepare ((int) window + 8);
        reset();
    }
    void reset() { line.reset(); phase = 0.0f; }

    float process (float x, float ratio) noexcept
    {
        line.push (x);
        phase += (1.0f - ratio) / window;
        phase -= std::floor (phase);

        const float p2 = phase + 0.5f - (phase + 0.5f >= 1.0f ? 1.0f : 0.0f);
        const float d1 = 1.0f + phase * window;
        const float d2 = 1.0f + p2 * window;
        const float w1 = std::sin (gs::pi * phase);
        const float w2 = std::sin (gs::pi * p2);
        return line.tapFrac (d1) * w1 * w1 + line.tapFrac (d2) * w2 * w2;
    }

private:
    double sr = 44100.0;
    float window = 661.0f, phase = 0.0f;
    gs::DelayLine line;
};

//==============================================================================
// Internal synth modulation: centre voice plus two stereo-panned, micro
// pitch-shifted copies (one -df, one +df). Depth sets the spread (0..25 cents),
// Mix sets how much of the detuned pair is heard. The pair is panned
// symmetrically with constant-sum gains, so L+R collapses cleanly to mono.
//==============================================================================
class SynthModulation
{
public:
    void prepare (double sampleRate)
    {
        down.prepare (sampleRate);
        up.prepare (sampleRate);
        depthSm.reset (sampleRate, 0.05);
        mixSm.reset (sampleRate, 0.05);
    }
    void reset() { down.reset(); up.reset(); }

    void setParams (float depth01, float mix01) noexcept
    {
        depthSm.setTargetValue (depth01);
        mixSm.setTargetValue (mix01);
    }

    // mono in -> stereo out
    void process (float x, float& outL, float& outR) noexcept
    {
        const float cents = 25.0f * depthSm.getNextValue();
        const float mix   = mixSm.getNextValue();
        const float r     = std::exp2 (cents / 1200.0f);

        const float a = down.process (x, 1.0f / r);
        const float b = up.process (x, r);

        const float centre = x * (1.0f - 0.35f * mix);
        const float side   = 0.6f * mix;
        // -df voice mostly left, +df voice mostly right (0.8 / 0.2 split).
        outL = centre + side * (0.8f * a + 0.2f * b);
        outR = centre + side * (0.2f * a + 0.8f * b);
    }

private:
    MicroShifter down, up;
    juce::SmoothedValue<float> depthSm, mixSm;
};
