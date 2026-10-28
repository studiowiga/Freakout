#pragma once
// =====================================================================================================
// FREAKOUT (Studio Wiga) - moteur de larsen « naturel » (d'apres le fonctionnement de la DigiTech FreqOut)
//
// Principe (aucun schema publie : pedale entierement numerique, reproduite d'apres le manuel et les tests) :
//   1. detection de la hauteur (YIN sur un signal decime a ~12 kHz, de 30 Hz (7 cordes / basse) a 1,6 kHz) ;
//   2. choix de l'harmonique qui « larsene » selon TYPE : SUB (octave en dessous), 1ST (unisson), 2ND, 3RD,
//      5TH (5 x la frequence de la note), NAT LOW / NAT HI (choix « naturel » parmi les partiels presents,
//      selon l'attaque, le micro et la tonalite, avec bascules d'un partiel a l'autre comme un vrai larsen) ;
//   3. un oscillateur verrouille en phase (PLL sur le signal analytique du partiel isole) reprend exactement le
//      partiel de la corde (vibrato, tire, inharmonicite) puis le fait « gonfler » de facon exponentielle
//      (croissance lineaire en dB, saturation douce en haut) : c'est le partiel qui se met a larsener ;
//   4. ONSET = temps pour atteindre la pleine intensite, GAIN = niveau du larsen (relatif a l'attaque de la note) ;
//   5. le larsen tient tant que la corde vibre ; corde etouffee ou nouvelle attaque -> il retombe et se reconstruit.
// Aucune allocation, aucun verrou dans process() ; tout est dimensionne dans prepare().
// =====================================================================================================
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include <atomic>

