// Captures PNG de la fenetre reelle de FreakOut (meme code que le plugin), pour l'apercu et la notice.
// Une guitare synthetique (corde pincee) est jouee pour faire monter le larsen et l'echelle de LED.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <random>
using namespace juce;

struct Pluck
{
    std::vector<float> d; int w = 0; float damp = 0.999f;
    void start (float fs, float hz)
    {
        d.assign ((size_t) std::lround (fs / hz - 0.5f), 0.0f);
        std::mt19937 r (3); std::uniform_real_distribution<float> u (-0.35f, 0.35f);
        float prev = 0; for (auto& v : d) { prev += 0.5f * (u (r) - prev); v = prev; }
        damp = std::pow (0.001f, 1.0f / (9.0f * hz)); w = 0;
    }
    float tick()
    {
        if (d.empty()) return 0.0f;
        const int n = (int) d.size(); const float y = d[(size_t) w]; const int nx = (w + 1) % n;
        d[(size_t) w] = 0.5f * (y + d[(size_t) nx]) * damp; w = nx; return y;
    }
};

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const String out = argc > 1 ? argv[1] : ".";
    FreakoutAudioProcessor proc;
    proc.prepareToPlay (48000.0, 256);
    std::unique_ptr<AudioProcessorEditor> ed (proc.createEditor());
    auto* fed = static_cast<FreakoutAudioProcessorEditor*> (ed.get());
    std::printf ("taille par defaut : %d x %d\n", ed->getWidth(), ed->getHeight());

    auto pump = [] (int n) { for (int i = 0; i < n; ++i) MessageManager::getInstance()->runDispatchLoopUntil (35); };
    auto play = [&] (float hz, float seconds)
    {
        Pluck s; s.start (48000.0f, hz);
        const int blocks = (int) (seconds * 48000.0f / 256.0f);
        for (int b = 0; b < blocks; ++b)
        {
            AudioBuffer<float> buf (2, 256); MidiBuffer m;
            for (int i = 0; i < 256; ++i) { const float v = s.tick(); buf.setSample (0, i, v); buf.setSample (1, i, v); }
            proc.processBlock (buf, m);
            if (b % 6 == 0) pump (1);
        }
    };
    auto shot = [&] (const String& name, int preset, bool active, float hz, float seconds, int height, const char* touch = nullptr)
    {
        fed->setScaledHeight (height);
        proc.applyFactoryPreset (preset);
        proc.setParamValue ("active", active ? 1.f : 0.f);
        pump (20);
        if (hz > 0) play (hz, seconds);
        if (touch != nullptr) proc.setParamValue (touch, proc.raw (touch) + 1.3f);
        pump (14);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        File f (out + "/" + name + ".png"); f.deleteFile();
        FileOutputStream os (f); PNGImageFormat().writeImageToStream (img, os);
        std::printf ("%s (%d x %d) meter %.2f fb %.1f Hz\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight(), proc.meter.load(), proc.fbHz.load());
    };
    shot ("freakout_default",  0, true,  0.0f,   0.0f, FreakoutAudioProcessorEditor::defaultH);
    shot ("freakout_bloom",    1, true,  164.8f, 0.30f, FreakoutAudioProcessorEditor::baseH);
    shot ("freakout_full",     3, true,  110.0f, 1.6f, FreakoutAudioProcessorEditor::baseH);
    shot ("freakout_status",   0, true,  0.0f,   0.0f, FreakoutAudioProcessorEditor::baseH, "gain");
    shot ("freakout_bypass",   10, false, 0.0f,  0.0f, FreakoutAudioProcessorEditor::baseH);
    // appui maintenu sur le footswitch (MOMENTARY ON)
    {
        proc.applyFactoryPreset (2);
        pump (10);
        auto* fsComp = ed->getChildComponent (4);
        const auto pt = fsComp->getLocalBounds().getCentre().toFloat();
        auto src = Desktop::getInstance().getMainMouseSource();
        MouseEvent ev (src, pt, ModifierKeys (ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0, fsComp, fsComp, Time::getCurrentTime(), pt, Time::getCurrentTime(), 1, false);
        fsComp->mouseDown (ev);
        play (220.0f, 0.8f);
        pump (10);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        File f (out + "/freakout_pressed.png"); f.deleteFile();
        FileOutputStream os (f); PNGImageFormat().writeImageToStream (img, os);
        fsComp->mouseUp (ev.withNewPosition (pt));
        std::printf ("%s\n", f.getFullPathName().toRawUTF8());
    }

    // pedale d'expression : course 2..116 sur CC 20 (gain)
    {
        auto sendCC = [&] (int v)
        {
            AudioBuffer<float> b (2, 256); b.clear(); MidiBuffer m; m.addEvent (MidiMessage::controllerEvent (1, 20, v), 0);
            proc.processBlock (b, m);
            for (int k = 0; k < 20; ++k) { MidiBuffer e; b.clear(); proc.processBlock (b, e); }
            pump (3);
            return proc.raw ("gain");
        };
        const float a = sendCC (2), c = sendCC (116), mid = sendCC (59);
        std::printf ("pedale 2..116 -> gain %.2f / %.2f, mi-course %.2f\n", a, c, mid);
    }
    ed.reset();
    return 0;
}
