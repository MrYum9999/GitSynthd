#pragma once

#include "Common.h"

//==============================================================================
// Causal, low-latency monophonic pitch tracker.
//
// Design goals (the plugin's latency brief is "as small as possible"):
//   * No lookahead. The tracker only ever looks at samples it already has,
//     so the plugin reports zero latency to the host.
//   * The analysis is a YIN-style cumulative-mean-normalised difference
//     function, but each candidate lag tau is compared over a window of only
//     max(tau, ~2 ms) samples instead of a fixed long window. A note can
//     therefore be recognised after roughly two of its own periods:
//         E2 (82 Hz)  ~24 ms,  E3 ~12 ms,  A4 ~4.5 ms,  C6 ~2 ms
//     which is about the physical minimum for any period-based detector.
//   * Analysis runs on a ~16 kHz decimated copy of the input (more than enough
//     bandwidth for an 82 Hz - 1.05 kHz fundamental) and re-estimates about
//     every 1 ms, so pitch bends, slides and vibrato are followed continuously.
//   * Sub-sample accuracy comes from parabolic interpolation of the minimum.
//==============================================================================
class PitchTracker
{
public:
    static constexpr float kMinHz = 75.0f;    // a little below E2 for detuned / drop tunings
    static constexpr float kMaxHz = 1300.0f;  // a little above C6

    void prepare (double sampleRate)
    {
        sr     = sampleRate;
        decim  = std::max (1, (int) std::lround (sampleRate / 16000.0));
        asr    = sampleRate / decim;

        // 4th-order Butterworth anti-alias (two biquads) ahead of decimation.
        const double fc = std::min (5500.0, 0.40 * asr);
        aa1.setLowpass (sampleRate, fc, 0.5412);
        aa2.setLowpass (sampleRate, fc, 1.3066);

        tauMin = std::max (2, (int) std::floor (asr / kMaxHz));
        tauMax = (int) std::ceil (asr / kMinHz);
        wMin   = std::max (8, (int) std::lround (asr * 0.002));
        hop    = std::max (4, (int) std::lround (asr * 0.001));

        ring.prepare (2 * tauMax + 8);
        d.assign ((size_t) tauMax + 3, 0.0f);
        raw.assign ((size_t) tauMax + 3, 0.0f);
        reset();
    }

    void reset()
    {
        aa1.reset(); aa2.reset();
        ring.reset();
        decimCount = hopCount = 0;
        filled = 0;
        lastHz = 0.0f; voiced = false; confidence = 0.0f;
    }

    // Feed one full-rate sample. Returns true when a fresh estimate is ready.
    bool push (float x) noexcept
    {
        const float y = aa2.process (aa1.process (x));
        if (++decimCount < decim) return false;
        decimCount = 0;

        ring.push (y);
        if (filled < 2 * tauMax + 4) ++filled;

        if (++hopCount < hop) return false;
        hopCount = 0;
        analyse();
        return true;
    }

    float getHz()         const noexcept { return lastHz; }
    bool  isVoiced()      const noexcept { return voiced; }
    float getConfidence() const noexcept { return confidence; }

    // Worst-case time to lock onto the lowest supported note (2 periods of kMinHz).
    double getWorstCaseLockSeconds() const noexcept { return 2.0 / kMinHz; }

private:
    void analyse() noexcept
    {
        // Normalised difference per lag. Window = max(tau, wMin) newest samples
        // against the same span tau samples earlier.
        const int maxTauAvail = std::min (tauMax, (filled - wMin) / 2);
        if (maxTauAvail <= tauMin + 2) { voiced = false; return; }

        float energy = 0.0f;
        for (int j = 0; j < wMin; ++j) { const float s = ring.tap (j); energy += s * s; }
        if (energy / (float) wMin < 1.0e-9f) { voiced = false; confidence = 0.0f; return; }

        float running = 0.0f;
        d[0] = 1.0f;
        for (int tau = 1; tau <= maxTauAvail; ++tau)
        {
            const int w = std::max (tau, wMin);
            float acc = 0.0f;
            for (int j = 0; j < w; ++j)
            {
                const float diff = ring.tap (j) - ring.tap (j + tau);
                acc += diff * diff;
            }
            const float dn = acc / (float) w;          // per-sample difference
            running += dn;
            d[(size_t) tau] = running > 0.0f ? dn * (float) tau / running : 1.0f;   // CMNDF
            raw[(size_t) tau] = dn;
        }

        // Collect every local minimum with its parabolically interpolated position
        // and depth. Interpolated depths matter at high notes, where the true period
        // falls between two analysis samples and neither integer lag looks deep.
        int   nDips = 0;
        float bestGlobal = 1.0e9f;
        int   bestIndex  = 0;
        for (int tau = std::max (tauMin, 2); tau < maxTauAvail && nDips < kMaxDips; ++tau)
        {
            const float a = d[(size_t) tau - 1], b = d[(size_t) tau], c = d[(size_t) tau + 1];
            if (! (b <= a && b < c)) continue;
            const float den = a - 2.0f * b + c;
            float off = 0.0f, val = b;
            if (den > 1.0e-12f)
            {
                off = juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / den);
                val = std::max (0.0f, b - 0.25f * (a - c) * off);
            }
            dipTau[(size_t) nDips] = (float) tau + off;
            dipVal[(size_t) nDips] = val;
            if (val < bestGlobal) { bestGlobal = val; bestIndex = nDips; }
            ++nDips;
        }
        if (nDips == 0) { voiced = false; confidence = 0.0f; return; }