namespace freakout
{
constexpr float pi = 3.14159265358979f;
constexpr float twoPi = 6.28318530717959f;

inline float dbToLin (float db) { return std::pow (10.0f, db * 0.05f); }
inline float linToDb (float l)  { return 20.0f * std::log10 (std::max (l, 1.0e-9f)); }
inline float wrapPi (float a)
{
    while (a >  pi) a -= twoPi;
    while (a < -pi) a += twoPi;
    return a;
}

enum Type { sub = 0, first, second, third, fifth, natLow, natHigh, numTypes };
inline const char* typeName (int t)
{
    static const char* n[numTypes] = { "SUB", "1ST", "2ND", "3RD", "5TH", "NAT LOW", "NAT HI" };
    return n[std::clamp (t, 0, numTypes - 1)];
}

struct Params
{
    float gain  = 0.5f;     // 0..1 (potard, 12 h = 0.5)
    float onset = 0.5f;     // 0..1
    int   type  = natHigh;
    bool  dry   = true;     // DRY ON : la guitare reste ; OFF : larsen seul (effet « EBow »)
    bool  active = true;    // effet enclenche (footswitch)
    int   sensitivity = 1;  // 0 = basse, 1 = normale, 2 = haute (seuil de declenchement)
};

//------------------------------------------------------------------------------------------------------
// petits filtres
//------------------------------------------------------------------------------------------------------
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowpass (float fs, float f, float q)
    {
        const float w = twoPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0f * q), a0 = 1.0f + al;
        b0 = (1.0f - c) * 0.5f / a0; b1 = (1.0f - c) / a0; b2 = b0; a1 = -2.0f * c / a0; a2 = (1.0f - al) / a0;
    }
    void highpass (float fs, float f, float q)
    {
        const float w = twoPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0f * q), a0 = 1.0f + al;
        b0 = (1.0f + c) * 0.5f / a0; b1 = -(1.0f + c) / a0; b2 = b0; a1 = -2.0f * c / a0; a2 = (1.0f - al) / a0;
    }
    void reset() { z1 = z2 = 0; }
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// filtre d'etat (TPT) : passe-bande a gain unite au centre, frequence modifiable sans clic
struct SVF
{
    float g = 0.1f, k = 0.1f, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    void set (float fs, float f, float q)
    {
        f = std::clamp (f, 5.0f, fs * 0.45f);
        g = std::tan (pi * f / fs); k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    float bandpass (float x)
    {
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        return v1 * k;            // gain unite au centre
    }
};

// paire de filtres passe-tout en quadrature (Olli Niemitalo) : 90 degres +/- 0,7 de 25 Hz a 20 kHz
struct Hilbert
{
    static constexpr double cI[4] = { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };
    static constexpr double cQ[4] = { 0.6923878,       0.9360654322959, 0.9882295226860, 0.9987488452737 };
    float xi[4][2] {}, yi[4][2] {}, xq[4][2] {}, yq[4][2] {}, qDelay = 0;
    void reset() { std::memset (this, 0, sizeof (*this)); }
    void process (float x, float& outI, float& outQ)
    {
        float vi = x, vq = x;
        for (int s = 0; s < 4; ++s)
        {
            const float ai = (float) (cI[s] * cI[s]), aq = (float) (cQ[s] * cQ[s]);
            const float yI = ai * (vi + yi[s][1]) - xi[s][1];
            xi[s][1] = xi[s][0]; xi[s][0] = vi; yi[s][1] = yi[s][0]; yi[s][0] = yI; vi = yI;
            const float yQ = aq * (vq + yq[s][1]) - xq[s][1];
            xq[s][1] = xq[s][0]; xq[s][0] = vq; yq[s][1] = yq[s][0]; yq[s][0] = yQ; vq = yQ;
        }
        outI = vi;
        outQ = qDelay; qDelay = vq;      // branche Q retardee d'un echantillon
    }
    // dephasage de la branche I a la pulsation w (rad / echantillon) : sert a remettre l'oscillateur en phase avec le sec
    static float phaseI (float w)
    {
        double re = 1.0, im = 0.0;
        const double c2 = std::cos (2.0 * w), s2 = -std::sin (2.0 * w);      // z^-2
        for (double c : cI)
        {
            const double a = c * c;
            const double nr = a - c2, ni = -s2;          // a - z^-2
            const double dr = 1.0 - a * c2, di = -a * s2; // 1 - a z^-2
            const double den = dr * dr + di * di;
            const double hr = (nr * dr + ni * di) / den, hi = (ni * dr - nr * di) / den;
            const double tr = re * hr - im * hi, ti = re * hi + im * hr;
            re = tr; im = ti;
        }
        return (float) std::atan2 (im, re);
    }
};

// bruit lisse (micro-respiration du larsen)
struct SmoothNoise
{
    uint32_t s = 12345u;
    float a = 0, b = 0, t = 0, rate = 0.0001f;
    float rnd() { s = s * 1664525u + 1013904223u; return (float) ((s >> 8) & 0xFFFF) / 32768.0f - 1.0f; }
    void init (float fs, float hz, uint32_t seed) { s = seed; rate = hz / fs; a = rnd(); b = rnd(); t = 0; }
    float next()
    {
        t += rate;
        if (t >= 1.0f) { t -= 1.0f; a = b; b = rnd(); }
        const float u = t * t * (3.0f - 2.0f * t);
        return a + (b - a) * u;
    }
};

//------------------------------------------------------------------------------------------------------
// Detection de hauteur YIN (signal decime)
//------------------------------------------------------------------------------------------------------
class PitchTracker
{
public:
    void prepare (double fs)
    {
        decim = std::max (1, (int) std::floor (fs / 11000.0));
        fsd = (float) fs / (float) decim;
        aa1.lowpass ((float) fs, std::min (3200.0f, fsd * 0.42f), 0.54f);
        aa2.lowpass ((float) fs, std::min (3200.0f, fsd * 0.42f), 1.31f);
        hp.highpass ((float) fs, 24.0f, 0.707f);
        tauMax = (int) std::ceil (fsd / fMin) + 2;
        tauMin = std::max (2, (int) std::floor (fsd / fMax));
        win = tauMax;
        bufSize = 1; while (bufSize < 4 * (tauMax + win)) bufSize <<= 1;
        buf.assign ((size_t) bufSize, 0.0f);
        lin.assign ((size_t) (tauMax + win + 4), 0.0f);
        d.assign ((size_t) (tauMax + 2), 0.0f);
        hop = std::max (16, (int) std::round (fsd * 0.0045f));
        reset();
    }
    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        aa1.reset(); aa2.reset(); hp.reset();
        writePos = 0; phaseCount = 0; sinceHop = 0;
        f0 = 0; aperiodicity = 1; voiced = false; newFrame = false;
    }

    // renvoie true quand une nouvelle analyse est disponible
    bool push (float x, bool analyse)
    {
        const float y = aa2.process (aa1.process (hp.process (x)));
        if (++phaseCount < decim) return false;
        phaseCount = 0;
        buf[(size_t) (writePos++ & (bufSize - 1))] = y;
        if (++sinceHop < hop) return false;
        sinceHop = 0;
        if (! analyse) { voiced = false; return false; }
        run();
        return true;
    }

    // energie d'un partiel (Goertzel fenetre de Hann) sur les N derniers echantillons decimes
    float partialAmp (float hz, int N) const
    {
        if (hz <= 0.0f || hz >= fsd * 0.48f) return 0.0f;
        N = std::min (N, bufSize / 2);
        const float w = twoPi * hz / fsd, c = 2.0f * std::cos (w);
        float s1 = 0, s2 = 0;
        const int start = writePos - N;
        for (int n = 0; n < N; ++n)
        {
            const float win_ = 0.5f - 0.5f * std::cos (twoPi * (float) n / (float) (N - 1));
            const float s0 = buf[(size_t) ((start + n) & (bufSize - 1))] * win_ + c * s1 - s2;
            s2 = s1; s1 = s0;
        }
        const float re = s1 - s2 * std::cos (w), im = s2 * std::sin (w);
        return 4.0f * std::sqrt (re * re + im * im) / (float) N;      // ~ amplitude du partiel
    }

