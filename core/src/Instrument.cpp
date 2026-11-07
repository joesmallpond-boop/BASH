#include "bash/Instrument.h"

#include "bash/AudioFile.h"
#include "bash/Library.h"
#include "bash/Warp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

// ---------------------------------------------------------------------------------------
// Messages the device sends (selector + arguments):
//   param <id> <value>               a live.* parameter changed (plain value)
//   key <root 0-11> <mask>           Live's key and scale (12-bit mask of the scale's notes)
//   note <pitch> <velocity>          MIDI in (velocity 0 = off)      allnotesoff
//   bend <-1..1>  touch <0..1>  modwheel <0..1>
//   load <path>                      a sample for the slicer          loop <index>  (library loop)
//   slices transients <0..1> | grid <n> | random <n> | clear
//   slice move <i> <0..1> | add <0..1> | delete <i> | play <i>
//   srcbpm <bpm> | half | double
//   pad select <i> | play <i> | load <i> <path> | sound <i> <index> | fm <i> <preset> | clear <i>
//   pad set <i> <field> <value>      field: tune gain pan decay start end cutoff choke reverse
//   kit lib <index> | fm <index> | libgroove <index>
//   step <lane> <i> toggle | vel <v> | ratchet | cond | prob   (drag / click / modifier clicks)
//   lane select <l> | algo <l> <a> | length <l> <n> | speed <l> <s> | mute <l> | roll <l> | gen <l>
//        | clear <l> | density <l> <0..1> | pulses <l> <n> | rotate <l> <n> | evolve <l> <0..1>
//   groove <index>   steps clear
//   regen   lock <i>   mute <i>   unlockall
//   dice    undo
//   restore <chunk> <ints...>        saved state coming back from the Live set
//   refresh                          resend everything (the device just opened)
// Messages it sends back (emit):
//   ui <id> <value>                  set a parameter (dice, grooves, kits, Live's key)
//   state <chunk> <ints...>          save this into the Live set
//   msg <text>                       status line
//   menu <name> <items...>           menu contents (loops, kits, sounds, grooves, fmkits)
//   seq ... / steps ... / wave ... / pads ... / frame ...   display data
// ---------------------------------------------------------------------------------------

namespace bash
{
    double atomNum (const Atom& a, double fallback)
    {
        if (const double* d = std::get_if<double> (&a)) return *d;
        const auto& s = std::get<std::string> (a);
        char* end = nullptr;
        const double v = std::strtod (s.c_str(), &end);
        return end != s.c_str() ? v : fallback;
    }

    std::string atomStr (const Atom& a)
    {
        if (const std::string* s = std::get_if<std::string> (&a)) return *s;
        const double d = std::get<double> (a);
        char buf[64];
        if (std::floor (d) == d && std::abs (d) < 1.0e15) std::snprintf (buf, sizeof buf, "%lld", (long long) d);
        else std::snprintf (buf, sizeof buf, "%g", d);
        return buf;
    }

    // ---- parameters -----------------------------------------------------------------
    namespace
    {
        float expFor (float lo, float hi, float centre)
        {
            const float frac = (centre - lo) / (hi - lo);
            return frac > 0.0f && frac < 1.0f ? std::log (frac) / std::log (0.5f) : 1.0f;
        }

        template <typename E, typename F>
        std::vector<std::string> enumNames (F nameOf)
        {
            std::vector<std::string> v;
            for (int i = 0; i < (int) E::count; ++i) v.push_back (nameOf ((E) i));
            return v;
        }

        constexpr int numModSlots = 6;
    }

    const std::vector<ParamSpec>& paramSpecs()
    {
        static const std::vector<ParamSpec> specs = []
        {
            std::vector<ParamSpec> p;
            using K = ParamSpec::Kind;
            auto f = [&] (const char* id, const char* name, const char* sh, float lo, float hi, float def, int group, const char* unit = "", float centre = -1.0f)
            {
                ParamSpec s; s.id = id; s.name = name; s.shortName = sh; s.kind = K::Float; s.min = lo; s.max = hi; s.def = def;
                s.group = group; s.unit = unit; s.exponent = centre > 0.0f ? expFor (lo, hi, centre) : 1.0f;
                p.push_back (s);
            };
            auto i = [&] (const char* id, const char* name, const char* sh, int lo, int hi, int def, int group)
            {
                ParamSpec s; s.id = id; s.name = name; s.shortName = sh; s.kind = K::Int; s.min = (float) lo; s.max = (float) hi; s.def = (float) def; s.group = group;
                p.push_back (s);
            };
            auto e = [&] (const char* id, const char* name, const char* sh, std::vector<std::string> ch, int def, int group)
            {
                ParamSpec s; s.id = id; s.name = name; s.shortName = sh; s.kind = K::Enum; s.min = 0.0f; s.max = (float) ch.size() - 1.0f;
                s.def = (float) def; s.choices = std::move (ch); s.group = group;
                p.push_back (s);
            };
            auto t = [&] (const char* id, const char* name, const char* sh, bool def, int group)
            {
                ParamSpec s; s.id = id; s.name = name; s.shortName = sh; s.kind = K::Toggle; s.min = 0.0f; s.max = 1.0f; s.def = def ? 1.0f : 0.0f; s.group = group;
                p.push_back (s);
            };

            std::vector<std::string> rates; for (auto& r : allRates()) rates.push_back (r.name);
            std::vector<std::string> keys; for (int k = 0; k < 12; ++k) keys.push_back (noteName (k));
            std::vector<std::string> scales; for (auto& d : allScales()) scales.push_back (d.name);
            std::vector<std::string> divs; for (auto& d : lfoDivisions()) divs.push_back (d.first);
            std::vector<std::string> sources { "Off" }; for (int s = 0; s < numModSources; ++s) sources.push_back (modSourceName ((ModSource) s));
            auto dests = enumNames<ModDest> (modDestName);

            // sequencer
            t ("seqon", "Sequencer On", "Seq", true, 0);
            e ("mode", "Sequencer Mode", "Mode", { "Generative", "Step" }, 0, 0);
            e ("algo", "Algorithm", "Algo", enumNames<Algorithm> (algorithmName), 0, 0);
            f ("type", "Type", "Type", 0.0f, 1.0f, 0.15f, 0);
            f ("density", "Density", "Density", 0.0f, 1.0f, 0.6f, 0);
            i ("steps", "Steps", "Steps", 1, 64, 16, 0);
            i ("offset", "Offset", "Offset", 0, 63, 0, 0);
            e ("direction", "Direction", "Dir", enumNames<Direction> (directionName), 0, 0);
            e ("rate", "Rate", "Rate", rates, 1, 0);
            f ("accent", "Accent", "Accent", 0.0f, 1.0f, 0.6f, 0);
            f ("gate", "Gate", "Gate", 0.05f, 2.0f, 0.5f, 0);
            i ("octave", "Octave", "Oct", 0, 6, 2, 0);
            f ("swing", "Swing", "Swing", 0.0f, 0.75f, 0.0f, 0);
            f ("jitter", "Jitter", "Jitter", 0.0f, 1.0f, 0.0f, 0);
            t ("scalelock", "Scale Lock", "ScLock", true, 0);
            t ("fill", "Fill", "Fill", false, 0);
            e ("autofill", "Auto Fill", "AutoFill", { "Off", "2 bars", "4 bars", "8 bars", "16 bars" }, 0, 0);
            t ("thru", "MIDI Thru", "Thru", true, 0);

            // clock + key
            t ("play", "Play", "Play", false, 3);
            t ("sync", "Sync to Live", "Sync", true, 3);
            f ("tempo", "Tempo", "Tempo", 40.0f, 300.0f, 128.0f, 3, "bpm");
            t ("keylock", "Follow Live Key", "KeyLock", true, 3);
            e ("root", "Key", "Key", keys, 0, 3);
            e ("scale", "Scale", "Scale", scales, 0, 3);

            // voice
            e ("engine", "Voice", "Voice", { "Synth", "Slicer", "Drums" }, 0, 1);
            e ("machine", "Machine", "Machine", enumNames<Machine> (machineName), 0, 1);
            f ("wave", "Character", "Char", 0.0f, 1.0f, 0.0f, 1);
            f ("timbre", "Timbre", "Timbre", 0.0f, 1.0f, 0.5f, 1);
            e ("ftype", "Filter Type", "Filter", enumNames<FilterType> (filterTypeName), 0, 1);
            f ("cutoff", "Cutoff", "Cutoff", 30.0f, 8000.0f, 380.0f, 1, "Hz", 500.0f);
            f ("reso", "Resonance", "Reso", 0.0f, 1.0f, 0.72f, 1);
            f ("envmod", "Env Mod", "EnvMod", 0.0f, 1.0f, 0.55f, 1);
            f ("decay", "Decay", "Decay", 0.02f, 2.0f, 0.3f, 1, "s", 0.3f);
            f ("drive", "Drive", "Drive", 0.0f, 1.0f, 0.35f, 1);
            f ("glide", "Glide", "Glide", 0.005f, 0.5f, 0.06f, 1, "s", 0.06f);
            f ("aatk", "Amp Attack", "Attack", 0.0005f, 2.0f, 0.001f, 1, "s", 0.05f);
            f ("adec", "Amp Decay", "ADecay", 0.01f, 4.0f, 0.5f, 1, "s", 0.4f);
            f ("asus", "Amp Sustain", "Sustain", 0.0f, 1.0f, 1.0f, 1);
            f ("arel", "Amp Release", "Release", 0.002f, 4.0f, 0.01f, 1, "s", 0.2f);
            e ("slicemode", "Slice Mode", "SlMode", enumNames<SliceMode> (sliceModeName), 0, 1);
            i ("winstart", "Window Start", "WinStart", 1, 256, 1, 1);
            i ("winsize", "Window Size", "WinSize", 1, 256, 4, 1);
            t ("pitchfollow", "Pitch Follow", "PitchFol", false, 1);
            f ("tune", "Tune", "Tune", -24.0f, 24.0f, 0.0f, 1, "st");
            f ("sattack", "Slice Attack", "SlAtk", 0.0005f, 0.5f, 0.002f, 1, "s", 0.02f);
            f ("srelease", "Slice Release", "SlRel", 0.002f, 2.0f, 0.03f, 1, "s", 0.1f);
            t ("reverse", "Reverse", "Reverse", false, 1);
            t ("warp", "Warp", "Warp", true, 1);
            f ("level", "Level", "Level", 0.0f, 1.0f, 0.8f, 1);
            f ("pan", "Pan", "Pan", -1.0f, 1.0f, 0.0f, 1);

            // modulation
            for (int k = 1; k <= 2; ++k)
            {
                const std::string n = "lfo" + std::to_string (k), N = "LFO " + std::to_string (k);
                static std::vector<std::string> keep;   // ids need stable storage for the lambdas' const char*
                auto add = [&] (const std::string& suffix) { keep.push_back (n + suffix); return keep.back(); };
                (void) add;
                ParamSpec s;
                s = {}; s.id = n + "shape"; s.name = N + " Shape"; s.shortName = "L" + std::to_string (k) + "Shape"; s.kind = K::Enum;
                s.choices = enumNames<LfoShape> (lfoShapeName); s.max = (float) s.choices.size() - 1.0f; s.def = k == 1 ? 0.0f : 5.0f; s.group = 2; p.push_back (s);
                s = {}; s.id = n + "rate"; s.name = N + " Rate"; s.shortName = "L" + std::to_string (k) + "Rate"; s.kind = K::Float;
                s.min = 0.01f; s.max = 20.0f; s.def = k == 1 ? 0.5f : 0.2f; s.exponent = expFor (0.01f, 20.0f, 1.0f); s.unit = "Hz"; s.group = 2; p.push_back (s);
                s = {}; s.id = n + "sync"; s.name = N + " Sync"; s.shortName = "L" + std::to_string (k) + "Sync"; s.kind = K::Toggle; s.max = 1.0f; s.group = 2; p.push_back (s);
                s = {}; s.id = n + "div"; s.name = N + " Division"; s.shortName = "L" + std::to_string (k) + "Div"; s.kind = K::Enum;
                s.choices = divs; s.max = (float) divs.size() - 1.0f; s.def = 4.0f; s.group = 2; p.push_back (s);
            }
            f ("liferate", "Life Rate", "Life", 0.005f, 2.0f, 0.08f, 2, "Hz", 0.1f);
            for (int k = 1; k <= numModSlots; ++k)
            {
                const std::string n = "m" + std::to_string (k), N = "Mod " + std::to_string (k);
                ParamSpec s;
                s = {}; s.id = n + "src"; s.name = N + " Source"; s.shortName = N + " Src"; s.kind = K::Enum; s.choices = sources;
                s.max = (float) sources.size() - 1.0f; s.def = 0.0f; s.group = 2; p.push_back (s);
                s = {}; s.id = n + "dst"; s.name = N + " Destination"; s.shortName = N + " Dst"; s.kind = K::Enum; s.choices = dests;
                s.max = (float) dests.size() - 1.0f; s.def = (float) (k == 1 ? (int) ModDest::Cutoff : k == 2 ? (int) ModDest::Density : 0); s.group = 2; p.push_back (s);
                s = {}; s.id = n + "amt"; s.name = N + " Amount"; s.shortName = N + " Amt"; s.kind = K::Float; s.min = -1.0f; s.max = 1.0f; s.def = 0.0f; s.group = 2; p.push_back (s);
            }
            return p;
        }();
        return specs;
    }

