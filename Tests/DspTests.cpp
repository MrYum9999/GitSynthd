// Offline checks for GitSynth'd!  (build with -DGITSYNTHD_TESTS=ON)
//  1. Pitch tracker accuracy and lock time across E2..C6 at several sample rates
//  2. Vibrato following
//  3. Whole-plugin stress: every engine, distortion mode, rack order, latency
//     mode, sample rate and odd block sizes; checks for NaN/Inf and runaway levels
//  4. CPU cost estimate
//  5. Renders the editor to a PNG

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>

static float guitarLike (double& ph, double hz, double sr, gs::Rng& rng)
{
    ph += hz / sr; if (ph >= 1.0) ph -= 1.0;
    double s = 0.0;
    for (int k = 1; k <= 12; ++k)
        if (hz * k < sr * 0.45)
            s += std::sin (2.0 * juce::MathConstants<double>::pi * k * ph) / (k * std::sqrt ((double) k));
    return (float) (0.35 * s) + 0.002f * rng.bi();
}

static int testTracker()
{
    int failures = 0;
    std::printf ("\n== Pitch tracker (zero lookahead) ==\n");
    std::printf ("%7s %9s %10s %10s %11s\n", "SR", "note Hz", "mean ct", "max ct", "lock ms");
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    const double notes[] = { 82.41, 110.0, 164.81, 220.0, 329.63, 440.0, 659.26, 1046.5 };

    for (double sr : rates)
        for (double hz : notes)
        {
            PitchFrontEnd fe; fe.prepare (sr);
            gs::Rng rng (5);
            double ph = 0.0;
            const int pre = (int) (0.05 * sr), len = (int) (0.5 * sr);
            for (int i = 0; i < pre; ++i) fe.process (0.0f);

            int lockAt = -1; double sumErr = 0.0, maxErr = 0.0; int count = 0;
            for (int i = 0; i < len; ++i)
            {
                const float env = std::min (1.0f, (float) i / (float) (0.002 * sr));
                fe.process (env * guitarLike (ph, hz, sr, rng));
                if (fe.hasLock() && lockAt < 0) lockAt = i;
                if (i > (int) (0.1 * sr) && fe.hasLock())
                {
                    const double err = std::abs (1200.0 * std::log2 (fe.getHz() / hz));
                    sumErr += err; maxErr = std::max (maxErr, err); ++count;
                }
            }
            const double mean = count ? sumErr / count : 999.0;
            const double lockMs = lockAt < 0 ? -1.0 : 1000.0 * lockAt / sr;
            std::printf ("%7.0f %9.2f %10.2f %10.2f %11.2f\n", sr, hz, mean, maxErr, lockMs);
            if (mean > 5.0 || maxErr > 15.0 || lockAt < 0 || lockMs > 2.6 * 1000.0 / hz + 3.0) ++failures;
        }
    return failures;
}

static int testVibrato()
{
    std::printf ("\n== Vibrato / bend following (A3, 5.5 Hz, +-50 cents) ==\n");
    const double sr = 48000.0;
    PitchFrontEnd fe; fe.prepare (sr);
    gs::Rng rng (9);
    double ph = 0.0, sumErr = 0.0, maxErr = 0.0; int count = 0;
    for (int i = 0; i < (int) sr; ++i)
    {
        const double t = i / sr;
        const double hz = 220.0 * std::exp2 (0.5 / 12.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t));
        fe.process (guitarLike (ph, hz, sr, rng));
        if (t > 0.1 && fe.hasLock())
        {
            // Compare against the true pitch ~4 ms earlier (tracker group delay).
            const double tl = t - 0.004;
            const double hzl = 220.0 * std::exp2 (0.5 / 12.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * tl));
            const double err = std::abs (1200.0 * std::log2 (fe.getHz() / hzl));
            sumErr += err; maxErr = std::max (maxErr, err); ++count;
        }
    }
    std::printf ("mean err %.2f ct, max err %.2f ct (vs pitch 4 ms earlier)\n", sumErr / count, maxErr);
    return (sumErr / count) < 10.0 ? 0 : 1;
}

static void setParam (GitSynthdProcessor& p, const char* id, float realValue)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (realValue));
}