    float fsd = 12000.0f;
    float f0 = 0.0f, aperiodicity = 1.0f;
    bool voiced = false, newFrame = false;
    static constexpr float fMin = 30.0f, fMax = 1600.0f;

private:
    void run()
    {
        const int need = tauMax + win;
        const int start = writePos - need;
        for (int i = 0; i < need; ++i) lin[(size_t) i] = buf[(size_t) ((start + i) & (bufSize - 1))];

        // fonction de difference (fenetre fixe = periode la plus longue : notes tres graves comprises)
        // fenetre ancree sur les echantillons les plus recents (reaction rapide aux nouvelles notes)
        const float* a = lin.data() + (need - win);
        d[0] = 0.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float s = 0.0f;
            const float* b = a - tau;
            for (int j = 0; j < win; ++j) { const float df = a[j] - b[j]; s += df * df; }
            d[(size_t) tau] = s;
        }
        // difference normalisee cumulee
        float run_ = 0.0f;
        d[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            run_ += d[(size_t) tau];
            d[(size_t) tau] = run_ > 0.0f ? d[(size_t) tau] * (float) tau / run_ : 1.0f;
        }
        // premier creux sous le seuil (evite les erreurs d'octave vers le grave), sinon minimum global
        int best = -1;
        constexpr float thr = 0.14f;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (d[(size_t) tau] < thr)
            {
                while (tau + 1 < tauMax && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
                best = tau; break;
            }
        if (best < 0)
        {
            float m = 2.0f;
            for (int tau = tauMin; tau < tauMax; ++tau)
                if (d[(size_t) tau] < m) { m = d[(size_t) tau]; best = tau; }
        }
        if (best <= tauMin || best >= tauMax - 1) { voiced = false; aperiodicity = 1.0f; return; }
        // interpolation parabolique
        const float y0 = d[(size_t) best - 1], y1 = d[(size_t) best], y2 = d[(size_t) best + 1];
        const float den = y0 - 2.0f * y1 + y2;
        const float off = std::abs (den) > 1.0e-9f ? 0.5f * (y0 - y2) / den : 0.0f;
        const float t = (float) best + std::clamp (off, -0.5f, 0.5f);
        aperiodicity = y1;
        voiced = y1 < 0.32f;
        if (voiced) f0 = fsd / t;
        newFrame = true;
    }

    Biquad aa1, aa2, hp;
    int decim = 4, phaseCount = 0, writePos = 0, bufSize = 4096, sinceHop = 0, hop = 54;
    int tauMax = 400, tauMin = 7, win = 400;
    std::vector<float> buf, lin, d;
};

//------------------------------------------------------------------------------------------------------
// Voix de larsen : oscillateur verrouille sur un partiel de la corde, enveloppe de « floraison »
//------------------------------------------------------------------------------------------------------
struct Voice
{
    enum Stage { idle, bloom, release };
    Stage stage = idle;
    float fs = 48000.0f;
    float lockHz = 220.0f;        // frequence du partiel suivi
    float ratio = 1.0f;           // frequence de sortie / partiel suivi (0,5 pour SUB)
    int   harmonic = 1;           // harmonique de la note (affichage, mode naturel)
    float wNom = 0.0f, integ = 0.0f, theta = 0.0f, phi = 0.0f, psi = 0.0f;
    float mag = 0.0f;             // amplitude lissee du partiel dans la corde
    float levelDb = -40.0f;       // floraison : niveau virtuel (dB), monte lineairement jusqu'a +6 dB
    float rateDb = 50.0f;         // dB / s
    float out = 0.0f;             // amplitude effective (lineaire, 0..~1 x cible)
    float relCoef = 0.999f, relGain = 1.0f;
    float bandQ = 10.0f;
    int   ctrl = 0;
    SVF bp;
    Hilbert hil;
    SmoothNoise flutter, drift;

    void start (float hz, float r, int h, float startDb, float rate, uint32_t seed)
    {
        lockHz = hz; ratio = r; harmonic = h;
        stage = bloom; levelDb = startDb; rateDb = rate;
        relGain = 1.0f; integ = 0.0f;
        wNom = wTarget = twoPi * lockHz / fs;
        glideCoef = 1.0f - std::exp (-1.0f / (0.022f * fs));
        bandQ = std::clamp (lockHz / 6.0f, 2.0f, 24.0f);
        bp.reset(); hil.reset(); bp.set (fs, lockHz, bandQ);
        psi = Hilbert::phaseI (wNom);
        mag = 0.0f; ctrl = 0;
        theta = 0.0f; phi = 0.0f; primed = false;
        fadeIn = 0.0f; age = 0;
        fadeStep = 1.0f / (0.025f * fs);         // entree en fondu (25 ms) : jamais de clic, meme quand la voix part fort
        flutter.init (fs, 2.6f, seed * 7u + 3u);
        drift.init (fs, 0.7f, seed * 13u + 11u);
    }
    void retune (float hz, bool glide = false)      // suivi du capteur de hauteur (tires, glissandos, legato)
    {
        lockHz = hz;
        wTarget = twoPi * hz / fs;
        if (! glide) { wNom = wTarget; integ *= 0.5f; }
        else         integ = 0.0f;                   // legato : glissement continu de la phase (aucun clic)
    }
    void startRelease (float seconds)
    {
        if (stage == idle) return;
        const float c = std::exp (-1.0f / (std::max (0.005f, seconds) * fs));
        if (stage != release || c < relCoef) relCoef = c;
        stage = release;
    }

