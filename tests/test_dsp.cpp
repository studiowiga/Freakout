// Banc d'essai du moteur Freakout (hors plugin) : cordes synthetiques (Karplus-Strong), ecrit des WAV + journal
#include "../FreakoutDSP.h"
#include <cstdio>
#include <string>
#include <random>
#include <cstdlib>
using namespace freakout;

static void writeWav (const std::string& path, const std::vector<float>& x, int fs)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    const uint32_t n = (uint32_t) x.size(), bytes = n * 2;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + bytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (1); w16 (1); w32 ((uint32_t) fs); w32 ((uint32_t) fs * 2); w16 (2); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (bytes);
    for (float v : x) { const int16_t s = (int16_t) std::lround (std::clamp (v, -1.0f, 1.0f) * 32767.0f); std::fwrite (&s, 2, 1, f); }
    std::fclose (f);
}

// corde pincee : Karplus-Strong avec amortissement dependant de la frequence, lecture fractionnaire
struct String_
{
    std::vector<float> d; float pos = 0; int w = 0; float len = 100, damp = 0.996f, lp = 0.5f, z = 0;
    void pluck (float fs, float hz, float amp, float bright, std::mt19937& rng, float decaySec)
    {
        len = fs / hz - 0.5f; d.assign ((size_t) std::lround (len), 0.0f); w = 0;
        std::uniform_real_distribution<float> u (-1.f, 1.f);
        float prev = 0;
        for (auto& v : d) { const float r = u (rng); prev = prev + bright * (r - prev); v = prev * amp; }
        damp = std::pow (0.001f, 1.0f / (decaySec * hz));
        lp = 0.5f; z = 0;
    }
    float tick()
    {
        if (d.empty()) return 0.0f;
        const int n = (int) d.size();
        const float y = d[(size_t) w];
        const int nx = (w + 1) % n;
        const float avg = 0.5f * (y + d[(size_t) nx]);
        d[(size_t) w] = avg * damp;
        w = nx;
        return y;
    }
    void mute() { for (auto& v : d) v *= 0.0f; }
};

int main()
{
    const int fs = std::getenv ("FS") ? std::atoi (std::getenv ("FS")) : 48000;
    std::mt19937 rng (7);
    struct Ev { float t; float hz; int kind; };    // kind 0 = attaque, 1 = etouffe, 2 = glissando (change de corde sans attaque)
    std::vector<Ev> evs = {
        { 0.2f, 82.41f, 0 }, { 3.2f, 0, 1 },
        { 3.6f, 329.63f, 0 }, { 6.6f, 0, 1 },
        { 7.0f, 49.0f, 0 },  { 10.0f, 0, 1 },          // G grave (7 cordes)
        { 10.4f, 196.0f, 0 }, { 11.6f, 220.0f, 2 }, { 12.8f, 246.94f, 2 }, { 14.0f, 0, 1 },
        { 14.4f, 659.26f, 0 },
    };
    const float total = 18.0f;
    const int N = (int) (total * fs);
    std::vector<float> in ((size_t) N, 0.0f);
    String_ s;
    size_t e = 0;
    for (int i = 0; i < N; ++i)
    {
        const float t = (float) i / fs;
        while (e < evs.size() && t >= evs[e].t)
        {
            if (evs[e].kind == 0) s.pluck ((float) fs, evs[e].hz, 0.35f, 0.5f, rng, 9.0f);
            else if (evs[e].kind == 1) s.mute();
            else
            {   // legato : meme energie, nouvelle longueur
                std::vector<float> old = s.d; const float amp = 0.15f;
                s.pluck ((float) fs, evs[e].hz, 0.0f, 0.5f, rng, 9.0f);
                for (size_t k = 0; k < s.d.size(); ++k) s.d[k] = old[k % old.size()];
                (void) amp;
            }
            ++e;
        }
        in[(size_t) i] = s.tick() + 0.0003f * std::uniform_real_distribution<float> (-1, 1) (rng);
    }
    writeWav ("tests/out_in.wav", in, fs);

    for (int type = 0; type < numTypes; ++type)
    {
        Engine eng; eng.prepare (fs, 1);
        Params p; p.type = type; p.gain = 0.6f; p.onset = 0.45f; p.dry = (std::getenv ("DRYOFF") == nullptr); p.active = true;
        std::vector<float> out = in;
        const int bs = 256;
        std::string log;
        for (int i = 0; i < N; i += bs)
        {
            float* ch[1] = { out.data() + i };
            eng.process (ch, 1, std::min (bs, N - i), p);
            if ((i / bs) % 47 == 0)
            {
                char buf[96];
                std::snprintf (buf, sizeof buf, "%.2fs h=%d f=%.1f m=%.2f | ", (float) i / fs, eng.shownHarmonic.load(), eng.shownHz.load(), eng.meter.load());
                log += buf;
            }
        }
        writeWav (std::string ("tests/out_") + (std::getenv ("DRYOFF") ? "fb_" : "") + std::to_string (type) + ".wav", out, fs);
        std::printf ("== %s\n%s\n", typeName (type), log.c_str());
    }
    return 0;
}