        // Adaptive absolute threshold: take the FIRST dip that is nearly as deep as
        // the deepest one. "First" avoids octave-down errors; "nearly as deep"
        // stops a shallow early dip (from the short one-period window) winning.
        const float threshold = std::min (0.15f, 1.5f * bestGlobal + 0.03f);
        int pick = -1;
        for (int i = 0; i < nDips; ++i)
            if (dipVal[(size_t) i] < threshold) { pick = i; break; }

        if (pick < 0)
        {
            pick = bestIndex;
            if (bestGlobal > 0.35f) { voiced = false; confidence = 1.0f - bestGlobal; return; }
        }

        // Final position from the raw (un-normalised) difference function, which is
        // closer to a true parabola around its minimum than the CMNDF is. This
        // removes most of the interpolation bias on high notes.
        float t = dipTau[(size_t) pick];
        {
            const int ti = juce::jlimit (2, maxTauAvail - 1, (int) std::lround (t));
            const float a = raw[(size_t) ti - 1], b = raw[(size_t) ti], c = raw[(size_t) ti + 1];
            const float den = a - 2.0f * b + c;
            if (b <= a && b <= c && den > 1.0e-12f)
                t = (float) ti + juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / den);
        }
        confidence = juce::jlimit (0.0f, 1.0f, 1.0f - dipVal[(size_t) pick]);
        const float hz = (float) asr / t;
        if (hz < kMinHz * 0.97f || hz > kMaxHz * 1.03f) { voiced = false; return; }

        lastHz = hz;
        voiced = true;
    }

    double sr = 44100.0, asr = 11025.0;
    int decim = 4, tauMin = 8, tauMax = 147, wMin = 22, hop = 11;
    int decimCount = 0, hopCount = 0, filled = 0;
    gs::Biquad aa1, aa2;
    gs::DelayLine ring;
    std::vector<float> d, raw;
    static constexpr int kMaxDips = 64;
    std::array<float, kMaxDips> dipTau {}, dipVal {};
    float lastHz = 0.0f, confidence = 0.0f;
    bool voiced = false;
};

//==============================================================================
// Turns raw tracker estimates into a smooth, continuous pitch + gate stream.
//  * Gate: fast envelope follower with hysteresis. Input Gain therefore acts
//    as the tracker's sensitivity trim, exactly as the spec describes.
//  * Pitch: glides in the log domain (no semitone quantisation) so bends,
//    slides and vibrato pass straight through. Single-estimate leaps larger
//    than 7 semitones must be confirmed by the next estimate (about 1 ms) to
//    reject octave errors without slowing down real note changes.
//==============================================================================
class PitchFrontEnd
{
public:
    static constexpr float kGateOpenDb  = -50.0f;
    static constexpr float kGateCloseDb = -58.0f;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        tracker.prepare (sampleRate);
        envAtt = (float) std::exp (-1.0 / (0.0008 * sampleRate));
        envRel = (float) std::exp (-1.0 / (0.040 * sampleRate));
        glide.setTime (2.5f, sampleRate);
        openLin  = gs::dbToGain (kGateOpenDb);
        closeLin = gs::dbToGain (kGateCloseDb);
        reset();
    }

    void reset()
    {
        tracker.reset();
        env = 0.0f; gate = false; hasPitch = false;
        logHz = std::log2 (220.0f); glide.reset (logHz);
        pendingLog = 0.0f; pending = false;
    }

    // Call once per sample with the (input-gained, mono) signal.
    void process (float x) noexcept
    {
        const float a = std::abs (x);
        env = a > env ? envAtt * (env - a) + a : envRel * (env - a) + a;

        if (! gate && env > openLin)        gate = true;
        else if (gate && env < closeLin)    { gate = false; hasPitch = false; }

        if (tracker.push (x) && gate && tracker.isVoiced())
        {
            const float newLog = std::log2 (tracker.getHz());

            if (! hasPitch)
            {
                // First lock of a new note: jump straight there, no glide.
                logHz = newLog; glide.reset (newLog); hasPitch = true; pending = false;
            }
            else if (std::abs (newLog - logHz) > 7.0f / 12.0f)
            {
                if (pending && std::abs (newLog - pendingLog) < 1.0f / 12.0f)
                {
                    logHz = newLog; glide.reset (newLog); pending = false;  // confirmed leap
                }
                else { pending = true; pendingLog = newLog; }
            }
            else { logHz = newLog; pending = false; }
        }

        smoothed = glide.process (logHz);
    }

    bool  isGateOpen() const noexcept { return gate; }
    bool  hasLock()    const noexcept { return gate && hasPitch; }
    float getHz()      const noexcept { return std::exp2 (smoothed); }
    float getEnvelope() const noexcept { return env; }

private:
    double sr = 44100.0;
    PitchTracker tracker;
    float env = 0.0f, envAtt = 0.0f, envRel = 0.0f, openLin = 0.0f, closeLin = 0.0f;
    bool gate = false, hasPitch = false, pending = false;
    float logHz = 7.78f, smoothed = 7.78f, pendingLog = 0.0f;
    gs::OnePole glide;
};
