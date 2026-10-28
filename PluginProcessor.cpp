#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

//==============================================================================
// Presets d'usine - potards notes comme sur la pedale (0 a 10, 5 = 12 h)
//==============================================================================
static const FreakoutPreset factoryPresets[] =
{
    //  nom                       gain  onset type mom dry
    { "Manual Setup",             5.0f, 5.0f, 3, 0, 1 },   // procedure du manuel : GAIN, ONSET et TYPE a 12 h
    { "Live Feedback Wail",       6.0f, 4.5f, 6, 0, 1 },   // NAT HI : le larsen choisit son harmonique, comme un ampli a fond
    { "Studio Swell (Momentary)", 4.2f, 5.8f, 6, 1, 1 },   // reglage releve dans le test Tape Op : gain 11 h, onset 13 h
    { "Unison Sustain",           5.5f, 2.0f, 1, 0, 1 },   // 1ST : sustain infini de la note jouee
    { "Octave Scream",            7.0f, 2.5f, 2, 0, 1 },
    { "Twelfth Lead",             6.0f, 3.5f, 3, 0, 1 },
    { "Fifth Harmonic Squeal",    6.5f, 3.0f, 4, 0, 1 },
    { "Sub Drone",                6.0f, 6.0f, 0, 0, 1 },
    { "Minor Key Natural",        5.0f, 5.0f, 5, 0, 1 },   // NAT LOW : pas de 5e harmonique (evite la tierce majeure)
    { "Doom Bloom",               7.0f, 7.5f, 5, 0, 1 },
    { "EBow Swell",               6.0f, 6.0f, 1, 0, 0 },   // DRY OFF : la note n'arrive qu'en gonflant
    { "Theremin Ghost",           6.5f, 3.5f, 3, 0, 0 },
    { "Instant Squeal",           9.0f, 0.5f, 6, 1, 1 },
};

int FreakoutAudioProcessor::getNumFactoryPresets() { return (int) std::size (factoryPresets); }

const FreakoutPreset& FreakoutAudioProcessor::getFactoryPreset (int i)
{
    return factoryPresets[(size_t) jlimit (0, getNumFactoryPresets() - 1, i)];
}

//==============================================================================
FreakoutAudioProcessor::FreakoutAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  AudioChannelSet::stereo(), true)
                        .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FREAKOUT", createLayout())
{
    for (int i = 0; i < numRaw; ++i) rawPtr[i] = apvts.getRawParameterValue (rawIds[i]);
    for (int i = 0; i < numQueued; ++i) { pendingValue[i] = 0.0f; pendingSet[i] = false; }
    startTimerHz (60);
}

AudioProcessorValueTreeState::ParameterLayout FreakoutAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    const auto& d = factoryPresets[0];
    auto knob = [] (const String& id, const String& name, float def)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 10.0f, 0.01f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int) { return String (roundToInt (v * 10.0f)) + " %"; }));
    };
    p.push_back (knob ("gain",  "Gain",  d.gain));
    p.push_back (knob ("onset", "Onset", d.onset));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "type", 1 }, "Type", typeNames(), d.type));
    p.push_back (std::make_unique<AudioParameterBool>   (ParameterID { "momentary", 1 }, "Momentary", d.momentary != 0));
    p.push_back (std::make_unique<AudioParameterBool>   (ParameterID { "dry", 1 }, "Dry", d.dry != 0));
    p.push_back (std::make_unique<AudioParameterBool>   (ParameterID { "active", 1 }, "Footswitch", true));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "sensitivity", 1 }, "Trigger Sensitivity", sensNames(), 1));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "midiFsMode", 1 }, "MIDI Footswitch Mode",
                                                         StringArray { "Latching (127 / 0)", "Toggle each press" }, 0));
    return { p.begin(), p.end() };
}

bool FreakoutAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo()) return false;
    return in == out;
}

void FreakoutAudioProcessor::setParamValue (const String& id, float plain)
{
    if (auto* prm = apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
        prm->endChangeGesture();
    }
}

// Anti-gel : jamais d'appel a l'hote depuis le thread audio ; le MIDI met la valeur en file,
// un timer du thread de messages la transmet au DAW et a la fenetre.
const char* FreakoutAudioProcessor::queuedId (int i)
{
    static const char* ids[numQueued] = { "gain", "onset", "type", "momentary", "dry", "active" };
    return ids[jlimit (0, numQueued - 1, i)];
}

void FreakoutAudioProcessor::setParamFromAudio (int qi, float plain)
{
    pendingValue[qi].store (plain, std::memory_order_relaxed);
    pendingSet[qi].store (true, std::memory_order_release);
}