    int paramIndex (const std::string& id)
    {
        const auto& s = paramSpecs();
        for (int i = 0; i < (int) s.size(); ++i) if (s[(size_t) i].id == id) return i;
        return -1;
    }

    // ---- life cycle ---------------------------------------------------------------------
    Instrument::Instrument()
        : values (paramSpecs().size())
    {
        for (size_t i = 0; i < values.size(); ++i) values[i].store (paramSpecs()[i].def);
        rngState ^= (uint64_t) std::chrono::steady_clock::now().time_since_epoch().count();
        loadFmKit (0);
        { std::lock_guard<SpinMutex> g (eng.stepMutex()); eng.stepPattern().clear(); }
        pull();
    }

    Instrument::~Instrument()
    {
        if (warpThread.joinable()) warpThread.join();
    }

    uint64_t Instrument::nextRandom()
    {
        rngState = rngState * 6364136223846793005ull + 1442695040888963407ull;
        return mix64 (rngState);
    }

    void Instrument::prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        eng.prepare (sampleRate, maxBlock);
        prepared = true;
    }

    float Instrument::param (const std::string& id) const
    {
        const int i = paramIndex (id);
        return i >= 0 ? values[(size_t) i].load() : 0.0f;
    }

    // ---- audio thread -----------------------------------------------------------------------
    void Instrument::pull()
    {
        auto v = [this] (const char* id) { return values[(size_t) paramIndex (id)].load (std::memory_order_relaxed); };
        auto& s = eng.settings();
        s.sequencerOn = v ("seqon") > 0.5f;
        s.stepMode    = v ("mode") > 0.5f;
        auto& q = s.seq;
        q.algorithm    = (Algorithm) std::clamp ((int) v ("algo"), 0, (int) Algorithm::count - 1);
        q.type         = v ("type");
        q.density      = v ("density");
        q.length       = (uint32_t) std::clamp ((int) v ("steps"), 1, 64);
        q.offset       = (uint32_t) std::max (0, (int) v ("offset"));
        q.direction    = (Direction) std::clamp ((int) v ("direction"), 0, (int) Direction::count - 1);
        q.accentAmount = v ("accent");
        q.gate         = v ("gate");
        q.baseNote     = 12 * ((int) v ("octave") + 1) + (int) v ("root");
        q.scaleLock    = v ("scalelock") > 0.5f;
        q.sliceMode    = (SliceMode) std::clamp ((int) v ("slicemode"), 0, (int) SliceMode::count - 1);
        q.windowStart  = (uint32_t) std::max (0, (int) v ("winstart") - 1);
        q.windowSize   = (uint32_t) std::max (1, (int) v ("winsize"));
        q.swing        = v ("swing");
        q.jitter       = v ("jitter");
        s.rateIndex    = (int) v ("rate");
        s.fill         = v ("fill") > 0.5f;
        static constexpr int fillBars[] = { 0, 2, 4, 8, 16 };
        s.autoFillBars = fillBars[std::clamp ((int) v ("autofill"), 0, 4)];
        s.midiThru     = v ("thru") > 0.5f;

        s.playing    = v ("play") > 0.5f;
        s.hostSync   = v ("sync") > 0.5f;
        s.tempo      = v ("tempo");
        s.root       = (int) v ("root");
        s.scaleIndex = (int) v ("scale");

        s.engine = (EngineType) std::clamp ((int) v ("engine"), 0, (int) EngineType::count - 1);
        auto& vp = s.voice;
        vp.machine    = (Machine) std::clamp ((int) v ("machine"), 0, (int) Machine::count - 1);
        vp.character  = v ("wave");
        vp.timbre     = v ("timbre");
        vp.filterType = (FilterType) std::clamp ((int) v ("ftype"), 0, (int) FilterType::count - 1);
        vp.cutoff     = v ("cutoff");
        vp.resonance  = v ("reso");
        vp.envMod     = v ("envmod");
        vp.decay      = v ("decay");
        vp.accent     = v ("accent");
        vp.drive      = v ("drive");
        vp.glide      = v ("glide");
        vp.attack     = v ("aatk");
        vp.ampDecay   = v ("adec");
        vp.sustain    = v ("asus");
        vp.release    = v ("arel");
        s.slicer.pitchFollow   = v ("pitchfollow") > 0.5f;
        s.slicer.tune          = v ("tune");
        s.slicer.voice.attack  = v ("sattack");
        s.slicer.voice.release = v ("srelease");
        s.slicer.voice.reverse = v ("reverse") > 0.5f;
        s.level = v ("level");
        s.pan   = v ("pan");

        for (int k = 0; k < 2; ++k)
        {
            const std::string n = "lfo" + std::to_string (k + 1);
            auto& lp = s.lfo[(size_t) k];
            lp.shape    = (LfoShape) std::clamp ((int) v ((n + "shape").c_str()), 0, (int) LfoShape::count - 1);
            lp.rateHz   = v ((n + "rate").c_str());
            lp.sync     = v ((n + "sync").c_str()) > 0.5f;
            lp.division = (int) v ((n + "div").c_str());
        }
        s.lifeRate = v ("liferate");

        // mod slots -> the matrix
        float m[numModDests][numModSources] {};
        for (int k = 1; k <= numModSlots; ++k)
        {
            const std::string n = "m" + std::to_string (k);
            const int src = (int) v ((n + "src").c_str()) - 1;
            const int dst = (int) v ((n + "dst").c_str());
            if (src < 0 || src >= numModSources || dst < 0 || dst >= numModDests) continue;
            m[dst][src] += v ((n + "amt").c_str());
        }
        for (int d = 0; d < numModDests; ++d)
            for (int sIdx = 0; sIdx < numModSources; ++sIdx)
                eng.matrix().set ((ModDest) d, (ModSource) sIdx, std::clamp (m[d][sIdx], -1.0f, 1.0f));
    }

    void Instrument::process (float* left, float* right, int n, const Transport& tp)
    {
        pull();
        eng.process (left, right, n, tp);
    }

    // ---- parameters from the device / to the device ----------------------------------------
    void Instrument::setParamValue (int index, float v, bool fromUi)
    {
        if (index < 0 || index >= (int) values.size()) return;
        const auto& s = paramSpecs()[(size_t) index];
        v = std::clamp (v, s.min, s.max);
        if (s.kind != ParamSpec::Kind::Float) v = std::round (v);
        const float old = values[(size_t) index].exchange (v);
        if (! fromUi) emitParam (s.id, v);
        if (old == v) return;

        // side effects
        if (s.id == "engine" || s.id == "root" || s.id == "scale" || s.id == "octave" || s.id == "scalelock"
            || s.id == "winstart" || s.id == "mode")
            viewDirty = true;
        if (s.id == "steps" || s.id == "type" || s.id == "density" || s.id == "algo" || s.id == "offset" || s.id == "direction"
            || s.id == "accent" || s.id == "gate" || s.id == "slicemode" || s.id == "winsize")
            viewDirty = true;
        if (s.id == "warp" || s.id == "sync" || s.id == "tempo") updateWarp (false);
        if (s.id == "engine") { emitPads(); emitWave(); }
        if (s.id == "keylock" && v > 0.5f && lastLiveRoot >= 0) keyFromLive (lastLiveRoot, lastLiveMask);
    }

    void Instrument::emitParam (const std::string& id, float v)
    {
        emit ("ui", { id, (double) v });
    }

    // ---- key lock --------------------------------------------------------------------------
    int Instrument::scaleForMask (int mask)
    {
        // pick the BASH! scale with exactly Live's notes, or else the closest one
        mask |= 1;
        const auto& scales = allScales();
        int best = 0;
        float bestScore = -1.0f;
        auto bits = [] (int x) { int c = 0; while (x) { c += x & 1; x >>= 1; } return c; };
        for (int i = 0; i < (int) scales.size(); ++i)
        {
            int m = 0;
            for (int iv : scales[(size_t) i].intervals) m |= 1 << (iv % 12);
            if (m == mask) return i;
            const float score = (float) bits (m & mask) / (float) std::max (1, bits (m | mask));
            if (score > bestScore) { bestScore = score; best = i; }
        }
        return best;
    }

    void Instrument::keyFromLive (int root, int mask)
    {
        lastLiveRoot = root; lastLiveMask = mask;
        if (param ("keylock") < 0.5f) return;
        const int r = ((root % 12) + 12) % 12;
        const int s = scaleForMask (mask);
        if ((int) param ("root") != r) setParamValue (paramIndex ("root"), (float) r, false);
        if ((int) param ("scale") != s) setParamValue (paramIndex ("scale"), (float) s, false);
    }

    // ---- small helpers ------------------------------------------------------------------------
    Scale Instrument::scaleNow() const { return Scale ((int) param ("root"), (int) param ("scale")); }

    EngineType Instrument::engineType() const
    {
        return (EngineType) std::clamp ((int) param ("engine"), 0, (int) EngineType::count - 1);
    }

    SequencerParams Instrument::uiSeqParams()
    {
        SequencerParams q;
        q.algorithm    = (Algorithm) std::clamp ((int) param ("algo"), 0, (int) Algorithm::count - 1);
        q.type         = param ("type");
        q.density      = param ("density");
        q.length       = (uint32_t) std::clamp ((int) param ("steps"), 1, 64);
        q.offset       = (uint32_t) std::max (0, (int) param ("offset"));
        q.direction    = (Direction) std::clamp ((int) param ("direction"), 0, (int) Direction::count - 1);
        q.accentAmount = param ("accent");
        q.gate         = param ("gate");
        q.baseNote     = 12 * ((int) param ("octave") + 1) + (int) param ("root");
        q.scaleLock    = param ("scalelock") > 0.5f;
        q.sliceMode    = (SliceMode) std::clamp ((int) param ("slicemode"), 0, (int) SliceMode::count - 1);
        q.windowStart  = (uint32_t) std::max (0, (int) param ("winstart") - 1);
        q.windowSize   = (uint32_t) std::max (1, (int) param ("winsize"));
        q.swing        = param ("swing");
        q.jitter       = param ("jitter");
        const auto e = engineType();
        if (e == EngineType::Slicer && playing != nullptr) q.sliceCount = (uint32_t) eng.slices().size();
        if (e == EngineType::Drums) { q.sliceCount = numCells; q.drumTrack = true; }
        return q;
    }

    LaneRole Instrument::laneRole (int lane) const
    {
        return engineType() == EngineType::Drums ? drumLaneRole (lane) : LaneRole::Melodic;
    }

    std::string Instrument::laneName (int lane)
    {
        switch (engineType())
        {
            case EngineType::Drums:
                return ! cells[(size_t) lane].name.empty() ? cells[(size_t) lane].name : "Pad " + std::to_string (lane + 1);
            case EngineType::Slicer:
                return "Slice " + std::to_string ((int) param ("winstart") + lane);
            default:
            {
                const auto sp = uiSeqParams();
                const auto sc = scaleNow();
                const int note = sp.scaleLock ? sc.stepFrom (sp.baseNote, lane) : sp.baseNote + lane;
                return std::string (noteName (((note % 12) + 12) % 12)) + std::to_string (note / 12 - 1);
            }
        }
    }

    void Instrument::editSteps (const std::function<void (StepPattern&)>& fn)
    {
        StepPattern p;
        { std::lock_guard<SpinMutex> g (eng.stepMutex()); p = eng.stepPattern(); }
        fn (p);
        { std::lock_guard<SpinMutex> g (eng.stepMutex()); eng.stepPattern() = p; }
        changed();
    }

    void Instrument::retire (std::shared_ptr<SampleData> s)
    {
        if (s != nullptr) graveyard.push_back ({ std::move (s), eng.blocksProcessed() });
    }

    // ---- slicer ---------------------------------------------------------------------------------
    double Instrument::desiredWarpRatio() const
    {
        if (original == nullptr || param ("warp") < 0.5f || srcBpm <= 0.0) return 1.0;
        const double target = param ("sync") > 0.5f ? std::max (1.0, eng.currentBpm()) : param ("tempo");
        return warp::ratioFor (srcBpm, target);
    }

    std::string Instrument::loadSlicerSample (const std::string& path, bool autoSlice, double knownBpm, bool fromLibraryLoop, int loopIndex)
    {
        std::shared_ptr<SampleData> data;
        std::string error;
        if (fromLibraryLoop)
        {
            const auto& loops = library::loops();
            if (loopIndex < 0 || loopIndex >= (int) loops.size()) return "No such loop";
            data = loops[(size_t) loopIndex].render();
            if (data == nullptr) return "Couldn't build that loop";
            data->path = "lib:" + library::loopPath (loops[(size_t) loopIndex]);
        }
        else
        {
            data = decodeAudioFile (path, 600.0, error);   // up to 10 minutes
            if (data == nullptr) return error;
        }

        waitForWarp();
        original = data;
        libraryLoop = fromLibraryLoop ? loopIndex : -1;
        missingSample.clear();
        std::string base = path;
        if (const auto slash = base.find_last_of ("/\\:"); slash != std::string::npos) base = base.substr (slash + 1);
        if (const auto dot = base.find_last_of ('.'); dot != std::string::npos) base = base.substr (0, dot);
        const double fromName = fromLibraryLoop ? 0.0 : warp::tempoFromName (base);
        srcBpm = knownBpm > 0.0 ? knownBpm : fromName > 0.0 ? fromName : warp::detectTempo (*data).bpm;

        const double ratio = desiredWarpRatio();
        requestedRatio = ratio;
        installSlicer (std::abs (ratio - 1.0) > 1.0e-4 ? warp::stretch (*data, ratio) : data, ratio);
        if (autoSlice)
        {
            auto s = slicing::transients (*playing, 0.5f);
            if (s.size() < 2) s = slicing::grid (playing->numFrames(), 16);   // no clear hits: an even grid
            setSlices (std::move (s));
        }
        emitWave();
        changed();
        return {};
    }

    void Instrument::installSlicer (std::shared_ptr<SampleData> next, double ratio)
    {
        // slices keep their place in the music: scale them with the new length
        auto sl = eng.slices();
        if (playing != nullptr && next != nullptr && playing->numFrames() > 0)
        {
            const double scale = (double) next->numFrames() / (double) playing->numFrames();
            for (auto& f : sl) f = (uint32_t) std::llround (f * scale);
        }
        retire (playing);
        playing = std::move (next);
        appliedRatio = ratio;
        eng.setSample (playing.get());
        setSlices (std::move (sl));
    }

    void Instrument::clearSlicer()
    {
        waitForWarp();
        retire (playing);
        playing = nullptr;
        original = nullptr;
        srcBpm = 0.0;
        appliedRatio = requestedRatio = 1.0;
        libraryLoop = -1;
        eng.setSample (nullptr);
        eng.setSlices ({});
        emitWave();
        changed();
    }

    void Instrument::setSlices (std::vector<uint32_t> s)
    {
        slicing::normalise (s, playing != nullptr ? playing->numFrames() : 0u);
        eng.setSlices (std::move (s));
        viewDirty = true;
    }

    void Instrument::waitForWarp()
    {
        if (warpThread.joinable()) warpThread.join();
        warpThreadBusy.store (false);
    }

    void Instrument::updateWarp (bool synchronous)
    {
        if (original == nullptr) return;
        const double want = desiredWarpRatio();
        if (std::abs (want - requestedRatio) < 1.0e-4) return;
        requestedRatio = want;
        auto src = original;
        if (synchronous)
        {
            waitForWarp();
            installSlicer (std::abs (want - 1.0) > 1.0e-4 ? warp::stretch (*src, want) : src, want);
            emitWave();
            return;
        }
        if (warpThreadBusy.load()) return;   // tick() asks again when this one is done
        if (warpThread.joinable()) warpThread.join();
        warpThreadBusy.store (true);
        warpThread = std::thread ([this, src, want]
        {
            auto result = std::abs (want - 1.0) > 1.0e-4 ? warp::stretch (*src, want) : src;
            {
                std::lock_guard<std::mutex> g (warpMutex);
                warpReady = result;
                warpReadyRatio = want;
            }
            warpThreadBusy.store (false);
        });
    }

    // ---- drum kit ----------------------------------------------------------------------------
    void Instrument::setCellSample (int i, std::shared_ptr<SampleData> data, const std::string& name, const std::string& path, int libSound)
    {
        DrumCell c = eng.cell (i);
        const DrumCell old = c;
        c.kind = DrumCell::Kind::Sample;
        c.sample = data.get();
        if (old.kind != DrumCell::Kind::Sample) { c.start = 0.0f; c.end = 1.0f; c.tune = 0.0f; c.cutoff = 20000.0f; c.reverse = false; c.gain = 1.0f; c.pan = 0.0f; }
        retire (cells[(size_t) i].sample);
        cells[(size_t) i] = { data, name, path, libSound, -1 };
        eng.setCell (i, c);
        changed();
    }

    std::string Instrument::loadCellFile (int i, const std::string& path)
    {
        if (i < 0 || i >= numCells) return {};
        std::string error;
        auto data = decodeAudioFile (path, 60.0, error);   // one-shots: up to a minute
        if (data == nullptr) return error;
        std::string name = path;
        if (const auto slash = name.find_last_of ("/\\:"); slash != std::string::npos) name = name.substr (slash + 1);
        if (const auto dot = name.find_last_of ('.'); dot != std::string::npos) name = name.substr (0, dot);
        setCellSample (i, data, name, path, -1);
        return {};
    }

    void Instrument::setCellFm (int i, int preset)
    {
        const auto& presets = fmPresets();
        if (i < 0 || i >= numCells || preset < 0 || preset >= (int) presets.size()) return;
        retire (cells[(size_t) i].sample);
        cells[(size_t) i] = { nullptr, presets[(size_t) preset].name, {}, -1, preset };
        eng.setCell (i, makeFmCell (presets[(size_t) preset]));
        changed();
    }

    void Instrument::clearCell (int i)
    {
        if (i < 0 || i >= numCells) return;
        retire (cells[(size_t) i].sample);
        cells[(size_t) i] = {};
        eng.setCell (i, DrumCell());
        changed();
    }

    void Instrument::loadFmKit (int k)
    {
        const auto& kits = factoryKits();
        if (k < 0 || k >= (int) kits.size()) return;
        for (int i = 0; i < numCells; ++i)
        {
            if (kits[(size_t) k].cells[(size_t) i] >= 0) setCellFm (i, kits[(size_t) k].cells[(size_t) i]);
            else clearCell (i);
        }
        kitName = kits[(size_t) k].name;
    }

    void Instrument::loadLibraryKit (int k, bool withGroove)
    {
        const auto& kits = library::kits();
        if (k < 0 || k >= (int) kits.size()) return;
        const auto& kit = kits[(size_t) k];
        for (int i = 0; i < numCells; ++i)
        {
            const auto& pad = kit.pads[(size_t) i];
            if (pad.sound.empty()) { clearCell (i); continue; }
            const auto* shot = library::findOneShot (pad.sound);
            if (shot == nullptr) { clearCell (i); continue; }
            const auto& all = library::oneShots();
            const int index = (int) (shot - all.data());
            auto data = shot->render();
            if (data == nullptr) { clearCell (i); continue; }
            data->path = "lib:" + library::oneShotPath (*shot);
            setCellSample (i, data, shot->name, data->path, index);
            auto c = eng.cell (i);
            c.tune = pad.tune;
            c.gain = std::pow (10.0f, pad.gainDb / 20.0f);
            c.pan = pad.pan;
            c.choke = pad.choke;
            eng.setCell (i, c);
        }
        kitName = kit.name;
        if (withGroove && ! kit.groove.empty())
            for (int g = 0; g < (int) grooves::all().size(); ++g)
                if (grooves::all()[(size_t) g].name == kit.groove) { applyGroove (g); break; }
        changed();
    }

    void Instrument::setCellField (int i, const std::string& field, float v)
    {
        if (i < 0 || i >= numCells) return;
        auto c = eng.cell (i);
        if (field == "tune")         { c.tune = std::clamp (v, -24.0f, 24.0f); if (c.kind == DrumCell::Kind::Fm) c.fm.pitch = std::max (20.0f, c.fm.pitch); }
        else if (field == "gain")    c.gain = std::clamp (v, 0.0f, 2.0f);
        else if (field == "pan")     c.pan = std::clamp (v, -1.0f, 1.0f);
        else if (field == "start")   c.start = std::clamp (v, 0.0f, c.end - 0.001f);
        else if (field == "end")     c.end = std::clamp (v, c.start + 0.001f, 1.0f);
        else if (field == "cutoff")  c.cutoff = std::clamp (v, 40.0f, 20000.0f);
        else if (field == "choke")   c.choke = std::clamp ((int) std::lround (v), 0, 8);
        else if (field == "reverse") c.reverse = v > 0.5f;
        else if (field == "decay")
        {
            if (c.kind == DrumCell::Kind::Fm) c.fm.decay = std::clamp (v, 0.01f, 4.0f);
            else c.fadeOut = std::clamp (v, 0.001f, 4.0f);
        }
        else if (field == "pitch")   c.fm.pitch = std::clamp (v, 20.0f, 2000.0f);
        else if (field == "noise")   c.fm.noise = std::clamp (v, 0.0f, 1.0f);
        else if (field == "fm")      c.fm.fmAmt = std::clamp (v, 0.0f, 20.0f);
        else return;
        eng.setCell (i, c);
        changed();
    }

    void Instrument::emitPad (int i)
    {
        if (i < 0 || i >= numCells) return;
        const auto c = eng.cell (i);
        const bool fm = c.kind == DrumCell::Kind::Fm;
        emit ("padedit", { (double) i, cells[(size_t) i].name.empty() ? std::string ("(empty)") : cells[(size_t) i].name,
                           (double) (int) c.kind, (double) c.tune, (double) c.gain, (double) c.pan,
                           (double) (fm ? c.fm.decay : c.fadeOut), (double) c.start, (double) c.end, (double) c.cutoff,
                           (double) c.choke, (double) (c.reverse ? 1 : 0) });
    }

    // ---- step sequencer ------------------------------------------------------------------------
    void Instrument::stepCommand (const Atoms& a)
    {
        // step <lane> <i> toggle | vel <v> | ratchet | cond | prob | on <0/1>
        if (a.size() < 3) return;
        const int lane = std::clamp ((int) atomNum (a[0]), 0, stepLanes - 1);
        const int i = std::clamp ((int) atomNum (a[1]), 0, maxSteps - 1);
        const auto what = atomStr (a[2]);
        const double v = a.size() > 3 ? atomNum (a[3]) : 0.0;
        selectedLane = lane;
        editSteps ([&] (StepPattern& p)
        {
            auto& s = p.lanes[(size_t) lane].steps[(size_t) i];
            if (what == "toggle")       s.on = ! s.on;
            else if (what == "on")      s.on = v > 0.5;
            else if (what == "vel")     { s.vel = (uint8_t) std::clamp ((int) v, 1, 127); s.on = true; }
            else if (what == "ratchet") { s.ratchet = (uint8_t) (s.ratchet >= 4 ? 1 : s.ratchet + 1); s.on = true; }
            else if (what == "cond")    s.cond = (TrigCond) (((int) s.cond + 1) % (int) TrigCond::count);
            else if (what == "prob")    s.prob = (uint8_t) (s.prob <= 25 ? 100 : s.prob - 25);
            else if (what == "nudge")   s.nudge = (int8_t) std::clamp ((int) v, -50, 50);
        });
    }

    void Instrument::laneCommand (const Atoms& a)
    {
        if (a.empty()) return;
        const auto what = atomStr (a[0]);
        const int lane = a.size() > 1 ? std::clamp ((int) atomNum (a[1]), 0, stepLanes - 1) : selectedLane;
        const double v = a.size() > 2 ? atomNum (a[2]) : 0.0;
        if (what == "select") { selectedLane = lane; viewDirty = true; return; }
        selectedLane = lane;
        const auto role = laneRole (lane);
        if (what == "roll") pushUndo();
        editSteps ([&] (StepPattern& p)
        {
            auto& l = p.lanes[(size_t) lane];
            if (what == "algo")         l.algo = (LaneAlgo) std::clamp ((int) v, 0, (int) LaneAlgo::count - 1);
            else if (what == "length")  l.length = (uint8_t) std::clamp ((int) v, 1, maxSteps);
            else if (what == "speed")   l.speed = (uint8_t) std::clamp ((int) v, 0, (int) laneSpeeds().size() - 1);
            else if (what == "mute")    l.mute = a.size() > 2 ? v > 0.5 : ! l.mute;
            else if (what == "density") l.density = (float) std::clamp (v, 0.0, 1.0);
            else if (what == "pulses")  l.pulses = (uint8_t) std::clamp ((int) v, 0, maxSteps);
            else if (what == "rotate")  l.rotate = (uint8_t) std::clamp ((int) v, 0, maxSteps - 1);
            else if (what == "evolve")  l.evolve = (float) std::clamp (v, 0.0, 1.0);
            else if (what == "roll")    stepgen::rollLane (l, role, nextRandom());
            else if (what == "gen")     stepgen::generate (l, role, nextRandom());
            else if (what == "clear")   { for (auto& s : l.steps) s = Step{}; }
        });
    }

    void Instrument::applyGroove (int index)
    {
        const auto& all = grooves::all();
        if (index < 0 || index >= (int) all.size()) return;
        const auto& g = all[(size_t) index];
        pushUndo();
        editSteps ([&g] (StepPattern& p) { grooves::apply (g, p); });
        setParamValue (paramIndex ("swing"), g.swing, false);
        setParamValue (paramIndex ("rate"), (float) g.rateIndex, false);
        setParamValue (paramIndex ("mode"), 1.0f, false);
        msg ("Groove: " + g.name);
    }

    // ---- dice -----------------------------------------------------------------------------------
    namespace
    {
        const std::vector<std::string>& diceParams()
        {
            // rate and octave stay put: a roll to a 1-bar step or a sub-bass register sounds broken, not random
            static const std::vector<std::string> ids { "type", "density", "steps", "offset", "direction", "accent", "gate",
                                                        "slicemode", "winstart", "winsize", "swing", "jitter" };
            return ids;
        }
    }

    void Instrument::pushUndo()
    {
        UndoSnap snap;
        for (auto& id : diceParams()) { const int i = paramIndex (id); snap.params.push_back ({ i, values[(size_t) i].load() }); }
        { std::lock_guard<SpinMutex> g (eng.pattern().mutex()); snap.seed = eng.pattern().getSeed(); }
        { std::lock_guard<SpinMutex> g (eng.stepMutex()); snap.steps = eng.stepPattern().serialise(); }
        undo.push_back (std::move (snap));
        if (undo.size() > 32) undo.erase (undo.begin());
    }

    void Instrument::rollDice()
    {
        pushUndo();
        if (param ("mode") > 0.5f)
        {
            // step mode: re-roll every lane in use; an empty pattern gets a starter groove
            const bool drums = engineType() == EngineType::Drums;
            const auto roll = nextRandom();
            editSteps ([&] (StepPattern& p)
            {
                const bool empty = p.activeLanes() == 0;
                for (int l = 0; l < stepLanes; ++l)
                {
                    auto& lane = p.lanes[(size_t) l];
                    bool used = false;
                    for (int i = 0; i < lane.length; ++i) used = used || lane.steps[(size_t) i].on;
                    if (empty) used = drums ? l < 5 : l == 0 || l == 2 || l == 4;
                    if (! used) continue;
                    if (empty && drums) lane.algo = l == 3 ? LaneAlgo::Rolls : LaneAlgo::Groove;
                    if (empty && ! drums) lane.algo = LaneAlgo::Random;
                    stepgen::rollLane (lane, laneRole (l), roll + (uint64_t) l * 7919u);
                }
            });
            msg ("Rolled the step pattern");
            return;
        }
        auto unitRand = [this] { return (float) (nextRandom() >> 11) * (1.0f / 9007199254740992.0f); };
        for (auto& id : diceParams())
        {
            const int i = paramIndex (id);
            const auto& s = paramSpecs()[(size_t) i];
            float next = s.min + (s.max - s.min) * unitRand();
            if (id == "steps")     next = (float) (std::vector<int> { 8, 12, 16, 16, 16, 24, 32, 32, 48, 64 })[(size_t) (unitRand() * 9.99f)];
            if (id == "gate")      next = 0.15f + 0.9f * unitRand();
            if (id == "swing")     next = unitRand() < 0.5f ? 0.0f : 0.35f * unitRand();
            if (id == "jitter")    next = unitRand() < 0.6f ? 0.0f : 0.3f * unitRand();
            if (id == "winsize")   next = (float) (1 + (int) (unitRand() * 15.99f));
            if (id == "winstart")  next = (float) (1 + (int) (unitRand() * std::max (1.0f, (float) eng.sliceCount() - 1.0f)));
            if (id == "offset")    next = unitRand() < 0.5f ? 0.0f : (float) (int) (unitRand() * 16.0f);
            setParamValue (i, next, false);
        }
        {
            std::lock_guard<SpinMutex> g (eng.pattern().mutex());
            eng.pattern().setSeed (nextRandom());   // locks are kept
        }
        changed();
        msg ("Rolled the dice (undo brings it back)");
    }

    void Instrument::undoDice()
    {
        if (undo.empty()) { msg ("Nothing to undo"); return; }
        const auto snap = undo.back();
        undo.pop_back();
        for (const auto& [i, v] : snap.params) setParamValue (i, v, false);
        StepPattern sp;
        if (sp.deserialise (snap.steps)) { std::lock_guard<SpinMutex> g (eng.stepMutex()); eng.stepPattern() = sp; }
        { std::lock_guard<SpinMutex> g (eng.pattern().mutex()); eng.pattern().setSeed (snap.seed); }
        changed();
        msg ("Undone");
    }

    // ---- the message switchboard --------------------------------------------------------------------
    void Instrument::command (const std::string& sel, const Atoms& a)
    {
        std::lock_guard<std::recursive_mutex> g (lock);
        auto num = [&] (size_t k, double fb = 0.0) { return k < a.size() ? atomNum (a[k], fb) : fb; };
        auto str = [&] (size_t k) { return k < a.size() ? atomStr (a[k]) : std::string(); };
        auto restOfLine = [&] (size_t from)
        {
            // a path with spaces arrives as several words
            std::string s;
            for (size_t k = from; k < a.size(); ++k) { if (k > from) s += ' '; s += atomStr (a[k]); }
            return s;
        };

        if (sel == "param")
        {
            setParamValue (paramIndex (str (0)), (float) num (1), true);
            return;
        }
        if (sel == "note")      { eng.queueNote ((int) num (0), (int) num (1)); if (num (1) > 0) eng.midiExpression (ModSource::Velocity, (float) num (1) / 127.0f); return; }
        if (sel == "allnotesoff") { eng.queueAllNotesOff(); return; }
        if (sel == "bend")      { eng.midiExpression (ModSource::PitchBend, (float) std::clamp (num (0), -1.0, 1.0)); return; }
        if (sel == "touch")     { eng.midiExpression (ModSource::Aftertouch, (float) std::clamp (num (0), 0.0, 1.0)); return; }
        if (sel == "modwheel")  { eng.midiExpression (ModSource::ModWheel, (float) std::clamp (num (0), 0.0, 1.0)); return; }
        if (sel == "key")       { keyFromLive ((int) num (0), (int) num (1)); return; }

        if (sel == "load")
        {
            const auto path = restOfLine (0);
            const auto err = loadSlicerSample (path, true, 0.0);
            if (! err.empty()) msg (err);
            else
            {
                msg ("Loaded " + path.substr (path.find_last_of ("/\\:") + 1) + " (" + std::to_string ((int) std::lround (srcBpm)) + " bpm)");
                if (engineType() != EngineType::Slicer) setParamValue (paramIndex ("engine"), 1.0f, false);
            }
            return;
        }
        if (sel == "loop")
        {
            const int i = (int) num (0);
            const auto& loops = library::loops();
            if (i < 0 || i >= (int) loops.size()) return;
            const auto err = loadSlicerSample ({}, true, loops[(size_t) i].bpm, true, i);
            if (! err.empty()) msg (err);
            else
            {
                msg ("Loop: " + loops[(size_t) i].name);
                if (engineType() != EngineType::Slicer) setParamValue (paramIndex ("engine"), 1.0f, false);
            }
            return;
        }
        if (sel == "unload") { clearSlicer(); return; }
        if (sel == "slices")
        {
            if (playing == nullptr) { msg ("Load a sample first"); return; }
            const auto how = str (0);
            if (how == "transients") setSlices (slicing::transients (*playing, (float) std::clamp (num (1, 0.5), 0.0, 1.0)));
            else if (how == "grid")  setSlices (slicing::grid (playing->numFrames(), std::clamp ((int) num (1, 16), 1, maxSlices)));
            else if (how == "random") setSlices (slicing::random (playing->numFrames(), std::clamp ((int) num (1, 16), 2, maxSlices), nextRandom(), playing->sampleRate));
            else if (how == "clear") setSlices ({ 0 });
            msg (std::to_string (eng.sliceCount()) + " slices");
            changed();
            return;
        }
        if (sel == "slice")
        {
            if (playing == nullptr) return;
            const auto how = str (0);
            auto s = eng.slices();
            const uint32_t frames = playing->numFrames();
            if (how == "move" && a.size() >= 3)
            {
                const int i = (int) num (1);
                if (i <= 0 || i >= (int) s.size()) return;   // slice 1 always starts at 0
                const uint32_t lo = s[(size_t) i - 1] + 32;
                const uint32_t hi = (size_t) i + 1 < s.size() ? s[(size_t) i + 1] - 32 : frames - 32;
                if (hi <= lo) return;
                s[(size_t) i] = std::clamp ((uint32_t) (std::clamp (num (2), 0.0, 1.0) * frames), lo, hi);
                eng.setSlices (s);   // already in order: no normalise (keeps the dragged index)
                viewDirty = true;
            }
            else if (how == "add")
            {
                if (s.size() >= (size_t) maxSlices) { msg ("256 slices is the maximum"); return; }
                s.push_back ((uint32_t) (std::clamp (num (1), 0.0, 1.0) * frames));
                setSlices (s);
            }
            else if (how == "delete")
            {
                const int i = (int) num (1);
                if (i <= 0 || i >= (int) s.size()) return;
                s.erase (s.begin() + i);
                setSlices (s);
            }
            else if (how == "play")
            {
                eng.auditionSlice ((int) num (1));
                return;
            }
            changed();
            return;
        }
        if (sel == "srcbpm")
        {
            if (original == nullptr) return;
            const auto how = str (0);
            if (how == "half") srcBpm *= 0.5;
            else if (how == "double") srcBpm *= 2.0;
            else srcBpm = num (0, srcBpm);
            srcBpm = std::clamp (srcBpm, 40.0, 250.0);
            updateWarp (false);
            emitWave();
            changed();
            return;
        }

        if (sel == "pad")
        {
            const auto how = str (0);
            const int i = std::clamp ((int) num (1), 0, numCells - 1);
            if (how == "select") { selectedPad = i; emitPad (i); viewDirty = true; return; }
            if (how == "play")   { selectedPad = i; eng.audition (i); emitPad (i); return; }
            if (how == "load")   { const auto err = loadCellFile (i, restOfLine (2)); if (! err.empty()) msg (err); selectedPad = i; emitPads(); emitPad (i); return; }
            if (how == "sound")
            {
                const int s = (int) num (2);
                const auto& shots = library::oneShots();
                if (s < 0 || s >= (int) shots.size()) return;
                auto data = shots[(size_t) s].render();
                if (data == nullptr) return;
                data->path = "lib:" + library::oneShotPath (shots[(size_t) s]);
                setCellSample (i, data, shots[(size_t) s].name, data->path, s);
                selectedPad = i;
                emitPads(); emitPad (i);
                eng.audition (i);
                return;
            }
            if (how == "fm")     { setCellFm (i, (int) num (2)); selectedPad = i; emitPads(); emitPad (i); eng.audition (i); return; }
            if (how == "clear")  { clearCell (i); emitPads(); emitPad (i); return; }
            if (how == "set")    { setCellField (i, str (2), (float) num (3)); return; }
            return;
        }
        if (sel == "kit")
        {
            const auto how = str (0);
            const int k = (int) num (1);
            if (how == "lib")        loadLibraryKit (k, false);
            else if (how == "libgroove") loadLibraryKit (k, true);
            else if (how == "fm")    loadFmKit (k);
            if (engineType() != EngineType::Drums) setParamValue (paramIndex ("engine"), 2.0f, false);
            emitPads(); emitPad (selectedPad);
            msg ("Kit: " + kitName);
            viewDirty = true;
            return;
        }

        if (sel == "step")   { stepCommand (a); return; }
        if (sel == "lane")   { laneCommand (a); return; }
        if (sel == "groove") { applyGroove ((int) num (0)); return; }
        if (sel == "steps" && str (0) == "clear") { pushUndo(); editSteps ([] (StepPattern& p) { p.clear(); }); return; }

        if (sel == "regen")
        {
            pushUndo();
            std::lock_guard<SpinMutex> g2 (eng.pattern().mutex());
            eng.pattern().regenerate();
            changed();
            return;
        }
        if (sel == "lock")
        {
            const auto i = (uint32_t) std::max (0.0, num (0));
            bool locked;
            { std::lock_guard<SpinMutex> g2 (eng.pattern().mutex()); locked = eng.pattern().lockMask (i) != 0; if (locked) eng.pattern().unlock (i); }
            if (! locked) eng.lockStep (uiSeqParams(), scaleNow(), i);
            changed();
            return;
        }
        if (sel == "mute")
        {
            const auto i = (uint32_t) std::max (0.0, num (0));
            const auto p = uiSeqParams();
            const auto sc = scaleNow();
            std::lock_guard<SpinMutex> g2 (eng.pattern().mutex());
            StepData s = eng.pattern().step (i, p, sc);
            s.active = ! s.active;
            eng.pattern().lock (i, lockbit::active, s);
            changed();
            return;
        }
        if (sel == "unlockall") { std::lock_guard<SpinMutex> g2 (eng.pattern().mutex()); eng.pattern().clearLocks(); changed(); return; }
        if (sel == "dice")      { rollDice(); return; }
        if (sel == "undo")      { undoDice(); return; }

        if (sel == "restore")
        {
            // restore <chunk> <ints...>; chunk 0 starts with [total ints, chunks]
            const int k = (int) num (0);
            if (k < 0 || k >= stateChunks) return;
            if (restoreChunks.size() != (size_t) stateChunks) restoreChunks.assign ((size_t) stateChunks, {});
            std::vector<int> ints;
            for (size_t j = 1; j < a.size(); ++j) ints.push_back ((int) atomNum (a[j]));
            restoreChunks[(size_t) k] = ints;
            if (restoreChunks[0].size() < 2) return;
            const int total = restoreChunks[0][0], chunks = restoreChunks[0][1];
            std::vector<int> all;
            for (int c = 0; c < chunks && c < stateChunks; ++c)
            {
                const auto& ch = restoreChunks[(size_t) c];
                all.insert (all.end(), ch.begin() + (c == 0 ? 2 : 0), ch.end());
            }
            if ((int) all.size() < total) return;   // more chunks to come
            all.resize ((size_t) total);
            const auto text = unpackState (all);
            if (! text.empty() && text != lastSaved)
            {
                restoring = true;
                loadState (text);
                restoring = false;
                lastSaved = text;
                emitAll();
            }
            restoreChunks.assign ((size_t) stateChunks, {});
            return;
        }
        if (sel == "refresh") { emitAll(); return; }
    }

    // ---- state --------------------------------------------------------------------------------------
    std::string Instrument::saveState()
    {
        std::ostringstream o;
        o << "BASHDEV1\n";
        { std::lock_guard<SpinMutex> g (eng.pattern().mutex()); o << "@pattern\n" << eng.pattern().serialise() << "\n"; }
        { std::lock_guard<SpinMutex> g (eng.stepMutex()); o << "@steps\n" << eng.stepPattern().serialise(); }
        if (original != nullptr)
        {
            // slices are saved on the file's own timeline (unwarped), so they survive any tempo
            const double ratio = std::max (1.0e-6, appliedRatio);
            o << "@sample\n" << original->path << "\n" << srcBpm << " " << libraryLoop << "\n";
            for (auto f : eng.slices()) o << (long long) std::llround (f / ratio) << ' ';
            o << "\n";
        }
        else if (! missingSample.empty())
            o << "@missing\n" << missingSample << "\n";
        o << "@kit\n" << kitName << "\n";
        for (int i = 0; i < numCells; ++i)
        {
            const auto c = eng.cell (i);
            if (c.kind == DrumCell::Kind::Empty) continue;
            const auto& info = cells[(size_t) i];
            o << i << '\t' << (int) c.kind << '\t' << info.libSound << '\t' << info.fmPreset << '\t' << info.name << '\t' << info.path << '\t';
            const float v[] = { c.start, c.end, c.fadeIn, c.fadeOut, c.attack, c.sustain, c.tune, c.cutoff,
                                c.reverse ? 1.0f : 0.0f, c.gain, c.pan, (float) c.choke,
                                c.fm.pitch, c.fm.pitchAmt, c.fm.pitchTime, c.fm.ratio, c.fm.fmAmt, c.fm.fmDecay,
                                c.fm.noise, c.fm.tone, c.fm.noiseDecay, (float) c.fm.bursts, c.fm.decay, c.fm.click, c.fm.drive };
            for (float f : v) o << f << ' ';
            o << "\n";
        }
        o << "@end\n";
        return o.str();
    }

    bool Instrument::loadState (const std::string& text)
    {
        std::istringstream in (text);
        std::string line;
        if (! std::getline (in, line) || line != "BASHDEV1") return false;
        std::string section;
        std::string patText, stepText, samplePath, sliceLine;
        double bpm = 0.0; int loop = -1;
        std::vector<std::string> kitLines;
        std::string kit;
        bool haveSample = false;
        int sampleLine = 0;
        while (std::getline (in, line))
        {
            if (! line.empty() && line[0] == '@') { section = line.substr (1); if (section == "kit") { std::getline (in, kit); } continue; }
            if (section == "pattern") patText += line;
            else if (section == "steps") stepText += line + "\n";
            else if (section == "sample")
            {
                if (sampleLine == 0) { samplePath = line; haveSample = true; }
                else if (sampleLine == 1) { std::istringstream l (line); l >> bpm >> loop; }
                else if (sampleLine == 2) sliceLine = line;
                ++sampleLine;
            }
            else if (section == "missing") missingSample = line;
            else if (section == "kit") kitLines.push_back (line);
        }

        { std::lock_guard<SpinMutex> g (eng.pattern().mutex()); eng.pattern().deserialise (patText); }
        { StepPattern sp; if (sp.deserialise (stepText)) { std::lock_guard<SpinMutex> g (eng.stepMutex()); eng.stepPattern() = sp; } }

        // the slicer sample
        if (haveSample)
        {
            std::string err;
            if (loop >= 0) err = loadSlicerSample ({}, false, std::max (0.0, bpm), true, loop);
            else err = loadSlicerSample (samplePath, false, std::max (0.0, bpm));
            if (err.empty())
            {
                std::vector<uint32_t> sl;
                std::istringstream l (sliceLine);
                long long f;
                while (l >> f) sl.push_back ((uint32_t) std::llround ((double) f * appliedRatio));
                setSlices (std::move (sl));
            }
            else
            {
                missingSample = samplePath;
                msg ("Missing sample: " + samplePath);
            }
        }

        // the kit
        for (int i = 0; i < numCells; ++i) clearCell (i);
        kitName = kit;
        for (const auto& kl : kitLines)
        {
            std::vector<std::string> f;
            std::string part;
            std::istringstream ls (kl);
            while (std::getline (ls, part, '\t')) f.push_back (part);
            if (f.size() < 7) continue;
            const int i = std::atoi (f[0].c_str());
            const int kind = std::atoi (f[1].c_str());
            const int libSound = std::atoi (f[2].c_str());
            const int fmPreset = std::atoi (f[3].c_str());
            if (i < 0 || i >= numCells) continue;
            std::vector<float> v;
            std::istringstream vs (f[6]);
            float x;
            while (vs >> x) v.push_back (x);
            if (v.size() < 25) continue;
            if (kind == (int) DrumCell::Kind::Sample)
            {
                std::shared_ptr<SampleData> data;
                if (libSound >= 0 && libSound < (int) library::oneShots().size())
                {
                    data = library::oneShots()[(size_t) libSound].render();
                    if (data) data->path = f[5];
                }
                else
                {
                    std::string err;
                    data = decodeAudioFile (f[5], 60.0, err);
                }
                if (data == nullptr) { cells[(size_t) i].name = "Missing: " + f[4]; continue; }
                setCellSample (i, data, f[4], f[5], libSound);
            }
            else if (kind == (int) DrumCell::Kind::Fm)
            {
                if (fmPreset >= 0) setCellFm (i, fmPreset);
                cells[(size_t) i].name = f[4];
            }
            auto c = eng.cell (i);
            c.start = v[0]; c.end = v[1]; c.fadeIn = v[2]; c.fadeOut = v[3]; c.attack = v[4]; c.sustain = v[5];
            c.tune = v[6]; c.cutoff = v[7]; c.reverse = v[8] > 0.5f; c.gain = v[9]; c.pan = v[10]; c.choke = (int) v[11];
            c.fm.pitch = v[12]; c.fm.pitchAmt = v[13]; c.fm.pitchTime = v[14]; c.fm.ratio = v[15]; c.fm.fmAmt = v[16];
            c.fm.fmDecay = v[17]; c.fm.noise = v[18]; c.fm.tone = v[19]; c.fm.noiseDecay = v[20]; c.fm.bursts = (int) v[21];
            c.fm.decay = v[22]; c.fm.click = v[23]; c.fm.drive = v[24];
            if (kind == (int) DrumCell::Kind::Fm) c.kind = DrumCell::Kind::Fm;
            eng.setCell (i, c);
        }
        viewDirty = true;
        return true;
    }

    std::vector<int> Instrument::packState (const std::string& s)
    {
        std::vector<int> out;
        out.reserve (s.size() / 3 + 1);
        for (size_t i = 0; i < s.size(); i += 3)
        {
            int v = 0;
            for (size_t k = 0; k < 3; ++k)
                v |= (i + k < s.size() ? (int) (unsigned char) s[i + k] : 0) << (8 * k);
            out.push_back (v);
        }
        return out;
    }

    std::string Instrument::unpackState (const std::vector<int>& ints)
    {
        std::string s;
        s.reserve (ints.size() * 3);
        for (int v : ints)
            for (int k = 0; k < 3; ++k)
            {
                const char c = (char) ((v >> (8 * k)) & 0xff);
                if (c != 0) s.push_back (c);
            }
        return s;
    }

    void Instrument::emitStateChunks()
    {
        const auto text = saveState();
        if (text == lastSaved) return;
        lastSaved = text;
        auto ints = packState (text);
        const int capacity = stateChunkSize * stateChunks - 2;
        if ((int) ints.size() > capacity) { msg ("Too much to save: simplify the pattern"); return; }
        const int total = (int) ints.size();
        std::vector<int> all { total, 0 };
        all.insert (all.end(), ints.begin(), ints.end());
        const int chunks = std::max (1, (int) ((all.size() + stateChunkSize - 1) / stateChunkSize));
        all[1] = chunks;
        for (int c = 0; c < stateChunks; ++c)
        {
            Atoms args { (double) c };
            if (c < chunks)
                for (size_t j = (size_t) c * stateChunkSize; j < std::min (all.size(), (size_t) (c + 1) * stateChunkSize); ++j)
                    args.push_back ((double) all[j]);
            else
                args.push_back (0.0);   // unused chunks are cleared
            emit ("state", args);
        }
    }

    // ---- the displays ---------------------------------------------------------------------------------
    void Instrument::emitMenus()
    {
        Atoms loops { std::string ("loops") };
        for (auto& l : library::loops()) loops.push_back (l.category + ": " + l.name);
        emit ("menu", loops);
        Atoms kits { std::string ("kits") };
        for (auto& k : library::kits()) kits.push_back (k.style + ": " + k.name);
        for (auto& k : factoryKits()) kits.push_back (std::string ("FM: ") + k.name);
        emit ("menu", kits);
        Atoms sounds { std::string ("sounds") };
        for (auto& s : library::oneShots()) sounds.push_back (s.category + ": " + s.name);
        for (auto& f : fmPresets()) sounds.push_back (std::string ("FM: ") + f.name);
        emit ("menu", sounds);
        Atoms gr { std::string ("grooves") };
        for (auto& g : grooves::all()) gr.push_back (g.style + ": " + g.name);
        emit ("menu", gr);
        Atoms algos { std::string ("lanealgos") };
        for (int i = 0; i < (int) LaneAlgo::count; ++i) algos.push_back (std::string (laneAlgoName ((LaneAlgo) i)));
        emit ("menu", algos);
        Atoms speeds { std::string ("lanespeeds") };
        for (auto& s : laneSpeeds()) speeds.push_back (std::string (s.first));
        emit ("menu", speeds);
    }

    void Instrument::emitSeqView()
    {
        // seq <length> <engine> then per step: active note accent slide locked
        const auto p = uiSeqParams();
        const uint32_t len = std::min<uint32_t> (p.length, 64u);
        const auto steps = eng.snapshot (p, scaleNow(), 0, len);
        Atoms a { (double) len, (double) (int) engineType() };
        std::lock_guard<SpinMutex> g (eng.pattern().mutex());
        for (uint32_t i = 0; i < len; ++i)
        {
            const auto& st = steps[(size_t) i];
            const int value = engineType() == EngineType::Synth ? st.note : st.slice;
            a.push_back (st.active ? 1.0 : 0.0);
            a.push_back ((double) value);
            a.push_back (st.accent ? 1.0 : 0.0);
            a.push_back (st.slide ? 1.0 : 0.0);
            a.push_back (eng.pattern().lockMask (i) != 0 ? 1.0 : 0.0);
        }
        emit ("seq", a);
    }

    void Instrument::emitStepView()
    {
        // lane <l> <name> <length> <speed> <mute> <algo> <density> <pulses> <rotate> <evolve> <64 packed steps>
        // packed step: on | vel << 1 | (ratchet - 1) << 8 | probIndex << 11 | cond << 13
        StepPattern p;
        { std::lock_guard<SpinMutex> g (eng.stepMutex()); p = eng.stepPattern(); }
        for (int l = 0; l < stepLanes; ++l)
        {
            const auto& lane = p.lanes[(size_t) l];
            Atoms a { (double) l, laneName (l), (double) lane.length, (double) lane.speed, (double) (lane.mute ? 1 : 0),
                      (double) (int) lane.algo, (double) lane.density, (double) lane.pulses, (double) lane.rotate, (double) lane.evolve };
            for (int i = 0; i < maxSteps; ++i)
            {
                const auto& s = lane.steps[(size_t) i];
                const int probIndex = s.prob >= 100 ? 0 : s.prob >= 75 ? 1 : s.prob >= 50 ? 2 : 3;
                const int packed = (s.on ? 1 : 0) | ((int) s.vel << 1) | ((std::clamp ((int) s.ratchet, 1, 4) - 1) << 8)
                                 | (probIndex << 11) | ((int) s.cond << 13);
                a.push_back ((double) packed);
            }
            emit ("lane", a);
        }
        emit ("lanesel", { (double) selectedLane });
    }

    void Instrument::emitWave()
    {
        // wave <frames> <srcBpm> <ratio> <name> <bins...>   (0 frames = no sample)
        constexpr int bins = 300;
        if (playing == nullptr)
        {
            emit ("wave", { 0.0, 0.0, 1.0, missingSample.empty() ? std::string ("drop a sample") : std::string ("missing: ") + missingSample });
            return;
        }
        std::string name = original != nullptr ? original->path : playing->path;
        if (const auto slash = name.find_last_of ("/\\:"); slash != std::string::npos) name = name.substr (slash + 1);
        Atoms a { (double) playing->numFrames(), srcBpm, appliedRatio, name };
        const uint32_t n = playing->numFrames();
        for (int b = 0; b < bins; ++b)
        {
            const uint32_t from = (uint32_t) ((uint64_t) n * (uint64_t) b / bins), to = std::max (from + 1, (uint32_t) ((uint64_t) n * (uint64_t) (b + 1) / bins));
            float pk = 0.0f;
            const uint32_t stride = std::max (1u, (to - from) / 64u);
            for (uint32_t i = from; i < to && i < n; i += stride) pk = std::max (pk, std::abs (playing->mono (i)));
            a.push_back ((double) std::min (1.0f, pk));
        }
        emit ("wave", a);
        viewDirty = true;   // slices follow
    }

    void Instrument::emitPads()
    {
        Atoms names;
        for (int i = 0; i < numCells; ++i) names.push_back (cells[(size_t) i].name.empty() ? std::string ("-") : cells[(size_t) i].name);
        emit ("padnames", names);
        Atoms kinds;
        for (int i = 0; i < numCells; ++i) kinds.push_back ((double) (int) eng.cell (i).kind);
        emit ("padkinds", kinds);
        emit ("padsel", { (double) selectedPad });
        emit ("kitname", { kitName.empty() ? std::string ("Kit") : kitName });
    }

    void Instrument::emitFrame()
    {
        // frame <running> <beat> <bpm> <playIndex> <slice> <slicePos 0..1> <peak> <lfo1> <lfo2> <life> <notes> <lastNote>
        const auto* s = eng.sample();
        const double fp = eng.playingFrame();
        emit ("frame", { eng.isRunning() ? 1.0 : 0.0, eng.currentBeat(), eng.currentBpm(), (double) eng.playIndex(),
                         (double) eng.playingSlice(), s != nullptr && fp >= 0.0 && s->numFrames() > 0 ? fp / s->numFrames() : -1.0,
                         (double) eng.outputPeak(), (double) eng.sourceValue (ModSource::Lfo1), (double) eng.sourceValue (ModSource::Lfo2),
                         (double) eng.sourceValue (ModSource::Life), (double) eng.noteCount(), (double) eng.lastNote() });
        Atoms hits, lanes;
        for (int i = 0; i < numCells; ++i) hits.push_back ((double) eng.hitCount (i));
        for (int l = 0; l < stepLanes; ++l) lanes.push_back ((double) eng.stepLanePosition (l));
        emit ("hits", hits);
        emit ("lanepos", lanes);
    }

    void Instrument::emitAll()
    {
        emitMenus();
        emitPads();
        emitPad (selectedPad);
        emitWave();
        viewDirty = true;
        const auto sc = scaleNow();
        emit ("keyname", { std::string (noteName ((int) param ("root"))) + " " + allScales()[(size_t) std::clamp ((int) param ("scale"), 0, (int) allScales().size() - 1)].name });
        (void) sc;
    }

    void Instrument::tick()
    {
        std::lock_guard<std::recursive_mutex> g (lock);
        ++tickCount;

        // warp: follow tempo changes once the tempo has settled, and pick up finished stretches
        const double target = param ("sync") > 0.5f ? eng.currentBpm() : param ("tempo");
        if (std::abs (target - lastTargetBpm) > 1.0e-3) { lastTargetBpm = target; targetSettleTicks = 0; }
        else if (++targetSettleTicks == 8) updateWarp (false);
        {
            std::shared_ptr<SampleData> ready;
            double ratio = 1.0;
            {
                std::lock_guard<std::mutex> w (warpMutex);
                ready.swap (warpReady);
                ratio = warpReadyRatio;
            }
            if (ready != nullptr)
            {
                if (warpThread.joinable() && ! warpThreadBusy.load()) warpThread.join();
                if (std::abs (ratio - requestedRatio) < 1.0e-4) { installSlicer (ready, ratio); emitWave(); changed(); }
                else updateWarp (false);
            }
            else if (! warpThreadBusy.load() && warpThread.joinable())
            {
                warpThread.join();
                updateWarp (false);
            }
        }

        // free replaced samples once the audio thread has moved past them
        const auto now = eng.blocksProcessed();
        graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                         [now, this] (const auto& gr) { return now >= gr.second + 2 || ! prepared; }),
                         graveyard.end());

        // the displays
        const int keyNow = (int) param ("root") * 1000 + (int) param ("scale");
        if (keyNow != keyShown)
        {
            keyShown = keyNow;
            emit ("keyname", { std::string (noteName ((int) param ("root"))) + " " + allScales()[(size_t) std::clamp ((int) param ("scale"), 0, (int) allScales().size() - 1)].name });
        }
        if (viewDirty)
        {
            viewDirty = false;
            emitSeqView();
            emitStepView();
            Atoms sl;
            if (playing != nullptr && playing->numFrames() > 0)
                for (auto f : eng.slices()) sl.push_back ((double) f / playing->numFrames());
            emit ("slices", sl);
        }
        emitFrame();

        // save into the Live set half a second after the last edit
        if (dirty && tickCount - dirtyAt > 12)
        {
            dirty = false;
            emitStateChunks();
        }
    }
}
