#pragma once

#include "Common.h"
#include <juce_dsp/juce_dsp.h>

//==============================================================================
// One distortion stage (the processor owns two: Pre and Post, sharing the same
// Gain / Mode parameters). Runs 2x oversampled with JUCE's polyphase IIR
// half-band filters, the lowest-latency option, set to integer latency
// (a couple of samples). When a stage is switched off it is replaced by a
// matching delay so the overall plugin latency never changes while you play.
//==============================================================================
class Distortion
{
public:
    enum class Mode { Soft = 0, Hard, Rich, Wavefold };

    void prepare (double sampleRate, int maxBlock)
    {
        os = std::make_unique<juce::dsp::Oversampling<float>> (
                 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                 false, true);
        os->initProcessing ((size_t) maxBlock);
        blockLimit = std::max (1, maxBlock);
        latency = (int) std::lround (os->getLatencyInSamples());

        for (auto& d : comp) d.prepare (std::max (1, latency) + 4);
        for (auto& dc : dcb) dc.prepare (sampleRate * 2.0);
        driveSm.reset (sampleRate, 0.03);
        reset();
    }

    void reset()
    {
        if (os) os->reset();
        for (auto& d : comp) d.reset();
        for (auto& dc : dcb) dc.reset();
    }

    int getLatency() const noexcept { return latency; }

    void setParams (float gain01, Mode m) noexcept { driveSm.setTargetValue (gain01); mode = m; }

    void process (juce::AudioBuffer<float>& buf, int numSamples, bool active)
    {
        if (! active)
        {
            // Latency-matched pass-through.
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = buf.getWritePointer (ch);
                for (int i = 0; i < numSamples; ++i)
                {
                    comp[(size_t) ch].push (d[i]);
                    d[i] = comp[(size_t) ch].tap (latency);
                }
            }
            driveSm.skip (numSamples);
            return;
        }

        juce::dsp::AudioBlock<float> whole (buf.getArrayOfWritePointers(), 2, (size_t) numSamples);
        for (int start = 0; start < numSamples; start += blockLimit)
        {
            const int len = std::min (blockLimit, numSamples - start);
            auto sub = whole.getSubBlock ((size_t) start, (size_t) len);
            processChunk (sub);
        }
    }

private:
    void processChunk (juce::dsp::AudioBlock<float>& block)
    {
        auto up = os->processSamplesUp (block);
        const int n = (int) up.getNumSamples();
        auto* L = up.getChannelPointer (0);
        auto* R = up.getChannelPointer (1);

        for (int i = 0; i < n; ++i)
        {
            const float g = (i & 1) ? driveSm.getCurrentValue() : driveSm.getNextValue();
            L[i] = dcb[0].process (shape (L[i], g));
            R[i] = dcb[1].process (shape (R[i], g));
        }

        os->processSamplesDown (block);
    }

    static float fold (float x) noexcept
    {
        // Triangle fold: any excursion beyond +-1 is reflected back across zero.
        float t = (x + 1.0f) * 0.25f;
        t -= std::floor (t);
        return 4.0f * std::abs (t - 0.5f) - 1.0f;
    }

    float shape (float x, float g) const noexcept
    {
        switch (mode)
        {
            case Mode::Soft:
            {
                // Asymmetric tube-style: biased tanh, bias removed (DC blocker cleans the rest).
                const float drive = gs::dbToGain (30.0f * g);
                const float bias  = 0.25f;
                const float y = std::tanh (drive * x + bias) - std::tanh (bias);
                return y * (0.9f / (0.6f + 0.4f * std::sqrt (drive)));
            }
            case Mode::Hard:
            {
                // Fuzz: big gain into a near-hard clip with a tiny knee.
                const float drive = gs::dbToGain (42.0f * g);
                const float v = drive * x;
                const float y = v / std::pow (1.0f + std::pow (std::abs (v), 8.0f), 0.125f);
                return 0.7f * y / (0.7f + 0.3f * std::sqrt (std::min (drive, 30.0f)));
            }
            case Mode::Rich:
            {
                // Even-order emphasis: odd soft clip plus a squared term (2nd harmonic).
                const float drive = gs::dbToGain (26.0f * g);
                const float s = std::tanh (drive * x);
                const float y = s + (0.35f + 0.4f * g) * s * s;
                return 0.8f * y / (0.6f + 0.4f * std::sqrt (drive));
            }
            case Mode::Wavefold:
            {
                // Two cascaded folders; more drive = more folds = more metallic sidebands.
                const float drive = 1.0f + 7.0f * g;
                const float s1 = fold (drive * x);
                const float s2 = fold (s1 * (1.0f + 1.5f * g));
                return 0.75f * (0.5f * s1 + 0.5f * s2);
            }
        }
        return x;
    }

    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    std::array<gs::DelayLine, 2> comp;
    std::array<gs::DCBlocker, 2> dcb;
    juce::SmoothedValue<float> driveSm;
    Mode mode = Mode::Soft;
    int latency = 0, blockLimit = 512;
};