void FreakoutAudioProcessor::timerCallback()
{
    for (int i = 0; i < numQueued; ++i)
        if (pendingSet[i].exchange (false, std::memory_order_acquire))
            if (auto* prm = apvts.getParameter (queuedId (i)))
                prm->setValueNotifyingHost (prm->convertTo0to1 (pendingValue[i].load (std::memory_order_relaxed)));
}

//==============================================================================
void FreakoutAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    engine.prepare (sr, jmax (1, getTotalNumOutputChannels()));
    for (auto& r : ramps) { r.active = false; r.left = 0; }
}

//==============================================================================
// MIDI
//==============================================================================
float FreakoutAudioProcessor::mapCC (int k, float raw01)
{
    auto& r = ramps[k];
    if (raw01 < r.lo) r.lo = raw01;
    if (raw01 > r.hi) r.hi = raw01;
    float v = raw01;
    if (r.hi - r.lo > 0.4f) v = (raw01 - r.lo) / (r.hi - r.lo);   // course de la pedale apprise automatiquement
    constexpr float dz = 0.02f;                                     // zones mortes aux butees
    return jlimit (0.0f, 1.0f, (v - dz) / (1.0f - 2.0f * dz));
}

void FreakoutAudioProcessor::handleMidi (MidiBuffer& midi)
{
    const bool latching = rawAt (rMidiFs) < 0.5f;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const juce::int64 when = sampleClock + meta.samplePosition;
        if (m.isController())
        {
            const int cc = m.getControllerNumber(), v = m.getControllerValue();
            bool known = true;
            auto knobFromCC = [&] (int k, float raw01)
            {
                auto& r = ramps[k];
                const float target = mapCC (k, raw01) * 10.0f;
                if (! r.active) r.value = rawAt (k == 0 ? rGain : rOnset);
                const double interval = (double) (when - r.lastTime);
                r.left = (int) jlimit (0.002 * sr, 0.040 * sr, interval);
                r.lastTime = when;
                r.target = target;
                r.step = (r.target - r.value) / (float) r.left;
                r.active = true;
                setParamFromAudio (k, target);
                currentPreset = -1;
            };
            if (cc == ccGain || cc == ccOnset)
            {
                const int k = cc - ccGain;
                ramps[k].msb = v;
                knobFromCC (k, (float) v / 127.0f);
            }
            else if (cc == ccGain + 32 || cc == ccOnset + 32)
            {
                const int k = cc - 32 - ccGain;
                if (ramps[k].msb >= 0) knobFromCC (k, (float) (ramps[k].msb * 128 + v) / 16383.0f);
            }
            else if (cc == ccType)      { setParamFromAudio (2, (float) jlimit (0, 6, v * 7 / 128)); currentPreset = -1; }
            else if (cc == ccMomentary) { setParamFromAudio (3, v >= 64 ? 1.0f : 0.0f); currentPreset = -1; }
            else if (cc == ccDry)       { setParamFromAudio (4, v >= 64 ? 1.0f : 0.0f); currentPreset = -1; }
            else if (cc == ccFootswitch)
            {
                // MOMENTARY ON : actif tant que le footswitch MIDI est enfonce ; sinon a accrochage ou bascule
                const float cur = pendingSet[5].load() ? pendingValue[5].load() : rawAt (rActive);
                const bool mom = (pendingSet[3].load() ? pendingValue[3].load() : rawAt (rMomentary)) > 0.5f;
                if (mom || latching)                setParamFromAudio (5, v >= 64 ? 1.0f : 0.0f);
                else if (v >= 64 && prevFsCC < 64)  setParamFromAudio (5, cur > 0.5f ? 0.0f : 1.0f);
                prevFsCC = v;
            }
            else known = false;
            if (known) { lastMidiCC = cc; ++midiCounter; }
        }
        else if (m.isProgramChange())
        {
            const int pc = m.getProgramChangeNumber();
            if (pc < getNumFactoryPresets()) { pendingPreset = pc; triggerAsyncUpdate(); }
            lastMidiCC = -2; ++midiCounter;
        }
    }
    midi.clear();
}

void FreakoutAudioProcessor::handleAsyncUpdate()
{
    const int p = pendingPreset.exchange (-1);
    if (p >= 0) applyFactoryPreset (p);
}

