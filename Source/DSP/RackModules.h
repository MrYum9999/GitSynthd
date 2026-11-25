#pragma once

#include "Common.h"

//==============================================================================
// The three reorderable rack modules. Each one processes a stereo sample pair
// in place, has no latency and owns its own parameter smoothing.
//==============================================================================

//==============================================================================
// BITCRUSH: sample-and-hold decimation + uniform mid-tread quantisation.
//   Crush      441 Hz .. 44.1 kHz target rate. The ceiling is clamped to
//              44.1 kHz even in 48 / 96 / 192 kHz sessions, so the top of the
//              range always means "44.1 kHz grit", never "transparent".
//   Resolution 1 .. 24 bits (continuous, so sweeps are stepless).
//              Mid-tread: zero is a valid output level, so silence stays silent.
//==============================================================================
class Bitcrusher
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        rateSm.reset (sampleRate, 0.03);
        bitsSm.reset (sampleRate, 0.03);
        reset();
    }
    void reset() { phase = 1.0f; holdL = holdR = 0.0f; }

    void setParams (float crushHz, float bits)
    {
        rateSm.setTargetValue (std::min (crushHz, 44100.0f));
        bitsSm.setTargetValue (bits);
    }

    void process (float& l, float& r) noexcept
    {
        const float rate = rateSm.getNextValue();
        const float bits = bitsSm.getNextValue();

        phase += rate / (float) sr;
        if (phase >= 1.0f)
        {
            phase -= std::floor (phase);
            holdL = l; holdR = r;
        }

        const float step = std::exp2 (1.0f - bits);       // 2 / 2^bits over a +-1 range
        l = juce::jlimit (-1.0f, 1.0f, step * std::round (holdL / step));
        r = juce::jlimit (-1.0f, 1.0f, step * std::round (holdR / step));
    }

private:
    double sr = 44100.0;
    float phase = 1.0f, holdL = 0.0f, holdR = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> rateSm;
    juce::SmoothedValue<float> bitsSm;
};

//==============================================================================
// RING MOD: four-quadrant multiplication with an internal sine carrier.
//==============================================================================
class RingModulator
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        freqSm.reset (sampleRate, 0.03);
        mixSm.reset (sampleRate, 0.03);
        freqSm.setCurrentAndTargetValue (440.0f);
        reset();
    }
    void reset() { phase = 0.0; }

    void setParams (float hz, float mix01) { freqSm.setTargetValue (hz); mixSm.setTargetValue (mix01); }

    void process (float& l, float& r) noexcept
    {
        const float m = mixSm.getNextValue();
        phase += freqSm.getNextValue() / sr;
        if (phase >= 1.0) phase -= 1.0;
        const float c = std::sin (gs::twoPi * (float) phase);
        l = l * (1.0f - m) + l * c * m;
        r = r * (1.0f - m) + r * c * m;
    }

private:
    double sr = 44100.0, phase = 0.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> freqSm;
    juce::SmoothedValue<float> mixSm;
};

//==============================================================================
// GATED REVERB: an 80s "non-linear" reverb.
//
// Rather than a feedback reverb with a gate on its output (which would chop
// earlier tails whenever a new note re-triggers it), every input sample gets
// its own burst of dense reflections that lasts exactly Time ms and then stops
// dead. The response is a sparse multi-tap FIR over a pre-diffused signal:
//   * Asymmetric L/R: each side has its own random reflection pattern and
//     its own diffusion lengths.
//   * LFO reflection modulation: every tap drifts by up to ~0.5 ms, driven by
//     a quadrature LFO pair with a per-tap phase (cheap, and never repeats
//     audibly).
//   * Post-filtering: 150 Hz high-pass and 7 kHz low-pass on the wet signal.
//   * Hard gate: reflections are flat to the end of the window, then nothing.
//   * Polyphonic tail overlap is inherent: because it is a linear FIR, a new
//     note never shortens the tail of an earlier one.
//   * Time 0 ms still yields a 4 ms micro-burst (the window is 4 ms + Time).
//==============================================================================
class GatedReverb
{
public:
    static constexpr int kTaps = 48;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const int maxD = (int) std::ceil (sampleRate * 1.02) + 8;
        for (auto& c : chans) c.line.prepare (maxD);

        // Pre-diffusion allpasses: different (mutually prime-ish) lengths per side.
        static constexpr float apMsL[4] = { 1.31f, 2.27f, 3.73f, 5.09f };
        static constexpr float apMsR[4] = { 1.47f, 2.53f, 3.41f, 5.63f };
        for (int i = 0; i < 4; ++i)
        {
            chans[0].ap[(size_t) i].prepare ((int) std::lround (apMsL[i] * 0.001 * sampleRate));
            chans[1].ap[(size_t) i].prepare ((int) std::lround (apMsR[i] * 0.001 * sampleRate));
        }

