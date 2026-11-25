#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "PresetManager.h"
#include "DSP/PitchTracker.h"
#include "DSP/SynthEngine.h"
#include "DSP/SynthModulation.h"
#include "DSP/Distortion.h"
#include "DSP/RackModules.h"

//==============================================================================
// GitSynth'd!  -  monophonic audio-to-synth resynthesiser + multi-effects.
// Audio in -> audio out only. No MIDI in or out.
//
//  In -> Input Gain -+-> [tracker] -> Synth + Mod -> Synth Mix -> Dist(Pre)
//                    |                                  ^           |
//                    |            raw input ------------+           v
//                    |                         [ Crush / Ring / Reverb, any order ]
//                    |                                              |
//                    +---------------- dry bus ---------> Global Mix <- Dist(Post)
//                                                              |
//                                                         Output Gain -> Out
//==============================================================================
class GitSynthdProcessor : public juce::AudioProcessor,
                           private juce::AsyncUpdater
{
public:
    GitSynthdProcessor();
    ~GitSynthdProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "GitSynth'd!"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets;

    // Read by the editor for its displays (written once per block).
    std::atomic<float> uiHz { 0.0f }, uiInputLevel { 0.0f }, uiSynthEnv { 0.0f };
    std::atomic<bool>  uiLock { false }, uiGate { false };
    std::atomic<int>   uiLatencySamples { 0 };

    // Lookahead used by "Aligned" latency mode (the original 12 ms spec).
    static constexpr double kAlignedLatencySeconds = 0.012;

private:
    void handleAsyncUpdate() override;
    int  computeLatency (int latencyMode) const;
    void updateParameters();

    struct Cached
    {
        std::atomic<float>* inputGain, *outputGain, *globalMix, *latencyMode,
                          * synthOn, *synthMode, *synthMix, *interval1, *interval2, *interval2On,
                          * modOn, *modDepth, *modMix,
                          * distOn, *distPre, *distPost, *distGain, *distMode,
                          * crushOn, *crushRate, *crushBits,
                          * ringOn, *ringFreq, *ringMix,
                          * verbOn, *verbTime, *verbMix, *rackOrder;
    } p {};

    double sr = 44100.0;
    int maxBlock = 512;

    PitchFrontEnd   frontEnd;
    SynthEngine     synth;
    SynthModulation mod;
    Distortion      distPre, distPost;
    Bitcrusher      crusher;
    RingModulator   ring;
    GatedReverb     reverb;

    gs::DelayLine dryDelay[2], rawDelay[2];
    juce::AudioBuffer<float> wet, dry;

    juce::SmoothedValue<float> inGainSm, outGainSm, globalMixSm, synthMixSm;

    int activeOrder = 0;            // rack order currently running on the audio thread
    bool orderFadingIn = false;

    std::atomic<int> appliedLatencyMode { -1 };
    int alignSamples = 0;           // 0 in Zero mode, ~12 ms in Aligned mode

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GitSynthdProcessor)
};