    // x : guitare (mono, filtree) ; target : amplitude du larsen a pleine intensite ; floorAmp : bruit de fond
    float process (float x, float target, float floorAmp, bool natural)
    {
        if (stage == idle) return 0.0f;

        if (std::abs (wNom - wTarget) > 0.0f)
        {
            wNom += (wTarget - wNom) * glideCoef;
            if (std::abs (wTarget - wNom) < 1.0e-7f) wNom = wTarget;
        }
        // isole le partiel suivi et en tire la phase instantanee (signal analytique)
        if (++ctrl >= 32)
        {
            ctrl = 0;
            const float w = std::clamp (wNom + integ, wNom * 0.85f, wNom * 1.18f);
            bp.set (fs, w * fs / twoPi, bandQ);
            psi = Hilbert::phaseI (w);
        }
        const float b = bp.bandpass (x);
        float I, Q;
        hil.process (b, I, Q);
        const float m = std::sqrt (I * I + Q * Q);
        mag += (m - mag) * (m > mag ? 0.02f : 0.004f);

        // confiance du verrouillage : partiel bien au-dessus du bruit
        const float snr = mag / std::max (floorAmp, 1.0e-6f);
        const float conf = std::clamp ((snr - 2.0f) / 6.0f, 0.0f, 1.0f);
        const float thetaB = std::atan2 (Q, I) - psi;
        // calage initial de la phase sur le partiel (seulement pendant l'entree en fondu, ensuite la PLL corrige en douceur)
        ++age;
        if (! primed && conf > 0.4f && fadeIn <= 0.0f) { theta = thetaB; phi = theta * ratio; primed = true; }

        // PLL du 2e ordre (bande ~12 Hz) ; en roue libre quand le partiel disparait
        const float e = wrapPi (thetaB - theta);
        const float kp = 2.0f * 0.8f * twoPi * 12.0f / fs, ki = (twoPi * 12.0f / fs) * (twoPi * 12.0f / fs);
        integ += ki * e * conf;
        const float lim = wNom * 0.06f;
        integ = std::clamp (integ, -lim, lim);
        // micro-derive tres faible quand on est en roue libre (un vrai larsen n'est jamais fige)
        const float wander = (1.0f - conf) * wNom * 0.0012f * drift.next();
        const float w = wNom + integ + kp * e * conf + wander;
        theta = wrapPi (theta + w);
        phi = wrapPi (phi + w * ratio);

        // floraison : croissance exponentielle (lineaire en dB) jusqu'a +6 dB virtuels, saturation douce
        if (stage == bloom)
        {
            levelDb = std::min (6.0f, levelDb + rateDb / fs);
            const float a = dbToLin (levelDb);
            out = std::tanh (a) * (1.0f / 0.9640276f);
        }
        else
        {
            relGain *= relCoef;
            if (relGain < 1.0e-4f) { stage = idle; out = 0.0f; return 0.0f; }
        }
        // l'entree en fondu attend le calage de phase (au plus 25 ms) : aucun saut de phase audible
        if (fadeIn < 1.0f && (primed || (float) age > 0.025f * fs)) fadeIn = std::min (1.0f, fadeIn + fadeStep);
        const float fi = fadeIn * fadeIn * (3.0f - 2.0f * fadeIn);
        const float lvl = out * relGain * fi;

        // timbre : quasi sinusoidal au debut, s'enrichit (2e / 3e harmoniques) quand le larsen sature
        const float sat = std::min (1.0f, out * out);
        const float c2 = (natural ? 0.12f : 0.09f) * sat, c3 = 0.05f * sat;
        float s = std::cos (phi) + c2 * std::cos (2.0f * phi + 0.45f) + c3 * std::cos (3.0f * phi + 1.1f);
        s *= 1.0f / (1.0f + 0.6f * (c2 + c3));
        const float breath = 1.0f + 0.035f * flutter.next() * sat;
        return s * lvl * breath * target;
    }

    float progressDb() const    // niveau atteint par rapport a la pleine intensite (pour l'echelle de LED)
    {
        if (stage == idle) return -60.0f;
        return linToDb (std::max (out * relGain, 1.0e-6f));
    }
    bool primed = false;
    float fadeIn = 1.0f, fadeStep = 0.001f, wTarget = 0.0f, glideCoef = 0.001f;
    int age = 0;
};

