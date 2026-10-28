#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// Potard concentrique RANGE : chapeau central = GAIN, jupe moletee = ONSET.
// Les corps des potards (et leurs reflets) font partie du fond : seuls les reperes peints tournent.
//==============================================================================
class ConcentricKnob : public juce::Component
{
public:
    ConcentricKnob (juce::RangedAudioParameter& gain, juce::RangedAudioParameter& onset);
    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    float unitScale = 1.0f;                   // px par unite du repere 760 x 958
    int dragging = -1;                        // 0 = gain, 1 = onset
    std::function<void()> onRightClick, onUserChange, onRepaintNeeded;
    std::function<void (int)> onTouched;      // ligne d'etat
    float valueOf (int i) const { return i == 0 ? gainV : onsetV; }

private:
    int zoneAt (juce::Point<float> p) const;
    void setV (int which, float v);
    juce::ParameterAttachment gainAtt, onsetAtt;
    float gainV = 5.0f, onsetV = 5.0f, startV = 0.0f;
};

//==============================================================================
// Selecteur TYPE a 7 positions (crans de 45 degres), rotation animee
//==============================================================================
class TypeKnob : public juce::Component
{
public:
    explicit TypeKnob (juce::RangedAudioParameter& p);
    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool tick();                               // animation (timer de l'editeur)
    float getShownAngle() const { return shownAngle; }
    float unitScale = 1.0f;
    std::function<void()> onRightClick, onUserChange;
    int getPosition() const { return pos; }

private:
    void moveTo (int p);
    juce::ParameterAttachment attachment;
    int pos = 3, dragStartPos = 3;
    float shownAngle = 0.0f, targetAngle = 0.0f;
};

//==============================================================================
// Inverseur a levier (rendu 3D, 3 images : haut = ON, intermediaire, bas = OFF)
//==============================================================================
class LeverSwitch : public juce::Component
{
public:
    explicit LeverSwitch (juce::RangedAudioParameter& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void tick (double nowMs);
    juce::Image strip;
    std::function<void()> onRightClick, onUserChange;

private:
    juce::ParameterAttachment attachment;
    bool on = false;
    int shownFrame = 0, targetFrame = 0;
    double transitionUntil = 0.0;
};

//==============================================================================
// Footswitch (reference Echoflex) : bascule a l'appui ; MOMENTARY ON = actif tant qu'on appuie
//==============================================================================
class FootSwitchArea : public juce::Component
{
public:
    FootSwitchArea (juce::RangedAudioParameter& active, std::atomic<float>* momentary);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override;
    bool isPressed() const { return pressed; }
    std::function<void()> onRightClick, onUserChange, onPressChange;

private:
    bool value = false, pressed = false;
    std::atomic<float>* mom = nullptr;
    juce::ParameterAttachment attachment;
};

//==============================================================================
class FreakoutAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit FreakoutAudioProcessorEditor (FreakoutAudioProcessor&);
    ~FreakoutAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    // repere de reference (100 %) et taille par defaut = taille minimale (identique a Echoflex)
    static constexpr int baseW = 760;
    static constexpr int baseH = 958;
    static constexpr int defaultH = 493;

    void setScaledHeight (int h);
    void showStatus();

private:
    void timerCallback() override;
    void useSoftwareRenderer();
    void visibilityChanged() override;
    void askRepaint (juce::Rectangle<int> r);
    void showContextMenu();
    void showPresetMenu();
    void stepPreset (int delta);
    void rebuildCaches (float scale = 0.0f);
    float physicalScale() const;
    juce::Rectangle<int> scaled (juce::Rectangle<float> r) const;
    juce::String buildStatusLine() const;
    juce::String buildInfoLine() const;
    juce::String buildPresetLine() const;
    void drawLeds (juce::Graphics&);
    void drawKnobs (juce::Graphics&);
    void drawFootswitch (juce::Graphics&);
    void drawDisplay (juce::Graphics&);

    FreakoutAudioProcessor& proc;

    juce::Image background, bgScaled, glowStrip, glowScaled[5], fsDown, fsDownS;
    int cacheW = 0, cacheH = 0;
    float cacheScale = 0.0f;
    int resizeSettle = 0;
    void* lastPeer = nullptr;
    double lastPaintMs = 0.0, repaintAskedMs = 0.0, lastScaleCheckMs = 0.0, lastRendererCheckMs = 0.0;
    juce::Typeface::Ptr labelFace, boldFace;

    ConcentricKnob range;
    TypeKnob typeKnob;
    LeverSwitch momentarySw, drySw;
    FootSwitchArea foot;

    // animation
    float ledLevel = 0.0f, meterShown = 0.0f;
    float ledB[9] {}, drawnLedB[9] {};
    float drawnGlow = -1.0f;
    int lastChanged = -1;
    float statusAlpha = 0.0f, drawnStatus = -1.0f;
    double statusUntil = 0.0;
    juce::String lastLine1, lastInfo, lastPreset;
    float lastValues[8];
    int lastPresetCounter = 0, lastMidiCount = 0, midiFlash = 0;
    int hoverZone = 0, drawnHover = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreakoutAudioProcessorEditor)
};