        // Random but repeatable reflection patterns (stratified, so they stay dense).
        for (int c = 0; c < 2; ++c)
        {
            gs::Rng rng (c == 0 ? 0x1234567u : 0x7654321u);
            auto& ch = chans[(size_t) c];
            float norm = 0.0f;
            for (int k = 0; k < kTaps; ++k)
            {
                ch.pos[(size_t) k]   = ((float) k + 0.15f + 0.7f * rng.uni()) / (float) kTaps;
                ch.gain[(size_t) k]  = (rng.uni() < 0.5f ? -1.0f : 1.0f) * (0.65f + 0.35f * rng.uni());
                const float ph       = gs::twoPi * rng.uni();
                const float depth    = (0.15f + 0.35f * rng.uni()) * 0.001f * (float) sampleRate;
                ch.modC[(size_t) k]  = depth * std::cos (ph);
                ch.modS[(size_t) k]  = depth * std::sin (ph);
                norm += ch.gain[(size_t) k] * ch.gain[(size_t) k];
            }
            ch.norm = 1.0f / std::sqrt (norm);
            ch.hp.setHighpass (sampleRate, 150.0, 0.707);
            ch.lp.setLowpass (sampleRate, 7000.0, 0.707);
        }

        timeSm.reset (sampleRate, 0.08);
        mixSm.reset (sampleRate, 0.03);
        lfoInc = 0.37 / sampleRate;
        reset();
    }

    void reset()
    {
        for (auto& c : chans)
        {
            c.line.reset();
            for (auto& a : c.ap) a.reset();
            c.hp.reset(); c.lp.reset();
        }
        lfo = 0.0;
    }

    void setParams (float timeMs, float mix01) { timeSm.setTargetValue (timeMs); mixSm.setTargetValue (mix01); }

    void process (float& l, float& r) noexcept
    {
        const float windowSamples = (4.0f + timeSm.getNextValue()) * 0.001f * (float) sr;
        const float pre = 0.0008f * (float) sr;          // 0.8 ms before the first reflection
        const float m   = mixSm.getNextValue();

        lfo += lfoInc; if (lfo >= 1.0) lfo -= 1.0;
        const float ls = std::sin (gs::twoPi * (float) lfo);
        const float lc = std::cos (gs::twoPi * (float) lfo);

        float in[2]  = { l, r };
        float wet[2] = { 0.0f, 0.0f };
        for (int c = 0; c < 2; ++c)
        {
            auto& ch = chans[(size_t) c];
            float x = in[c];
            for (auto& a : ch.ap) x = a.process (x, 0.6f);
            ch.line.push (x);

            float acc = 0.0f;
            for (int k = 0; k < kTaps; ++k)
            {
                const float d = pre + ch.pos[(size_t) k] * windowSamples
                                + ch.modC[(size_t) k] * ls + ch.modS[(size_t) k] * lc
                                + 1.0f;   // keep >= 1 sample (the newest is read separately by the FIR)
                acc += ch.gain[(size_t) k] * readLinear (ch.line, d);
            }
            wet[c] = ch.lp.process (ch.hp.process (acc * ch.norm * 1.4f));
        }

        float dg, wg;
        gs::equalPower (m, dg, wg);
        l = l * dg + wet[0] * wg;
        r = r * dg + wet[1] * wg;
    }

private:
    struct Allpass
    {
        gs::DelayLine d; int len = 1;
        void prepare (int n) { len = std::max (1, n); d.prepare (len + 2); }
        void reset() { d.reset(); }
        float process (float x, float g) noexcept
        {
            const float z = d.tap (len - 1);
            const float v = x + g * z;
            d.push (v);
            return z - g * v;
        }
    };

    struct Channel
    {
        gs::DelayLine line;
        std::array<Allpass, 4> ap;
        std::array<float, kTaps> pos {}, gain {}, modC {}, modS {};
        float norm = 1.0f;
        gs::Biquad hp, lp;
    };

    static float readLinear (const gs::DelayLine& line, float d) noexcept
    {
        const int i = (int) d;
        const float f = d - (float) i;
        return line.tap (i) + f * (line.tap (i + 1) - line.tap (i));
    }

    double sr = 44100.0, lfo = 0.0, lfoInc = 0.0;
    std::array<Channel, 2> chans;
    juce::SmoothedValue<float> timeSm, mixSm;
};
