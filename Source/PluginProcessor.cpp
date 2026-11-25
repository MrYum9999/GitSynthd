#include "PluginProcessor.h"
#include "PluginEditor.h"

GitSynthdProcessor::GitSynthdProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GitSynthdState", createParameterLayout()),
      presets (apvts)
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    p = { get (ParamID::inputGain), get (ParamID::outputGain), get (ParamID::globalMix), get (ParamID::latencyMode),
          get (ParamID::synthOn), get (ParamID::synthMode), get (ParamID::synthMix),
          get (ParamID::interval1), get (ParamID::interval2), get (ParamID::interval2On),
          get (ParamID::modOn), get (ParamID::modDepth), get (ParamID::modMix),
          get (ParamID::distOn), get (ParamID::distPre), get (ParamID::distPost), get (ParamID::distGain), get (ParamID::distMode),
          get (ParamID::crushOn), get (ParamID::crushRate), get (ParamID::crushBits),
          get (ParamID::ringOn), get (ParamID::ringFreq), get (ParamID::ringMix),
          get (ParamID::verbOn), get (ParamID::verbTime), get (ParamID::verbMix), get (ParamID::rackOrder) };
}

GitSynthdProcessor::~GitSynthdProcessor() { cancelPendingUpdate(); }

bool GitSynthdProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

int GitSynthdProcessor::computeLatency (int latencyMode) const
{
    const int align = latencyMode == 1 ? (int) std::lround (kAlignedLatencySeconds * sr) : 0;
    return align + distPre.getLatency() + distPost.getLatency();
}

void GitSynthdProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = std::max (1, samplesPerBlock);

    frontEnd.prepare (sr);
    synth.prepare (sr);
    mod.prepare (sr);
    distPre.prepare (sr, maxBlock);
    distPost.prepare (sr, maxBlock);
    crusher.prepare (sr);
    ring.prepare (sr);
    reverb.prepare (sr);

    const int maxDelay = (int) std::lround (kAlignedLatencySeconds * sr) + distPre.getLatency() + distPost.getLatency() + 8;
    for (int c = 0; c < 2; ++c) { dryDelay[c].prepare (maxDelay); rawDelay[c].prepare (maxDelay); }

    wet.setSize (2, maxBlock, false, true, true);
    dry.setSize (2, maxBlock, false, true, true);

    inGainSm.reset (sr, 0.02);    outGainSm.reset (sr, 0.02);
    globalMixSm.reset (sr, 0.02); synthMixSm.reset (sr, 0.02);
    inGainSm.setCurrentAndTargetValue (gs::dbToGain (p.inputGain->load()));
    outGainSm.setCurrentAndTargetValue (gs::dbToGain (p.outputGain->load()));
    globalMixSm.setCurrentAndTargetValue (p.globalMix->load() * 0.01f);
    synthMixSm.setCurrentAndTargetValue (p.synthMix->load() * 0.01f);

    activeOrder = juce::jlimit (0, 5, (int) p.rackOrder->load());
    orderFadingIn = false;

    const int lm = (int) p.latencyMode->load();
    appliedLatencyMode = lm;
    alignSamples = lm == 1 ? (int) std::lround (kAlignedLatencySeconds * sr) : 0;
    setLatencySamples (computeLatency (lm));
    uiLatencySamples = computeLatency (lm);
}

void GitSynthdProcessor::handleAsyncUpdate()
{
    const int lm = appliedLatencyMode.load();
    setLatencySamples (computeLatency (lm));
    uiLatencySamples = computeLatency (lm);
}

void GitSynthdProcessor::updateParameters()
{
    inGainSm.setTargetValue (gs::dbToGain (p.inputGain->load()));
    outGainSm.setTargetValue (gs::dbToGain (p.outputGain->load()));
    globalMixSm.setTargetValue (p.globalMix->load() * 0.01f);
    synthMixSm.setTargetValue (p.synthMix->load() * 0.01f);

    synth.setMode ((SynthEngine::Mode) juce::jlimit (0, 4, (int) p.synthMode->load()));
    synth.setHarmony (p.interval1->load(), p.interval2->load(), p.interval2On->load() > 0.5f);
    mod.setParams (p.modDepth->load() * 0.01f, p.modMix->load() * 0.01f);

    const auto dm = (Distortion::Mode) juce::jlimit (0, 3, (int) p.distMode->load());
    distPre.setParams  (p.distGain->load() * 0.01f, dm);
    distPost.setParams (p.distGain->load() * 0.01f, dm);

    crusher.setParams (p.crushRate->load(), p.crushBits->load());
    ring.setParams (p.ringFreq->load(), p.ringMix->load() * 0.01f);
    reverb.setParams (p.verbTime->load(), p.verbMix->load() * 0.01f);
}

void GitSynthdProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();   // strictly audio in / audio out

    const int n = buffer.getNumSamples();
    if (n > wet.getNumSamples())
    {
        wet.setSize (2, n, false, true, true);   // safety net if a host exceeds its promised block size
        dry.setSize (2, n, false, true, true);
    }

    // Latency mode switches are applied here and reported to the host from the message thread.
    const int lm = (int) p.latencyMode->load();
    if (lm != appliedLatencyMode.load())
    {
        appliedLatencyMode = lm;
        alignSamples = lm == 1 ? (int) std::lround (kAlignedLatencySeconds * sr) : 0;
        triggerAsyncUpdate();
    }
    const int dryDelaySamples = alignSamples + distPre.getLatency() + distPost.getLatency();

    updateParameters();

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const bool synthOn = p.synthOn->load() > 0.5f;
    const bool modOn   = p.modOn->load() > 0.5f;

    auto* inL = buffer.getReadPointer (0);
    auto* inR = buffer.getReadPointer (numIn > 1 ? 1 : 0);
    auto* wL  = wet.getWritePointer (0);
    auto* wR  = wet.getWritePointer (1);
    auto* dL  = dry.getWritePointer (0);
    auto* dR  = dry.getWritePointer (1);

    float peak = 0.0f;

    //--------------------------------------------------------------------------
    // Stage 1: input gain, tracking, resynthesis, synth mix -> wet buffer
    for (int i = 0; i < n; ++i)
    {
        const float g = inGainSm.getNextValue();
        const float l = inL[i] * g, r = inR[i] * g;
        peak = std::max (peak, std::max (std::abs (l), std::abs (r)));

        // Dry bus, delayed by exactly the wet path's latency.
        dryDelay[0].push (l); dryDelay[1].push (r);
        dL[i] = dryDelay[0].tap (dryDelaySamples);
        dR[i] = dryDelay[1].tap (dryDelaySamples);
        rawDelay[0].push (l); rawDelay[1].push (r);
        const float rawL = rawDelay[0].tap (alignSamples);
        const float rawR = rawDelay[1].tap (alignSamples);

        const float sm = synthMixSm.getNextValue();
        if (synthOn)
        {
            frontEnd.process (0.5f * (l + r));
            const float s = synth.process (frontEnd.getHz(), frontEnd.hasLock());

            float sL = s, sR = s;
            if (modOn) mod.process (s, sL, sR);

            float dg, wg;
            gs::equalPower (sm, dg, wg);
            wL[i] = rawL * dg + sL * wg;
            wR[i] = rawR * dg + sR * wg;
        }
        else
        {
            frontEnd.process (0.5f * (l + r));   // keep the tracker warm so re-enabling is instant
            wL[i] = rawL;
            wR[i] = rawR;
        }
    }

    //--------------------------------------------------------------------------
    // Stage 2: Distortion (Pre). At least one stage is always active when enabled.
    const bool distOn   = p.distOn->load() > 0.5f;
    bool preActive      = p.distPre->load() > 0.5f;
    const bool postActive = p.distPost->load() > 0.5f;
    if (distOn && ! preActive && ! postActive) preActive = true;

    distPre.process (wet, n, distOn && preActive);

    //--------------------------------------------------------------------------
    // Stage 3: reorderable rack. A change of order fades the rack output out
    // over one block, switches, then fades back in over the next block.
    const int wantedOrder = juce::jlimit (0, 5, (int) p.rackOrder->load());
    const bool fadingOut  = wantedOrder != activeOrder;
    const auto& order     = Rack::orders[(size_t) activeOrder];
    const bool on[3] = { p.crushOn->load() > 0.5f, p.ringOn->load() > 0.5f, p.verbOn->load() > 0.5f };

    for (int i = 0; i < n; ++i)
    {
        float l = wL[i], r = wR[i];
        for (int slot = 0; slot < 3; ++slot)
        {
            const int m = order[(size_t) slot];
            if (! on[m]) continue;
            if (m == Rack::Bitcrush)     crusher.process (l, r);
            else if (m == Rack::RingMod) ring.process (l, r);
            else                         reverb.process (l, r);
        }

        if (fadingOut || orderFadingIn)
        {
            const float t = (float) (i + 1) / (float) n;
            const float g = fadingOut ? 1.0f - t : t;
            l *= g; r *= g;
        }
        wL[i] = l; wR[i] = r;
    }

    if (fadingOut) { activeOrder = wantedOrder; orderFadingIn = true; }
    else             orderFadingIn = false;

    //--------------------------------------------------------------------------
    // Stage 4: Distortion (Post)
    distPost.process (wet, n, distOn && postActive);

    //--------------------------------------------------------------------------
    // Stage 5: global dry/wet (latency-aligned) and output gain
    auto* outL = buffer.getWritePointer (0);
    auto* outR = numOut > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        float dg, wg;
        gs::equalPower (globalMixSm.getNextValue(), dg, wg);
        const float og = outGainSm.getNextValue();

        const float l = (dL[i] * dg + wL[i] * wg) * og;
        const float r = (dR[i] * dg + wR[i] * wg) * og;
        if (outR != nullptr) { outL[i] = l; outR[i] = r; }
        else                   outL[i] = 0.5f * (l + r);
    }
    for (int ch = 2; ch < numOut; ++ch) buffer.clear (ch, 0, n);

    uiInputLevel = peak;
    uiHz   = frontEnd.getHz();
    uiLock = frontEnd.hasLock();
    uiGate = frontEnd.isGateOpen();
    uiSynthEnv = synth.getEnvelope();
}

//==============================================================================
void GitSynthdProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", presets.getCurrentName(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void GitSynthdProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            presets.setCurrentName (tree.getProperty ("presetName").toString());
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* GitSynthdProcessor::createEditor() { return new GitSynthdEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GitSynthdProcessor(); }