static int testPlugin()
{
    std::printf ("\n== Whole-plugin stress ==\n");
    int failures = 0;
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    const int blocks[] = { 32, 441, 512, 1024 };

    for (double sr : rates)
        for (int bs : blocks)
        {
            GitSynthdProcessor p;
            p.setPlayConfigDetails (2, 2, sr, bs);
            p.prepareToPlay (sr, bs);
            juce::AudioBuffer<float> buf (2, bs);
            juce::MidiBuffer midi;
            gs::Rng rng (3);
            double ph = 0.0;
            float peak = 0.0f; bool bad = false;

            for (int engine = 0; engine < 5; ++engine)
                for (int dm = 0; dm < 4; ++dm)
                    for (int order = 0; order < 6; order += (engine == 0 ? 1 : 5))
                    {
                        setParam (p, ParamID::synthMode, (float) engine);
                        setParam (p, ParamID::distMode, (float) dm);
                        setParam (p, ParamID::rackOrder, (float) order);
                        setParam (p, ParamID::distOn, 1.0f);
                        setParam (p, ParamID::distPre, 1.0f);
                        setParam (p, ParamID::distPost, (float) (dm & 1));
                        setParam (p, ParamID::distGain, 100.0f);
                        setParam (p, ParamID::crushOn, 1.0f);  setParam (p, ParamID::crushBits, 1.0f + 23.0f * rng.uni());
                        setParam (p, ParamID::crushRate, 441.0f + 43000.0f * rng.uni());
                        setParam (p, ParamID::ringOn, 1.0f);   setParam (p, ParamID::ringFreq, 20.0f + 4980.0f * rng.uni());
                        setParam (p, ParamID::verbOn, 1.0f);   setParam (p, ParamID::verbTime, 1000.0f * rng.uni());
                        setParam (p, ParamID::verbMix, 100.0f);
                        setParam (p, ParamID::interval2On, 1.0f);
                        setParam (p, ParamID::latencyMode, (float) ((engine + dm) & 1));
                        setParam (p, ParamID::inputGain, 24.0f);
                        setParam (p, ParamID::outputGain, 0.0f);

                        const double hz = 82.41 * std::exp2 (rng.uni() * 3.6);
                        for (int b = 0; b < (int) (0.12 * sr / bs) + 1; ++b)
                        {
                            for (int i = 0; i < bs; ++i)
                            {
                                const float s = guitarLike (ph, hz, sr, rng);
                                buf.setSample (0, i, s); buf.setSample (1, i, s);
                            }
                            p.processBlock (buf, midi);
                            for (int ch = 0; ch < 2; ++ch)
                                for (int i = 0; i < bs; ++i)
                                {
                                    const float v = buf.getSample (ch, i);
                                    if (! std::isfinite (v)) bad = true;
                                    peak = std::max (peak, std::abs (v));
                                }
                        }
                    }
            std::printf ("sr %6.0f  block %5d  peak %6.2f  %s  latency %d smp\n", sr, bs, peak,
                         bad ? "NaN/Inf!" : "finite", p.getLatencySamples());
            if (bad || peak > 8.0f) ++failures;
        }
    return failures;
}

static int testLatencyAndNulling()
{
    std::printf ("\n== Latency report & dry alignment ==\n");
    int failures = 0;
    for (int mode = 0; mode < 2; ++mode)
    {
        GitSynthdProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 256);
        setParam (p, ParamID::latencyMode, (float) mode);
        p.prepareToPlay (48000.0, 256);
        setParam (p, ParamID::globalMix, 0.0f);   // dry only
        juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer midi;
        int impulseAt = -1;
        for (int b = 0; b < 20 && impulseAt < 0; ++b)
        {
            buf.clear();
            if (b == 2) { buf.setSample (0, 10, 1.0f); buf.setSample (1, 10, 1.0f); }
            p.processBlock (buf, midi);
            for (int i = 0; i < 256; ++i)
                if (std::abs (buf.getSample (0, i)) > 0.5f) { impulseAt = b * 256 + i - (2 * 256 + 10); break; }
        }
        std::printf ("mode %-8s reported %3d smp (%.2f ms), measured dry delay %3d smp\n",
                     mode ? "Aligned" : "Zero", p.getLatencySamples(), p.getLatencySamples() / 48.0, impulseAt);
        if (impulseAt != p.getLatencySamples()) ++failures;
    }
    return failures;
}

static void cpuEstimate()
{
    std::printf ("\n== CPU estimate (everything on, 48 kHz, 128-sample blocks) ==\n");
    GitSynthdProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 128);
    p.prepareToPlay (48000.0, 128);
    for (auto* id : { ParamID::distOn, ParamID::distPost, ParamID::crushOn, ParamID::ringOn, ParamID::verbOn, ParamID::interval2On })
        setParam (p, id, 1.0f);
    setParam (p, ParamID::synthMode, 4.0f);
    setParam (p, ParamID::verbTime, 1000.0f);
    juce::AudioBuffer<float> buf (2, 128); juce::MidiBuffer midi;
    gs::Rng rng (1); double ph = 0.0;
    const int blocks = (int) (10.0 * 48000.0 / 128);
    const auto t0 = juce::Time::getHighResolutionTicks();
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < 128; ++i) { const float s = guitarLike (ph, 196.0, 48000.0, rng); buf.setSample (0, i, s); buf.setSample (1, i, s); }
        p.processBlock (buf, midi);
    }
    const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    std::printf ("10 s of audio in %.3f s  ->  %.1f%% of one core\n", secs, secs * 10.0);
}

static void renderEditor (const char* path)
{
    GitSynthdProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 256);
    p.prepareToPlay (48000.0, 256);
    setParam (p, ParamID::distOn, 1.0f);
    setParam (p, ParamID::verbOn, 1.0f);
    setParam (p, ParamID::crushOn, 1.0f);
    setParam (p, ParamID::rackOrder, 3.0f);
    p.uiHz = 196.8f; p.uiLock = true; p.uiGate = true; p.uiInputLevel = 0.3f;

    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    static_cast<GitSynthdEditor*> (ed.get())->refreshDisplays();
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File f (path);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("\nEditor snapshot written to %s\n", path);
}

static void dumpRaw (double hz)
{
    double sr = 48000; PitchTracker t; t.prepare (sr); double ph = 0; int c = 0;
    gs::Rng rng (5);
    for (int i = 0; i < (int) (0.3 * sr); ++i)
        if (t.push (guitarLike (ph, hz, sr, rng)) && i > 0.05 * sr && c++ < 60)
            std::printf ("%.2f%s ", t.getHz(), t.isVoiced() ? "" : "u");
    std::printf ("\n");
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 2 && juce::String (argv[1]) == "raw") { dumpRaw (juce::String (argv[2]).getDoubleValue()); return 0; }
    int failures = 0;
    failures += testTracker();
    failures += testVibrato();
    failures += testLatencyAndNulling();
    failures += testPlugin();
    cpuEstimate();
    if (argc > 1) renderEditor (argv[1]);
    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
