#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
// Discrete .preset files (XML) stored in
//   Windows: Documents\GitSynthd\Presets
//   macOS:   ~/Documents/GitSynthd/Presets
// Each file is a full snapshot of the parameter tree: engine, intervals, rack
// order, module bypasses, Pre/Post distortion flags, all continuous controls
// and the gain / mix staging. A handful of factory presets are written the
// first time the folder is empty.
//==============================================================================
class PresetManager
{
public:
    static constexpr const char* extension = ".preset";

    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : state (s)
    {
        folder().createDirectory();
        if (getPresetFiles().isEmpty())
            writeFactoryPresets();
    }

    static juce::File folder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("GitSynthd").getChildFile ("Presets");
    }

    juce::Array<juce::File> getPresetFiles() const
    {
        auto files = folder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension);
        files.sort();
        return files;
    }

    bool save (const juce::String& name)
    {
        const auto clean = juce::File::createLegalFileName (name.trim());
        if (clean.isEmpty()) return false;
        auto xml = state.copyState().createXml();
        if (xml == nullptr) return false;
        xml->setAttribute ("presetName", clean);
        const auto f = folder().getChildFile (clean + extension);
        if (! xml->writeTo (f)) return false;
        current = clean;
        return true;
    }

    bool load (const juce::File& f)
    {
        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr || ! xml->hasTagName (state.state.getType())) return false;
        state.replaceState (juce::ValueTree::fromXml (*xml));
        current = f.getFileNameWithoutExtension();
        return true;
    }

    void step (int delta)
    {
        const auto files = getPresetFiles();
        if (files.isEmpty()) return;
        int idx = 0;
        for (int i = 0; i < files.size(); ++i)
            if (files[i].getFileNameWithoutExtension() == current) { idx = i; break; }
        idx = (idx + delta + files.size()) % files.size();
        load (files[idx]);
    }

    juce::String getCurrentName() const { return current.isEmpty() ? juce::String ("INIT") : current; }
    void setCurrentName (const juce::String& n) { current = n; }

private:
    void writeFactoryPresets()
    {
        struct Factory { const char* name; std::initializer_list<std::pair<const char*, float>> values; };

        // Values are in real units (dB, %, Hz, ms, choice index, 0/1).
        const Factory factory[] = {
            { "01 Init", {} },
            { "02 Deep Sub Bass", { { "synthMode", 0 }, { "synthMix", 100 }, { "modOn", 0 }, { "globalMix", 100 } } },
            { "03 Vintage Lead",  { { "synthMode", 1 }, { "modDepth", 40 }, { "modMix", 55 }, { "verbOn", 1 }, { "verbTime", 180 }, { "verbMix", 25 } } },
            { "04 Solo Fuzz",     { { "synthMode", 2 }, { "distOn", 1 }, { "distMode", 1 }, { "distGain", 55 }, { "distPre", 1 } } },
            { "05 Gated Strings", { { "synthMode", 3 }, { "verbOn", 1 }, { "verbTime", 420 }, { "verbMix", 45 }, { "modDepth", 60 }, { "modMix", 70 } } },
            { "06 Fifths Harmony",{ { "synthMode", 4 }, { "interval1", 7 }, { "interval2", 12 }, { "interval2On", 1 }, { "synthMix", 80 } } },
            { "07 Robot Ring",    { { "synthMode", 2 }, { "ringOn", 1 }, { "ringFreq", 310 }, { "ringMix", 70 }, { "crushOn", 1 }, { "crushRate", 6000 }, { "crushBits", 6 } } },
            { "08 Fold Crush FX", { { "synthOn", 0 }, { "distOn", 1 }, { "distMode", 3 }, { "distGain", 45 }, { "distPost", 1 }, { "distPre", 0 },
                                    { "crushOn", 1 }, { "crushRate", 4000 }, { "crushBits", 10 }, { "verbOn", 1 }, { "verbTime", 90 }, { "rackOrder", 4 } } },
        };

        const auto saved = state.copyState();
        for (const auto& p : factory)
        {
            for (auto* param : state.processor.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
                    rp->setValueNotifyingHost (rp->getDefaultValue());

            for (const auto& [id, v] : p.values)
                if (auto* rp = state.getParameter (id))
                    rp->setValueNotifyingHost (rp->convertTo0to1 (v));

            save (p.name);
        }
        state.replaceState (saved);
        current = {};
    }

    juce::AudioProcessorValueTreeState& state;
    juce::String current;
};