//------------------------------------------------------------------------------------------------------
// Moteur complet
//------------------------------------------------------------------------------------------------------
class Engine
{
public:
    void prepare (double sampleRate, int /*numChannels*/)
    {
        fs = (float) sampleRate;
        tracker.prepare (sampleRate);
        inHp.highpass (fs, 22.0f, 0.707f);
        outHp.highpass (fs, 18.0f, 0.707f);
        for (auto& v : voices) { v.fs = fs; v.stage = Voice::idle; }
        ctrlRing.assign ((size_t) std::ceil (0.12f * fs / 32.0f) + 2, 0.0f);
        rmsRing.assign ((size_t) std::max (2, (int) std::round (0.02f * fs / 32.0f)), 0.0f);
        rmsHist.assign ((size_t) std::ceil (0.2f * fs / 32.0f) + 2, 0.0f);
        auto coef = [this] (float sec) { return 1.0f - std::exp (-1.0f / (sec * fs)); };
        kFastA = coef (0.0003f); kFastR = coef (0.030f);
        kSlowA = coef (0.005f);  kSlowR = coef (0.060f);
        reset();
    }
    void reset()
    {
        tracker.reset(); inHp.reset(); outHp.reset();
        for (auto& v : voices) v.stage = Voice::idle;
        envFast = envSlow = 0; notePeak = 0; noteAlive = false; noteAge = 0;
        std::fill (ctrlRing.begin(), ctrlRing.end(), 0.0f);
        std::fill (rmsRing.begin(), rmsRing.end(), 0.0f);
        std::fill (rmsHist.begin(), rmsHist.end(), 0.0f);
        rmsPos = histPos = 0; rmsNow = blockEnergy = 0.0f; legatoCount = 0;
        ringPos = 0; ctrlCount = 0; refractory = 0; pendingNote = false;
        stableCount = 0; lastF0 = 0; noteF0 = 0;
        dryGain = 1.0f; fbGain = 0.0f; targetSm = 0.0f;
        main = -1; natTimer = 0; dwell = 0;
        meter.store (0.0f); shownHarmonic.store (0); shownHz.store (0.0f);
    }

    void process (float* const* io, int numCh, int n, const Params& p)
    {
        const float gateDb = p.sensitivity == 0 ? -50.0f : (p.sensitivity == 2 ? -66.0f : -58.0f);
        const float gateAmp = dbToLin (gateDb);
        // GAIN : niveau du larsen par rapport a l'attaque de la note (12 h ~ -4 dB, a fond +6 dB)
        const float g = std::clamp (p.gain, 0.0f, 1.0f);
        const float gainLin = 2.0f * std::pow (g, 1.6f);
        // ONSET : 30 ms a 6 s pour atteindre la pleine intensite (12 h ~ 0,42 s)
        const float onsetSec = 0.03f * std::pow (200.0f, std::clamp (p.onset, 0.0f, 1.0f));
        const float rate = 46.0f / onsetSec;                        // -40 dB -> +6 dB
        const bool engaged = p.active;
        const float fbTarget = engaged ? 1.0f : 0.0f;
        const float dryTarget = (! engaged || p.dry) ? 1.0f : 0.0f;
        const float smooth = 1.0f - std::exp (-1.0f / (0.012f * fs));
        const float smoothSlow = 1.0f - std::exp (-1.0f / (0.035f * fs));

        // mise en route / coupure du larsen
        if (engaged && ! wasEngaged) { engageKick = true; }
        if (! engaged && wasEngaged) for (auto& v : voices) v.startRelease (0.06f);
        wasEngaged = engaged;
        if (p.type != lastType && lastType >= 0 && engaged && noteAlive) typeChanged = true;
        lastType = p.type;

        float meterNow = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float xin = 0.0f;
            for (int c = 0; c < numCh; ++c) xin += io[c][i];
            xin /= (float) std::max (1, numCh);
            const float x = inHp.process (xin);

            blockEnergy += x * x;
            // enveloppes
            const float ax = std::abs (x);
            envFast += (ax - envFast) * (ax > envFast ? kFastA : kFastR);
            envSlow += (ax - envSlow) * (ax > envSlow ? kSlowA : kSlowR);

            // detection de hauteur (seulement quand il y a du signal)
            if (tracker.push (x, envSlow > gateAmp * 0.5f))
                onPitchFrame (p, rate, engaged);

            // controle toutes les 32 echantillons : attaques, etouffement, vie de la note
            if (++ctrlCount >= 32)
            {
                ctrlCount = 0;
                control (p, gateAmp, rate, engaged);
            }

            // cible du larsen (relative a l'attaque de la note), lissee (pedale d'expression)
            const float tgt = std::min (0.95f, notePeak * gainLin);
            targetSm += (tgt - targetSm) * smoothSlow;
            dryGain += (dryTarget - dryGain) * smooth;
            fbGain  += (fbTarget - fbGain) * smooth;

            const float floorAmp = std::max (1.0e-5f, notePeak * 0.0015f);
            float fb = 0.0f;
            for (auto& v : voices)
                fb += v.process (x, targetSm, floorAmp, p.type >= natLow);
            fb = outHp.process (fb) * fbGain;
            // limiteur doux : jamais de debordement meme avec deux voix en fondu
            const float afb = std::abs (fb);
            if (afb > 0.6f) fb = std::copysign (0.6f + 0.35f * std::tanh ((afb - 0.6f) / 0.35f), fb);

            for (int c = 0; c < numCh; ++c)
                io[c][i] = io[c][i] * dryGain + fb;
        }

