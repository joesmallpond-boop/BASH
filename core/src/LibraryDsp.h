#pragma once
// Sound-design toolkit for the factory library (internal to the core).

#include "bash/Fx.h"
#include "bash/Library.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace bash::library::dsp
{
    constexpr double SR = renderRate;
    constexpr double TWO_PI = 6.283185307179586;

    struct Rng
    {
        uint64_t s;
        explicit Rng (uint64_t seed) : s (seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) { if (s == 0) s = 1; }
        float uni() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (float) ((double) (s >> 11) * (1.0 / 9007199254740992.0)); }
        float bi()  { return uni() * 2.0f - 1.0f; }
    };

    using Mono = std::vector<float>;
    inline Mono mono (double seconds) { return Mono ((size_t) std::max (1.0, seconds * SR), 0.0f); }
    inline double tOf (size_t i) { return (double) i / SR; }
    /** Every generator ends with a short fade so nothing stops mid-decay with a click. */
    inline void fadeEnd (std::vector<float>& x, double seconds = 0.008)
    {
        const size_t n = std::min (x.size(), (size_t) (seconds * SR));
        for (size_t i = 0; i < n; ++i) x[x.size() - 1 - i] *= (float) i / (float) n;
    }

    struct Buf
    {
        std::vector<float> l, r;
        Buf() = default;
        explicit Buf (double seconds) : l ((size_t) std::max (1.0, seconds * SR), 0.0f), r (l.size(), 0.0f) {}
        explicit Buf (const Mono& m) : l (m), r (m) {}
        size_t n() const { return l.size(); }
        void ensure (size_t len) { if (len > l.size()) { l.resize (len, 0.0f); r.resize (len, 0.0f); } }
        /** Mix another buffer in at sample `at` (equal-power pan). */
        void mix (const Buf& o, size_t at, float gain = 1.0f, float pan = 0.0f)
        {
            ensure (at + o.n());
            const float a = (float) ((pan + 1.0) * 0.25 * 3.14159265358979);
            const float gl = gain * std::cos (a) * 1.41421356f, gr = gain * std::sin (a) * 1.41421356f;
            for (size_t i = 0; i < o.n(); ++i) { l[at + i] += o.l[i] * gl; r[at + i] += o.r[i] * gr; }
        }
        void mixMono (const Mono& m, size_t at, float gain = 1.0f, float pan = 0.0f) { mix (Buf (m), at, gain, pan); }
        void gain (float g) { for (auto& v : l) v *= g; for (auto& v : r) v *= g; }
    };

    /** Zero-delay state-variable filter. */
    struct Svf
    {
        float ic1 = 0, ic2 = 0, lp = 0, bp = 0, hp = 0;
        void run (float x, double fc, float q)
        {
            const double g = std::tan (3.14159265358979 * std::clamp (fc, 10.0, SR * 0.49) / SR);
            const double k = 1.0 / std::max (0.05f, q);
            const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = (float) (2.0 * v1 - ic1); ic2 = (float) (2.0 * v2 - ic2);
            lp = (float) v2; bp = (float) v1; hp = (float) (x - k * v1 - v2);
        }
    };

    enum class F { LP, BP, HP };
    inline void filter (Mono& x, F type, double fc, float q = 0.707f)
    {
        Svf s;
        for (auto& v : x) { s.run (v, fc, q); v = type == F::LP ? s.lp : type == F::BP ? s.bp : s.hp; }
    }
    inline void filter (Buf& b, F type, double fc, float q = 0.707f) { filter (b.l, type, fc, q); filter (b.r, type, fc, q); }
    /** Filter with a cutoff that moves: fc(t). */
    template <typename Fn> void sweep (Mono& x, F type, Fn&& fc, float q = 0.707f)
    {
        Svf s;
        for (size_t i = 0; i < x.size(); ++i) { s.run (x[i], fc (tOf (i)), q); x[i] = type == F::LP ? s.lp : type == F::BP ? s.bp : s.hp; }
    }

    inline float env (double t, double tau) { return (float) std::exp (-t / std::max (1.0e-5, tau)); }
    inline float ramp (double t, double attack) { return attack <= 0.0 ? 1.0f : (float) std::min (1.0, t / attack); }

    inline void drive (Mono& x, float amount)
    {
        if (amount <= 0.0f) return;
        const float g = 1.0f + amount, norm = 1.0f / std::tanh (g);
        for (auto& v : x) v = std::tanh (v * g) * norm;
    }
    inline void drive (Buf& b, float amount) { drive (b.l, amount); drive (b.r, amount); }

    /** Run one of BASH!'s own effects over a buffer, with room for the tail. */
    inline void fx (Buf& b, FxType type, float a, float bKnob, float gnarl, float mix, double tailSeconds = 0.0, double bpm = 120.0)
    {
        b.ensure (b.n() + (size_t) (tailSeconds * SR));
        FxSlot slot;
        slot.prepare (SR, 512);
        FxParams p; p.type = type; p.a = a; p.b = bKnob; p.gnarl = gnarl; p.mix = mix;
        for (size_t i = 0; i < b.n(); i += 512)
        {
            const int n = (int) std::min<size_t> (512, b.n() - i);
            slot.process (b.l.data() + i, b.r.data() + i, n, p, bpm);
        }
    }

    /** Sine with an exponential pitch drop from f0 to f1. */
    inline Mono sweptSine (double seconds, double f0, double f1, double pitchTau, double ampTau, double attack = 0.001)
    {
        auto x = mono (seconds);
        double ph = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = tOf (i);
            const double f = f1 + (f0 - f1) * std::exp (-t / pitchTau);
            ph += f / SR;
            x[i] = (float) std::sin (TWO_PI * ph) * env (t, ampTau) * ramp (t, attack);
        }
        fadeEnd (x);
        return x;
    }

    inline Mono noise (double seconds, uint64_t seed, double ampTau, double attack = 0.0005)
    {
        Rng r (seed);
        auto x = mono (seconds);
        for (size_t i = 0; i < x.size(); ++i) { const double t = tOf (i); x[i] = r.bi() * env (t, ampTau) * ramp (t, attack); }
        fadeEnd (x);
        return x;
    }

    /** 808-style metal: six detuned square waves (cymbal/hat source). */
    inline Mono metal (double seconds, double base, double ampTau, double hp, uint64_t seed = 1, double attack = 0.0003)
    {
        static constexpr double ratios[6] = { 1.0, 1.4826, 1.8003, 2.5464, 2.6303, 3.8968 };
        Rng r (seed);
        double ph[6]; for (auto& p : ph) p = r.uni();
        auto x = mono (seconds);
        for (size_t i = 0; i < x.size(); ++i)
        {
            float s = 0.0f;
            for (int k = 0; k < 6; ++k) { ph[k] += base * ratios[k] / SR; ph[k] -= std::floor (ph[k]); s += ph[k] < 0.5 ? 1.0f : -1.0f; }
            const double t = tOf (i);
            x[i] = s / 6.0f * env (t, ampTau) * ramp (t, attack);
        }
        filter (x, F::BP, hp * 1.2, 0.8f);
        filter (x, F::HP, hp, 0.7f);
        fadeEnd (x);
        return x;
    }

    /** Two-operator FM hit. */
    inline Mono fmHit (double seconds, double fc, double ratio, double index, double indexTau, double ampTau, double attack = 0.0005)
    {
        auto x = mono (seconds);
        double pc = 0.0, pm = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = tOf (i);
            pm += fc * ratio / SR;
            const double m = std::sin (TWO_PI * pm) * index * std::exp (-t / indexTau);
            pc += fc / SR;
            x[i] = (float) std::sin (TWO_PI * pc + m) * env (t, ampTau) * ramp (t, attack);
        }
        fadeEnd (x);
        return x;
    }

    /** Band-limited-ish saw (polyBLEP). */
    struct Saw
    {
        double ph = 0.0;
        float next (double f)
        {
            const double dt = f / SR;
            ph += dt; if (ph >= 1.0) ph -= 1.0;
            double v = 2.0 * ph - 1.0;
            if (ph < dt) { const double t = ph / dt; v -= t + t - t * t - 1.0; }
            else if (ph > 1.0 - dt) { const double t = (ph - 1.0) / dt; v -= t * t + t + t + 1.0; }
            return (float) v;
        }
    };

    inline Mono squares (double seconds, std::initializer_list<double> freqs, double tau, double attack = 0.0005)
    {
        auto x = mono (seconds);
        std::vector<double> ph (freqs.size(), 0.0);
        for (size_t i = 0; i < x.size(); ++i)
        {
            float s = 0.0f; size_t k = 0;
            for (double f : freqs) { ph[k] += f / SR; ph[k] -= std::floor (ph[k]); s += ph[k] < 0.5 ? 1.0f : -1.0f; ++k; }
            const double t = tOf (i);
            x[i] = s / (float) freqs.size() * env (t, tau) * ramp (t, attack);
        }
        fadeEnd (x);
        return x;
    }

    inline Mono sawStack (double seconds, double f, std::initializer_list<double> detuneSemis, double attack, double tau)
    {
        auto x = mono (seconds);
        std::vector<Saw> saws (detuneSemis.size());
        for (size_t i = 0; i < x.size(); ++i)
        {
            float s = 0.0f; size_t k = 0;
            for (double d : detuneSemis) s += saws[k++].next (f * std::pow (2.0, d / 12.0));
            const double t = tOf (i);
            x[i] = s / (float) detuneSemis.size() * ramp (t, attack) * env (t, tau);
        }
        fadeEnd (x);
        return x;
    }

    inline void addInto (Mono& dst, const Mono& src, float gain = 1.0f, size_t at = 0)
    {
        if (dst.size() < at + src.size()) dst.resize (at + src.size(), 0.0f);
        for (size_t i = 0; i < src.size(); ++i) dst[at + i] += src[i] * gain;
    }

    inline void reverse (Buf& b) { std::reverse (b.l.begin(), b.l.end()); std::reverse (b.r.begin(), b.r.end()); }

    /** Haas widening: delay one side a little. */
    inline void widen (Buf& b, double ms)
    {
        const size_t d = (size_t) (ms * 0.001 * SR);
        b.ensure (b.n() + d);
        for (size_t i = b.n(); i-- > d;) b.r[i] = b.r[i - d];
        for (size_t i = 0; i < d; ++i) b.r[i] = 0.0f;
    }

    /** Sparse vinyl crackle plus hiss. */
    inline void vinyl (Buf& b, uint64_t seed, float amount)
    {
        Rng r (seed);
        Svf s;
        for (size_t i = 0; i < b.n(); ++i)
        {
            float v = r.bi() * 0.012f * amount;
            if (r.uni() < 0.0006f) v += r.bi() * 0.5f * amount;
            s.run (v, 3500.0, 0.6f);
            b.l[i] += s.lp; b.r[i] += s.lp;
        }
    }

    inline std::shared_ptr<SampleData> toSample (Buf& b, float peakDb, bool trimTail)
    {
        auto d = std::make_shared<SampleData>();
        d->sampleRate = SR;
        d->left = std::move (b.l);
        d->right = std::move (b.r);
        finish (*d, peakDb, trimTail);
        return d;
    }
}