//==============================================================================
void FreakoutAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    handleMidi (midi);
    if (n == 0) return;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, n);

    float knobs[numKnobs];
    for (int k = 0; k < numKnobs; ++k)
    {
        auto& r = ramps[k];
        if (r.active)
        {
            const int adv = jmin (r.left, n);
            r.value += r.step * (float) adv;
            r.left -= adv;
            if (r.left <= 0) { r.value = r.target; r.active = false; }
            knobs[k] = r.value;
        }
        else knobs[k] = rawAt (k == 0 ? rGain : rOnset);
    }
    freakout::Params p;
    p.gain  = knobs[0] / 10.0f;
    p.onset = knobs[1] / 10.0f;
    p.type  = jlimit (0, 6, roundToInt (rawAt (rType)));
    p.dry   = rawAt (rDry) > 0.5f;
    p.active = rawAt (rActive) > 0.5f;
    p.sensitivity = jlimit (0, 2, roundToInt (rawAt (rSens)));

    const int nCh = jmin (2, buffer.getNumChannels());
    float* io[2] = { buffer.getWritePointer (0), nCh > 1 ? buffer.getWritePointer (1) : nullptr };
    engine.process (io, nCh, n, p);

    meter = engine.meter.load (std::memory_order_relaxed);
    fbHz = engine.shownHz.load (std::memory_order_relaxed);
    fbHarmonic = engine.shownHarmonic.load (std::memory_order_relaxed);
    sampleClock += n;
}

//==============================================================================
// Presets
//==============================================================================
int FreakoutAudioProcessor::getCurrentProgram()
{
    const int c = currentPreset.load();
    return (c >= 0 && c < getNumFactoryPresets()) ? c : 0;
}

void FreakoutAudioProcessor::applyFactoryPreset (int index)
{
    const auto& p = getFactoryPreset (index);
    setParamValue ("gain", p.gain);
    setParamValue ("onset", p.onset);
    setParamValue ("type", (float) p.type);
    setParamValue ("momentary", (float) p.momentary);
    setParamValue ("dry", (float) p.dry);
    currentPreset = jlimit (0, getNumFactoryPresets() - 1, index);
    ++presetCounter;
}

File FreakoutAudioProcessor::getUserPresetFile()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
             .getChildFile ("Studio Wiga").getChildFile ("FreakOut").getChildFile ("UserPresets.xml");
}

static const char* presetIds[] = { "gain", "onset", "type", "momentary", "dry" };

bool FreakoutAudioProcessor::saveUserPreset (int slot)
{
    if (slot < 1 || slot > 9) return false;
    auto file = getUserPresetFile();
    file.getParentDirectory().createDirectory();
    std::unique_ptr<XmlElement> root;
    if (file.existsAsFile()) root = XmlDocument::parse (file);
    if (root == nullptr || ! root->hasTagName ("UserPresets")) root = std::make_unique<XmlElement> ("UserPresets");
    for (auto* e = root->getFirstChildElement(); e != nullptr; e = e->getNextElement())
        if (e->getIntAttribute ("slot") == slot) { root->removeChildElement (e, true); break; }
    auto* e = root->createNewChildElement ("Preset");
    e->setAttribute ("slot", slot);
    for (auto* id : presetIds) e->setAttribute (id, (double) raw (id));
    const bool ok = root->writeTo (file);
    if (ok) { currentPreset = 100 + slot; ++presetCounter; }
    return ok;
}

bool FreakoutAudioProcessor::loadUserPreset (int slot)
{
    const auto file = getUserPresetFile();
    if (! file.existsAsFile()) return false;
    auto root = XmlDocument::parse (file);
    if (root == nullptr) return false;
    for (auto* e = root->getFirstChildElement(); e != nullptr; e = e->getNextElement())
        if (e->getIntAttribute ("slot") == slot)
        {
            for (auto* id : presetIds)
                if (e->hasAttribute (id)) setParamValue (id, (float) e->getDoubleAttribute (id));
            currentPreset = 100 + slot; ++presetCounter;
            return true;
        }
    return false;
}

bool FreakoutAudioProcessor::hasUserPreset (int slot) const
{
    const auto file = getUserPresetFile();
    if (! file.existsAsFile()) return false;
    if (auto root = XmlDocument::parse (file))
        for (auto* e = root->getFirstChildElement(); e != nullptr; e = e->getNextElement())
            if (e->getIntAttribute ("slot") == slot) return true;
    return false;
}

//==============================================================================
void FreakoutAudioProcessor::getStateInformation (MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("currentPreset", currentPreset.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void FreakoutAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = ValueTree::fromXml (*xml);
            currentPreset = (int) state.getProperty ("currentPreset", 0);
            apvts.replaceState (state);
            ++presetCounter;
        }
}

//==============================================================================
AudioProcessorEditor* FreakoutAudioProcessor::createEditor() { return new FreakoutAudioProcessorEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FreakoutAudioProcessor(); }