        // echelle de LED : progression du larsen principal (centre = effet actif)
        if (main >= 0)
        {
            const float db = voices[main].progressDb();
            meterNow = std::clamp ((db + 30.0f) / 30.0f, 0.0f, 1.0f);
        }
        meter.store (engaged ? meterNow : 0.0f, std::memory_order_relaxed);
    }

    // pour la fenetre
    std::atomic<float> meter { 0.0f };
    std::atomic<int>   shownHarmonic { 0 };
    std::atomic<float> shownHz { 0.0f };

private:
    // ---------------------------------------------------------------- analyse de hauteur
    void onPitchFrame (const Params& p, float rate, bool engaged)
    {
        if (! tracker.voiced) { stableCount = 0; return; }
        const float f = tracker.f0;
        const float cents = lastF0 > 0.0f ? 1200.0f * std::abs (std::log2 (f / lastF0)) : 9999.0f;
        lastF0 = f;
        stableCount = cents < 45.0f ? stableCount + 1 : 0;

        if (stableCount < 1) return;                          // deux trames concordantes

        if (pendingNote)
        {
            if (noteAge < 0.035f) return;                     // fenetre d'analyse encore occupee par la note precedente
            // nouvelle note (apres une attaque) : on lance la floraison
            pendingNote = false;
            noteF0 = f;
            if (engaged) launch (p, -40.0f, rate, 0.05f);
            return;
        }
        if (! noteAlive) return;

        const float jump = noteF0 > 0.0f ? 1200.0f * std::log2 (f / noteF0) : 0.0f;
        if (std::abs (jump) < 70.0f)
        {
            legatoCount = 0;
            // vibrato, tire, glissando : la voix suit (le verrouillage de phase fait le detail)
            noteF0 = f;
            if (main >= 0 && voices[main].stage == Voice::bloom)
                voices[main].retune (lockFreqFor (voices[main].harmonic, f, p.type));
            return;
        }
        // changement de note sans attaque (legato, hammer-on) : le larsen glisse et se reforme en partie.
        // Il faut 3 trames concordantes, une note bien periodique et un niveau qui tient (pas une corde qui s'eteint)
        if (tracker.aperiodicity > 0.2f || rmsNow < notePeak * 0.02f) { legatoCount = 0; return; }
        const float dl = legatoF > 0.0f ? 1200.0f * std::abs (std::log2 (f / legatoF)) : 9999.0f;
        legatoF = f;
        legatoCount = dl < 45.0f ? legatoCount + 1 : 1;
        if (legatoCount < 3) return;
        legatoCount = 0;
        noteF0 = f;
        if (engaged)
        {
            if (main >= 0 && voices[main].stage == Voice::bloom && p.type != natLow && p.type != natHigh)
            {
                auto& v = voices[main];
                v.retune (lockFreqFor (v.harmonic == 0 ? 1 : v.harmonic, f, p.type), true);
                v.levelDb = std::max (-40.0f, std::min (v.levelDb, 6.0f) - 5.0f);   // le larsen se reforme sur la nouvelle note
                v.rateDb = rate * 1.6f;
                shownHz.store (v.lockHz * v.ratio);
            }
            else
            {
                float startDb = -40.0f;
                if (main >= 0 && voices[main].stage == Voice::bloom) startDb = std::max (-40.0f, voices[main].levelDb - 10.0f);
                launch (p, startDb, rate * 1.6f, 0.08f);
            }
        }
    }

    float lockFreqFor (int h, float f, int type) const
    {
        return type == sub ? f : f * (float) h;
    }

    int harmonicFor (int type) const
    {
        switch (type)
        {
            case sub:    return 0;
            case first:  return 1;
            case second: return 2;
            case third:  return 3;
            case fifth:  return 5;
            default:     return chooseNatural (type, -1);
        }
    }

    // NAT LOW / NAT HI : le partiel qui « gagne » depend de son energie dans la corde (attaque, micro, tonalite),
    // d'une courbe de resonance de la boucle, et d'un peu de hasard par note (comme un vrai larsen)
    int chooseNatural (int type, int current) const
    {
        static const int candLow[] = { 1, 2, 3, 4 };
        static const int candHigh[] = { 1, 2, 3, 4, 5, 6, 8 };
        const bool hi = type == natHigh;
        const int* cand = hi ? candHigh : candLow;
        const int nc = hi ? 7 : 4;
        // le centre de resonance descend quand la note vieillit (les aigus s'eteignent avant les graves)
        const float centre = hi ? std::max (330.0f, 1150.0f * std::pow (2.0f, -0.32f * noteAge)) : 300.0f;
        int best = cand[0];
        float bestScore = -1.0f, curScore = 0.0f;
        for (int k = 0; k < nc; ++k)
        {
            const int h = cand[k];
            const float hz = noteF0 * (float) h;
            if (hz > 7000.0f || hz < 35.0f) continue;
            const float amp = tracker.partialAmp (hz, 1024);
            const float oct = std::log2 (hz / centre);
            float w = std::exp (-0.5f * oct * oct / (1.15f * 1.15f));
            static const float prefLow[9]  = { 0, 1.0f, 1.15f, 0.9f, 0.75f, 0, 0, 0, 0 };
            static const float prefHigh[9] = { 0, 0.45f, 0.8f, 1.0f, 1.0f, 0.9f, 0.85f, 0, 0.7f };
            w *= hi ? prefHigh[h] : prefLow[h];
            const float score = std::pow (amp + 1.0e-7f, 0.8f) * w * noteRand[h];
            if (score > bestScore) { bestScore = score; best = h; }
            if (h == current) curScore = score;
        }
        if (current > 0)
        {
            const float hyst = hi ? 1.35f : 1.9f;
            if (bestScore < curScore * hyst) return current;
        }
        return best;
    }

    void launch (const Params& p, float startDb, float rateUsed, float fadeOld)
    {
        if (noteF0 <= 0.0f) return;
        int h = harmonicFor (p.type);
        float ratio = 1.0f, lockHz;
        if (h == 0)
        {
            // SUB : octave en dessous ; trop grave pour etre entendu -> unisson
            if (noteF0 * 0.5f < 25.0f) { h = 1; lockHz = noteF0; }
            else { lockHz = noteF0; ratio = 0.5f; }
        }
        else
        {
            // harmonique trop aigue (notes tres hautes) : on redescend d'un cran
            while (h > 1 && noteF0 * (float) h > 8500.0f) h = h == 5 ? 3 : h - 1;
            lockHz = noteF0 * (float) h;
        }
        startVoice (lockHz, ratio, h, startDb, rateUsed, fadeOld);
        natTimer = 0; dwell = 0;
    }

    void startVoice (float lockHz, float ratio, int h, float startDb, float rate, float fadeOld)
    {
        for (auto& v : voices) if (v.stage != Voice::idle) v.startRelease (fadeOld);
        // voix libre, sinon celle qui s'est le plus eteinte (jamais la voix principale en cours)
        int slot = -1;
        float lowest = 1.0e9f;
        for (int k = 0; k < numVoices; ++k)
        {
            if (voices[k].stage == Voice::idle) { slot = k; break; }
            const float l = voices[k].out * voices[k].relGain;
            if (k != main && l < lowest) { lowest = l; slot = k; }
        }
        if (slot < 0) slot = (main + 1) % numVoices;
        voices[slot].start (lockHz, ratio, h, startDb, rate, ++seedCounter);
        main = slot;
        shownHarmonic.store (ratio < 1.0f ? -1 : h);
        shownHz.store (lockHz * ratio);
    }

    void newNoteRandomness()
    {
        for (int h = 0; h < 9; ++h)
        {
            rng = rng * 1664525u + 1013904223u;
            noteRand[h] = 0.62f + 0.76f * (float) ((rng >> 9) & 0xFFFF) / 65535.0f;
        }
    }

    // ---------------------------------------------------------------- controle (toutes les 32 echantillons)
    void control (const Params& p, float gateAmp, float rate, bool engaged)
    {
        const float dt = 32.0f / fs;
        // RMS glissant sur ~20 ms
        rmsRing[(size_t) rmsPos] = blockEnergy; blockEnergy = 0.0f;
        rmsPos = (rmsPos + 1) % (int) rmsRing.size();
        float esum = 0.0f;
        for (float v : rmsRing) esum += v;
        rmsNow = std::sqrt (esum / (32.0f * (float) rmsRing.size()));
        rmsHist[(size_t) histPos] = rmsNow;
        histPos = (histPos + 1) % (int) rmsHist.size();
        ctrlRing[(size_t) ringPos] = envFast;
        ringPos = (ringPos + 1) % (int) ctrlRing.size();
        auto rmsPast = [this] (float sec)
        {
            const int sz = (int) rmsHist.size();
            const int k = std::clamp ((int) std::round (sec * fs / 32.0f), 1, sz - 1);
            return rmsHist[(size_t) ((histPos - 1 - k + sz * 4) % sz)];
        };
        auto past = [this] (float sec)
        {
            const int k = std::clamp ((int) std::round (sec * fs / 32.0f), 1, (int) ctrlRing.size() - 1);
            return ctrlRing[(size_t) ((ringPos - 1 - k + (int) ctrlRing.size() * 4) % (int) ctrlRing.size())];
        };
        if (refractory > 0) refractory -= dt;

        // attaque de mediator : l'enveloppe rapide double en ~15 ms et passe le seuil
        // reference : maximum de l'enveloppe 25 a 45 ms plus tot (couvre une periode des notes les plus graves)
        const float before = std::max (past (0.025f), std::max (past (0.035f), past (0.045f)));
        if (refractory <= 0.0f && envFast > gateAmp && envFast > 2.0f * before + gateAmp * 0.2f)
        {
            refractory = 0.07f;
            noteAlive = true; noteAge = 0.0f; notePeak = envFast;
            pendingNote = true; stableCount = 0;
            newNoteRandomness();
            // la corde est re-attaquee : le larsen en cours est coupe net puis se reconstruit
            for (auto& v : voices) v.startRelease (0.045f);
        }
        if (noteAlive)
        {
            noteAge += dt;
            if (noteAge < 0.09f) notePeak = std::max (notePeak, envFast);
            // corde etouffee : chute rapide du RMS (> 18 dB en 80 ms) ; un declin naturel est bien plus lent
            const float prev = rmsPast (0.08f);
            const bool muted = noteAge > 0.12f && prev > gateAmp * 0.7f && rmsNow < prev * 0.125f;
            // corde eteinte : sous le seuil, ou 56 dB sous l'attaque
            const bool dead = envSlow < std::max (gateAmp * 0.35f, notePeak * 0.0016f);
            if (muted || dead)
            {
                noteAlive = false; pendingNote = false;
                for (auto& v : voices) v.startRelease (muted ? 0.07f : 0.35f);
            }
        }

        // effet enclenche en cours de note (footswitch, mode momentane) : le larsen part de la note qui sonne
        if (engageKick)
        {
            engageKick = false;
            if (noteAlive && ! pendingNote && noteF0 > 0.0f) launch (p, -40.0f, rate, 0.05f);
        }
        // TYPE tourne pendant que ca larsene : passage sur la nouvelle harmonique
        if (typeChanged)
        {
            typeChanged = false;
            if (noteAlive && noteF0 > 0.0f && engaged)
            {
                float startDb = (main >= 0) ? std::max (-40.0f, voices[main].levelDb - 6.0f) : -40.0f;
                launch (p, startDb, rate * 1.5f, 0.09f);
            }
        }

        // modes naturels : le larsen peut sauter sur un autre partiel en cours de note
        if (engaged && noteAlive && (p.type == natLow || p.type == natHigh) && main >= 0
            && voices[main].stage == Voice::bloom && noteF0 > 0.0f)
        {
            natTimer += dt; dwell += dt;
            const float minDwell = p.type == natHigh ? 0.35f : 0.6f;
            if (natTimer > 0.025f && dwell > minDwell)
            {
                natTimer = 0.0f;
                const int cur = voices[main].harmonic;
                const int nh = chooseNatural (p.type, cur);
                if (nh != cur && nh * noteF0 < 8500.0f)
                {
                    const float startDb = std::max (-40.0f, std::min (voices[main].levelDb, 6.0f) - 12.0f);
                    startVoice (noteF0 * (float) nh, 1.0f, nh, startDb, rate * 2.0f, 0.09f);
                    dwell = 0.0f;
                }
            }
        }
        if (main >= 0 && voices[main].stage == Voice::idle) main = -1;
    }

    float fs = 48000.0f;
    PitchTracker tracker;
    Biquad inHp, outHp;
    static constexpr int numVoices = 3;
    Voice voices[numVoices];
    int main = -1;
    float envFast = 0, envSlow = 0, notePeak = 0, noteAge = 0, refractory = 0;
    bool noteAlive = false, pendingNote = false, wasEngaged = false, engageKick = false, typeChanged = false;
    int lastType = -1;
    std::vector<float> ctrlRing, rmsRing, rmsHist;
    int rmsPos = 0, histPos = 0, legatoCount = 0;
    float rmsNow = 0.0f, blockEnergy = 0.0f, legatoF = 0.0f;
    int ringPos = 0, ctrlCount = 0, stableCount = 0;
    float lastF0 = 0, noteF0 = 0;
    float dryGain = 1.0f, fbGain = 0.0f, targetSm = 0.0f;
    float kFastA = 0.07f, kFastR = 0.0007f, kSlowA = 0.004f, kSlowR = 0.0003f;
    float natTimer = 0, dwell = 0;
    float noteRand[9] = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };
    uint32_t rng = 0x9E3779B9u, seedCounter = 1;
};
} // namespace freakout
