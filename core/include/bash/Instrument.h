#pragma once
// BASH! core — the instrument: everything the Max for Live device does, with no Max code.
// It owns the engine, the parameters, samples, the drum kit, step / slice editing, the
// dice, saved state and the data the device's displays draw. The Max external is a thin
// wrapper: it forwards messages to command(), calls process() from the audio thread and
// tick() from a timer, and sends whatever emit() produces back into the patch.

#include "bash/Engine.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace bash
{
    /** One message argument: a number or a word (like a Max atom). */
    using Atom  = std::variant<double, std::string>;
    using Atoms = std::vector<Atom>;
    double      atomNum (const Atom&, double fallback = 0.0);
    std::string atomStr (const Atom&);

    /** An automatable parameter (a live.* object in the device). Values are plain units. */
    struct ParamSpec
    {
        enum class Kind { Float, Int, Enum, Toggle };
        std::string id, name, shortName;
        Kind  kind = Kind::Float;
        float min = 0.0f, max = 1.0f, def = 0.0f;
        float exponent = 1.0f;          // live.dial exponent (skewed ranges)
        std::string unit;               // "Hz", "s", "st", "%", ...
        std::vector<std::string> choices;
        int   group = 0;                // 0 sequencer, 1 voice, 2 mod, 3 global
    };
    const std::vector<ParamSpec>& paramSpecs();
    int paramIndex (const std::string& id);   // -1 if unknown

    class Instrument
    {
    public:
        using Emit = std::function<void (const std::string& selector, const Atoms& args)>;

        Instrument();
        ~Instrument();

        void setEmitter (Emit e)               { emitFn = std::move (e); }

        // ---- audio thread ----
        void prepare (double sampleRate, int maxBlock);
        void process (float* left, float* right, int n, const Transport&);

        // ---- message threads (main / scheduler; serialised inside) ----
        /** Handle one message from the device (see Instrument.cpp for the list). */
        void command (const std::string& selector, const Atoms& args);
        /** ~25 times a second: animation data for the displays, finished warps,
            freeing replaced samples, saving state after edits. */
        void tick();

        // ---- direct access (tests) ----
        Engine& engine()                       { return eng; }
        float param (const std::string& id) const;
        std::string saveState();
        bool loadState (const std::string&);
        /** Ints of 24 bits each (3 bytes), so the state survives as numbers in a Live set. */
        static std::vector<int> packState (const std::string&);
        static std::string unpackState (const std::vector<int>&);
        static constexpr int stateChunkSize = 1024;   // ints per pattr chunk
        static constexpr int stateChunks = 48;
        /** LFO k (0, 1) as a value for the parameter it's mapped to (in that parameter's own
            units), updated every block. Feeds live.remote~ in the device. */
        float mapOutput (int k) const          { return mapOut[(size_t) k].load(); }
        bool  isMapped (int k);
        /** The BASH! Sequencer device: notes made since the last call (velocity 0 = off). */
        int   takeMidi (Engine::MidiNote* dest, int maxCount) { return eng.takeMidi (dest, maxCount); }
        bool warpBusy() const                  { return warpThreadBusy.load(); }
        void waitForWarp();
        double sourceBpm() const               { return srcBpm; }

    private:
        struct CellInfo { std::shared_ptr<SampleData> sample; std::string name, path; int libSound = -1; int fmPreset = -1; };

        void setParamValue (int index, float v, bool fromUi);
        void pull();                             // audio thread: params -> engine settings
        void emit (const std::string& s, const Atoms& a) { if (emitFn) emitFn (s, a); }
        void emitParam (const std::string& id, float v);
        void msg (const std::string& text)     { emit ("msg", { text }); }
        void changed()                         { viewDirty = true; if (! restoring) { dirty = true; dirtyAt = tickCount; } }

        // samples / slicer
        std::string loadSlicerSample (const std::string& path, bool autoSlice, double knownBpm, bool fromLibraryLoop = false, int loopIndex = -1);
        void installSlicer (std::shared_ptr<SampleData> playing, double ratio);
        void clearSlicer();
        void setSlices (std::vector<uint32_t>);
        double desiredWarpRatio() const;
        void updateWarp (bool synchronous);
        void retire (std::shared_ptr<SampleData>);

        // kit
        std::string loadCellFile (int i, const std::string& path);
        void setCellSample (int i, std::shared_ptr<SampleData>, const std::string& name, const std::string& path, int libSound);
        void setCellFm (int i, int preset);
        void clearCell (int i);
        void loadFmKit (int k);
        void loadLibraryKit (int k, bool withGroove);
        void setCellField (int i, const std::string& field, float v);
        void emitPad (int i);

        // sequencing
        SequencerParams uiSeqParams();
        Scale scaleNow() const;
        EngineType engineType() const;
        LaneRole laneRole (int lane) const;
        std::string laneName (int lane);
        void editSteps (const std::function<void (StepPattern&)>&);
        void stepCommand (const Atoms&);
        void laneCommand (const Atoms&);
        void applyGroove (int index);
        void rollDice();
        void undoDice();
        void pushUndo();
        void keyFromLive (int root, int mask);
        static int scaleForMask (int mask);

        // views
        void emitAll();
        void emitMenus();
        void emitSeqView();
        void emitStepView();
        void emitWave();
        void emitPads();
        void emitFrame();
        void emitStateChunks();

        Engine eng;
        Emit emitFn;
        std::recursive_mutex lock;
        std::vector<std::atomic<float>> values;
        bool prepared = false;
        double sr = 44100.0;

        // slicer sample (original + the warped copy that plays)
        std::shared_ptr<SampleData> original, playing;
        double srcBpm = 0.0, appliedRatio = 1.0, requestedRatio = 1.0;
        std::string missingSample;
        int libraryLoop = -1;
        std::thread warpThread;
        std::atomic<bool> warpThreadBusy { false };
        std::mutex warpMutex;
        std::shared_ptr<SampleData> warpReady;
        double warpReadyRatio = 1.0;
        double lastTargetBpm = 0.0;
        int targetSettleTicks = 0;

        std::vector<std::pair<std::shared_ptr<SampleData>, uint64_t>> graveyard;
        std::array<CellInfo, numCells> cells;
        std::string kitName;
        int selectedPad = 0, selectedLane = 0;

        struct UndoSnap { std::vector<std::pair<int, float>> params; uint64_t seed; std::string steps; };
        std::vector<UndoSnap> undo;
        uint64_t rngState = 0x5EEDBA55ull;
        uint64_t nextRandom();

        // state / views
        uint64_t tickCount = 0, dirtyAt = 0;
        bool dirty = false, viewDirty = true, restoring = false;
        std::vector<std::vector<int>> restoreChunks;
        std::string lastSaved;
        uint64_t lastBlocks = 0;
        int lastPlayIndex = -1;
        std::array<uint32_t, numCells> lastHits {};
        std::string lastStepViewKey;
        int lastLiveRoot = -1, lastLiveMask = -1;
        int keyShown = -1;
        // LFO -> any Live parameter (like Live's LFO: MAP, then click a parameter)
        struct LfoMap { std::string path, name, device; float lo = 0.0f, hi = 1.0f; bool waiting = false; };
        std::array<LfoMap, 2> maps;
        std::array<std::atomic<float>, 2> mapLo {}, mapHi {}, mapOut {};
        void emitMap (int k);
        void mapCommand (const std::string& sel, const Atoms&);
    };
}
