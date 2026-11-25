#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

//==============================================================================
// 80s analogue polysynth faceplate: walnut end cheeks, black panel with
// silkscreened section headers and coloured stripes, cream-capped knobs with
// scale ticks, rubber push buttons with red LEDs, a red LED tuner readout and
// a green vacuum-fluorescent preset display.
//==============================================================================
namespace Retro
{
    const juce::Colour panel      { 0xff1d1c1b };
    const juce::Colour panelLight { 0xff2a2826 };
    const juce::Colour silk       { 0xffece6d6 };   // silkscreen off-white
    const juce::Colour silkDim    { 0xff9d978a };
    const juce::Colour orange     { 0xffe8711f };
    const juce::Colour red        { 0xffd8322a };
    const juce::Colour yellow     { 0xffe9b52f };
    const juce::Colour cream      { 0xffe7dcc2 };
    const juce::Colour ledRed     { 0xffff3b26 };
    const juce::Colour vfd        { 0xff6effd2 };
    const juce::Colour amber      { 0xffffb238 };

    juce::Font font (float h, bool bold = true, bool italic = false);
}

//==============================================================================
class RetroLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RetroLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

//==============================================================================
// Rubber key with a red LED above it.
class LedButton : public juce::Button
{
public:
    LedButton (const juce::String& buttonText, juce::Colour led = Retro::ledRed);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    void setShowLed (bool shouldShow);
private:
    juce::Colour ledColour;
    bool showLed = true;
};

//==============================================================================
// Knob + silkscreen label + small amber value readout.
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& label);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;
private:
    juce::String label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

//==============================================================================
// Toggle bound to a bool parameter.
class ParamToggle : public LedButton
{
public:
    ParamToggle (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& buttonText,
                 juce::Colour led = Retro::ledRed);
    juce::RangedAudioParameter* param = nullptr;
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
};

//==============================================================================
// Row of radio LED buttons bound to a choice parameter.
class ChoiceButtons : public juce::Component
{
public:
    ChoiceButtons (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::StringArray& labels);
    void resized() override;
private:
    void select (int index);
    juce::RangedAudioParameter* param = nullptr;
    juce::OwnedArray<LedButton> buttons;
    std::unique_ptr<juce::ParameterAttachment> att;
};

//==============================================================================
class GitSynthdEditor;

// One module of the reorderable rack. Drag its header onto another module to
// move it there in the signal chain.
class RackModulePanel : public juce::Component,
                        public juce::DragAndDropTarget
{
public:
    RackModulePanel (GitSynthdEditor&, int moduleId);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { hover = true;  repaint(); }
    void itemDragExit  (const SourceDetails&) override { hover = false; repaint(); }
    void itemDropped   (const SourceDetails&) override;

    const int moduleId;
    int slotNumber = 0;
private:
    GitSynthdEditor& editor;
    std::unique_ptr<ParamToggle> onButton;
    std::unique_ptr<Knob> knobA, knobB;
    bool hover = false, dragging = false;
    juce::Rectangle<int> header;
};

//==============================================================================
class GitSynthdEditor : public juce::AudioProcessorEditor,
                        public juce::DragAndDropContainer,
                        private juce::Timer
{
public:
    explicit GitSynthdEditor (GitSynthdProcessor&);
    ~GitSynthdEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void moveModule (int moduleId, int ontoModuleId);
    void refreshDisplays() { timerCallback(); }
    GitSynthdProcessor& proc;

private:
    void timerCallback() override;
    void layoutRack (bool animate);
    void paintStatic (juce::Graphics&);
    void paintMeter (juce::Graphics&);
    void drawSection (juce::Graphics&, juce::Rectangle<int>, const juce::String& title, juce::Colour stripe) const;
    void drawTuner (juce::Graphics&, juce::Rectangle<int>) const;
    void drawVfd (juce::Graphics&, juce::Rectangle<int>) const;
    void enforceDistortionStage (bool preWasClicked);
    void savePresetDialog();

    RetroLookAndFeel lnf;

    Knob inputKnob, synthMixKnob, int1Knob, int2Knob, modDepthKnob, modMixKnob, distGainKnob, globalMixKnob, outputKnob;
    ParamToggle synthOn, int2On, modOn, distOn, distPre, distPost, latency;
    ChoiceButtons engineButtons, distModeButtons;
    LedButton prevPreset { "<" }, nextPreset { ">" }, savePreset { "SAVE" };

    std::array<std::unique_ptr<RackModulePanel>, 3> rack;
    int shownOrder = -1;

    juce::Rectangle<int> rHeader, rTuner, rVfd, rInput, rSynth, rMod, rDist, rRack, rOut, rRackArea;
    std::unique_ptr<juce::AlertWindow> saveWindow;

    // display state copied from the processor by the timer
    float dispHz = 0.0f, dispLevel = 0.0f;
    bool dispLock = false, dispGate = false;
    juce::String lastName;
    juce::Image background;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GitSynthdEditor)
};
