#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

//==============================================================================
// Every automatable value lives in the AudioProcessorValueTreeState, so host
// sessions, undo and .preset files all capture exactly the same state:
// engine selection, harmony intervals, rack order, module bypasses, Pre/Post
// distortion flags, every continuous control and the gain / mix staging.
//==============================================================================
namespace ParamID
{
    #define GS_PARAM(name) inline constexpr const char* name = #name;
    GS_PARAM (inputGain)  GS_PARAM (outputGain) GS_PARAM (globalMix) GS_PARAM (latencyMode)
    GS_PARAM (synthOn)    GS_PARAM (synthMode)  GS_PARAM (synthMix)
    GS_PARAM (interval1)  GS_PARAM (interval2)  GS_PARAM (interval2On)
    GS_PARAM (modOn)      GS_PARAM (modDepth)   GS_PARAM (modMix)
    GS_PARAM (distOn)     GS_PARAM (distPre)    GS_PARAM (distPost) GS_PARAM (distGain) GS_PARAM (distMode)
    GS_PARAM (crushOn)    GS_PARAM (crushRate)  GS_PARAM (crushBits)
    GS_PARAM (ringOn)     GS_PARAM (ringFreq)   GS_PARAM (ringMix)
    GS_PARAM (verbOn)     GS_PARAM (verbTime)   GS_PARAM (verbMix)
    GS_PARAM (rackOrder)
    #undef GS_PARAM
}

namespace Rack
{
    enum Module { Bitcrush = 0, RingMod = 1, Reverb = 2 };

    // All 6 serial orders of the three rack modules. The rackOrder parameter is
    // an index into this table, which keeps the order host-automatable and
    // makes it part of every saved state for free.
    inline constexpr std::array<std::array<int, 3>, 6> orders {{
        { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 }
    }};

    inline int indexOf (const std::array<int, 3>& o)
    {
        for (int i = 0; i < 6; ++i)
            if (orders[(size_t) i] == o) return i;
        return 0;
    }

    inline const char* name (int module)
    {
        switch (module) { case Bitcrush: return "BITCRUSH"; case RingMod: return "RING MOD"; default: return "GATED REVERB"; }
    }
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    using P = AudioParameterFloat;
    using B = AudioParameterBool;
    using C = AudioParameterChoice;
    using I = AudioParameterInt;
    AudioProcessorValueTreeState::ParameterLayout l;

    auto pid = [] (const char* id) { return ParameterID { id, 1 }; };
    auto pct = AudioParameterFloatAttributes().withLabel ("%")
                   .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + "%"; });
    auto db  = AudioParameterFloatAttributes().withLabel ("dB")
                   .withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; });
    auto hz  = AudioParameterFloatAttributes().withLabel ("Hz")
                   .withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (roundToInt (v)) + " Hz"; });

    // Gain staging
    l.add (std::make_unique<P> (pid (ParamID::inputGain),  "Input",  NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<P> (pid (ParamID::outputGain), "Output", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<P> (pid (ParamID::globalMix),  "Global Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pct));
    l.add (std::make_unique<C> (pid (ParamID::latencyMode), "Latency", StringArray { "Zero", "Aligned" }, 0));

    // Synth engine
    l.add (std::make_unique<B> (pid (ParamID::synthOn),   "Synth On", true));
    l.add (std::make_unique<C> (pid (ParamID::synthMode), "Engine", StringArray { "Deep", "Vintage", "Solo", "Strings", "Harmony" }, 1));
    l.add (std::make_unique<P> (pid (ParamID::synthMix),  "Synth Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pct));
    auto st = AudioParameterIntAttributes().withLabel ("st")
                  .withStringFromValueFunction ([] (int v, int) { return (v > 0 ? "+" : "") + String (v) + " st"; });
    l.add (std::make_unique<I> (pid (ParamID::interval1), "Interval 1", -24, 24, 7, st));
    l.add (std::make_unique<I> (pid (ParamID::interval2), "Interval 2", -24, 24, 12, st));
    l.add (std::make_unique<B> (pid (ParamID::interval2On), "Interval 2 On", false));

    // Synth modulation
    l.add (std::make_unique<B> (pid (ParamID::modOn),    "Mod On", true));
    l.add (std::make_unique<P> (pid (ParamID::modDepth), "Mod Depth", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f, pct));
    l.add (std::make_unique<P> (pid (ParamID::modMix),   "Mod Mix",   NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, pct));

    // Distortion
    l.add (std::make_unique<B> (pid (ParamID::distOn),   "Dist On", false));
    l.add (std::make_unique<B> (pid (ParamID::distPre),  "Dist Pre", true));
    l.add (std::make_unique<B> (pid (ParamID::distPost), "Dist Post", false));
    l.add (std::make_unique<P> (pid (ParamID::distGain), "Dist Gain", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f, pct));
    l.add (std::make_unique<C> (pid (ParamID::distMode), "Dist Mode", StringArray { "Soft", "Hard", "Rich", "Wavefold" }, 0));

    // Bitcrush
    l.add (std::make_unique<B> (pid (ParamID::crushOn),   "Crush On", false));
    l.add (std::make_unique<P> (pid (ParamID::crushRate), "Crush", NormalisableRange<float> (441.0f, 44100.0f, 0.0f, 0.25f), 11025.0f, hz));
    l.add (std::make_unique<P> (pid (ParamID::crushBits), "Resolution", NormalisableRange<float> (1.0f, 24.0f, 0.01f), 8.0f,
                                AudioParameterFloatAttributes().withLabel ("bits")
                                    .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " bits"; })));

    // Ring mod
    l.add (std::make_unique<B> (pid (ParamID::ringOn),   "Ring On", false));
    l.add (std::make_unique<P> (pid (ParamID::ringFreq), "Ring Freq", NormalisableRange<float> (20.0f, 5000.0f, 0.0f, 0.3f), 440.0f, hz));
    l.add (std::make_unique<P> (pid (ParamID::ringMix),  "Ring Mix",  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, pct));

    // Gated reverb
    l.add (std::make_unique<B> (pid (ParamID::verbOn),   "Reverb On", false));
    l.add (std::make_unique<P> (pid (ParamID::verbTime), "Reverb Time", NormalisableRange<float> (0.0f, 1000.0f, 1.0f, 0.6f), 250.0f,
                                AudioParameterFloatAttributes().withLabel ("ms")
                                    .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })));
    l.add (std::make_unique<P> (pid (ParamID::verbMix),  "Reverb Mix",  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f, pct));

    // Rack order
    l.add (std::make_unique<I> (pid (ParamID::rackOrder), "Rack Order", 0, 5, 0,
                                AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
                                {
                                    const auto& o = Rack::orders[(size_t) juce::jlimit (0, 5, v)];
                                    return String (Rack::name (o[0])) + " > " + Rack::name (o[1]) + " > " + Rack::name (o[2]);
                                })));
    return l;
}
