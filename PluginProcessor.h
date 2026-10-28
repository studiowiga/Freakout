#pragma once
#include <cstring>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include "FreakoutDSP.h"

//==============================================================================
// FREAKOUT (Studio Wiga) - generateur de larsen naturel, d'apres le fonctionnement de la DigiTech FreqOut.
// Voir README.md et FreakoutDSP.h.
//==============================================================================

struct FreakoutPreset
{
    const char* name;
    float gain, onset;      // 0..10 comme sur la pedale (5 = 12 h)
    int   type;             // 0 SUB, 1 1ST, 2 2ND, 3 3RD, 4 5TH, 5 NAT LOW, 6 NAT HI
    int   momentary;        // 1 = footswitch momentane
    int   dry;              // 1 = signal sec garde
};

class FreakoutAudioProcessor : public juce::AudioProcessor,
                               private juce::AsyncUpdater,
                               private juce::Timer
{
public:
    FreakoutAudioProcessor();
    ~FreakoutAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "FreakOut"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    static int getNumFactoryPresets();
    static const FreakoutPreset& getFactoryPreset (int index);
    int getNumPrograms() override { return getNumFactoryPresets(); }
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override { applyFactoryPreset (index); }
    const juce::String getProgramName (int index) override { return getFactoryPreset (index).name; }
    void changeProgramName (int, const juce::String&) override {}
    void applyFactoryPreset (int index);

    bool saveUserPreset (int slot);
    bool loadUserPreset (int slot);
    bool hasUserPreset (int slot) const;
    static juce::File getUserPresetFile();

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setParamValue (const juce::String& id, float plain);

    // anti-gel : pointeurs des parametres mis en cache au demarrage (aucune recherche par chaine dans processBlock)
    enum RawIndex { rGain, rOnset, rType, rMomentary, rDry, rActive, rSens, rMidiFs, numRaw };
    static constexpr const char* rawIds[numRaw] = { "gain", "onset", "type", "momentary", "dry", "active", "sensitivity", "midiFsMode" };
    std::atomic<float>* rawPtr[numRaw] {};
    float rawAt (int i) const { return rawPtr[i]->load (std::memory_order_relaxed); }
    float raw (const char* id) const
    {
        for (int i = 0; i < numRaw; ++i)
            if (std::strcmp (id, rawIds[i]) == 0) return rawAt (i);
        return apvts.getRawParameterValue (id)->load();
    }

    static juce::StringArray typeNames()  { return { "SUB", "1ST", "2ND", "3RD", "5TH", "NAT LOW", "NAT HI" }; }
    static juce::StringArray sensNames()  { return { "Low", "Normal", "High" }; }

    // ---- informations pour la fenetre ----
    std::atomic<int>   currentPreset { 0 };
    std::atomic<int>   presetCounter { 0 };
    std::atomic<int>   lastMidiCC { -1 }, midiCounter { 0 };
    std::atomic<float> meter { 0.0f };          // progression du larsen (echelle de LED)
    std::atomic<float> fbHz { 0.0f };           // frequence du larsen en cours
    std::atomic<int>   fbHarmonic { 0 };        // harmonique en cours (-1 = SUB)

    enum MidiMap
    {
        ccGain = 20, ccOnset = 21,
        ccType = 22,        // 0..127 -> 7 positions
        ccMomentary = 23,   // >= 64 : ON
        ccDry = 24,         // >= 64 : ON
        ccFootswitch = 25   // footswitch (effet on / off)
    };
    static constexpr int numKnobs = 2;
    static const char* knobId (int i) { return i == 0 ? "gain" : "onset"; }

private:
    void handleMidi (juce::MidiBuffer& midi);
    void setParamFromAudio (int queueIndex, float plain);
    void timerCallback() override;
    static constexpr int numQueued = 6;     // gain, onset, type, momentary, dry, active
    static const char* queuedId (int i);
    std::atomic<float> pendingValue[numQueued];
    std::atomic<bool>  pendingSet[numQueued];
    void handleAsyncUpdate() override;
    float mapCC (int knob, float raw01);

    freakout::Engine engine;
    double sr = 48000.0;

    struct ExprRamp
    {
        bool  active = false;
        float value = 0.0f, target = 0.0f, step = 0.0f;
        int   left = 0;
        juce::int64 lastTime = 0;
        int   msb = -1;
        float lo = 1.0f, hi = 0.0f;
    } ramps[numKnobs];
    juce::int64 sampleClock = 0;
    int prevFsCC = 0;
    std::atomic<int> pendingPreset { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreakoutAudioProcessor)
};
