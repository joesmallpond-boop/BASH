#include "bash/Engine.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace bash
{
    const std::vector<Rate>& allRates()
    {
        static const std::vector<Rate> rates {
            { "1/8", 2.0 }, { "1/16", 4.0 }, { "1/32", 8.0 }, { "1/8T", 3.0 }, { "1/16T", 6.0 }
        };
        return rates;
    }

    const char* engineTypeName (EngineType e)
    {
        switch (e)
        {
            case EngineType::Synth:  return "Synth";
            case EngineType::Slicer: return "Slicer";
            case EngineType::Drums:  return "Drums";
            default:                 return "?";
        }
    }

    Engine::Engine()
    {
        for (int t = 0; t < numTracks; ++t)
        {
            patterns[(size_t) t].setSeed (mix64 (0xBA54ull + (uint64_t) t));
            playIdx[(size_t) t].store (0);
            sliceNow[(size_t) t].store (-1);
            framePos[(size_t) t].store (-1.0);
            samples[(size_t) t].store (nullptr);
            slicerNote[(size_t) t] = -1;
            sliceStores[(size_t) t].points.reserve (maxSlices + 1);
        }
        tracks[0].enabled = true;   // track 1 plays out of the box
        refreshScale();
    }

    void Engine::prepare (double sampleRate, int maxBlockSize)
    {
        sr = sampleRate;
        maxBlock = std::max (32, std::min (maxBlockSize, 2048));
        bufL.assign ((size_t) maxBlock, 0.0f);
        bufR.assign ((size_t) maxBlock, 0.0f);
        events.reserve (256);
        for (auto& v : synths)  v.prepare (sr);
        for (auto& v : slicers) v.prepare (sr);
        for (auto& p : players) p.reset();
    }

    void Engine::midiNoteIn (int note, int velocity)
    {
        // keep held notes sorted ascending, no duplicates, max 16
        int pos = 0;
        while (pos < heldInCount && heldIn[(size_t) pos] < note) ++pos;
        const bool present = pos < heldInCount && heldIn[(size_t) pos] == note;
        if (velocity > 0 && ! present && heldInCount < (int) heldIn.size())
        {
            for (int i = heldInCount; i > pos; --i) heldIn[(size_t) i] = heldIn[(size_t) i - 1];
            heldIn[(size_t) pos] = note;
            ++heldInCount;
        }
        else if (velocity == 0 && present)
        {
            for (int i = pos; i < heldInCount - 1; ++i) heldIn[(size_t) i] = heldIn[(size_t) i + 1];
            --heldInCount;
        }
    }

    void Engine::refreshScale()
    {
        if (globals.root != scaleRoot || globals.scaleIndex != scaleIdx)
        {
            currentScale = Scale (globals.root, globals.scaleIndex);
            scaleRoot = globals.root;
            scaleIdx  = globals.scaleIndex;
        }
    }

    // ---- UI-thread helpers -------------------------------------------------
    std::vector<StepData> Engine::snapshot (int t, const SequencerParams& params, const Scale& sc,
                                            uint32_t first, uint32_t count)
    {
        std::vector<StepData> out;
        out.reserve (count);
        auto& pat = patterns[(size_t) t];
        std::lock_guard<SpinMutex> g (pat.mutex());
        for (uint32_t i = 0; i < count; ++i)
            out.push_back (pat.step (first + i, params, sc));
        return out;
    }

    void Engine::lockStep (int t, const SequencerParams& params, const Scale& sc, uint32_t index, uint8_t mask)
    {
        auto& pat = patterns[(size_t) t];
        std::lock_guard<SpinMutex> g (pat.mutex());
        const StepData current = pat.step (index, params, sc);
        pat.lock (index, mask, current);
    }

    void Engine::setSlices (int t, std::vector<uint32_t> s)
    {
        auto& store = sliceStores[(size_t) t];
        if (s.size() > (size_t) maxSlices) s.resize ((size_t) maxSlices);
        std::lock_guard<SpinMutex> g (store.m);
        store.points.assign (s.begin(), s.end());   // capacity reserved: no allocation
    }

    std::vector<uint32_t> Engine::slices (int t)
    {
        auto& store = sliceStores[(size_t) t];
        std::lock_guard<SpinMutex> g (store.m);
        return store.points;
    }

    uint32_t Engine::sliceCount (int t)
    {
        auto& store = sliceStores[(size_t) t];
        std::lock_guard<SpinMutex> g (store.m);
        return (uint32_t) store.points.size();
    }

    // ---- audio -------------------------------------------------------------
    void Engine::process (float* left, float* right, int numSamples,
                          const Transport& tp, std::vector<MidiOut>& midiOut)
    {
        refreshScale();

        // Drop voices still reading a sample that has just been replaced.
        for (int t = 0; t < numTracks; ++t)
        {
            const SampleData* now = samples[(size_t) t].load();
            if (now != lastSample[(size_t) t])
            {
                slicers[(size_t) t].reset();
                slicerNote[(size_t) t] = -1;
                lastSample[(size_t) t] = now;
            }
        }

        const bool useHost = globals.hostSync && tp.hostAvailable;
        const bool run     = useHost ? tp.hostPlaying : globals.playing;
        const double bpm   = std::max (1.0, useHost ? tp.hostBpm : globals.tempo);
        double ppq         = useHost ? tp.hostPpq : internalPpq;

        if (run && ! wasRunning && ! useHost)
            ppq = internalPpq = 0.0;            // internal clock restarts from bar 1
        wasRunning = run;
        running.store (run);

        const double ppqPerSample = bpm / 60.0 / sr;
        int done = 0;
        while (done < numSamples)
        {
            const int n = std::min (maxBlock, numSamples - done);
            renderChunk (left + done, right + done, n, done, run, ppq, bpm, midiOut);
            ppq += ppqPerSample * n;
            done += n;
        }

        if (! useHost && run)
            internalPpq = ppq;

        blockCount.fetch_add (1);
    }

    void Engine::handleEvent (int t, const NoteEvent& e)
    {
        auto& ts = tracks[(size_t) t];
        switch (ts.engine)
        {
            case EngineType::Slicer:
            {
                auto& v = slicers[(size_t) t];
                if (! e.noteOn)
                {
                    if (e.note == slicerNote[(size_t) t]) { v.release(); slicerNote[(size_t) t] = -1; }
                    return;
                }
                const SampleData* s = lastSample[(size_t) t];
                if (s == nullptr) return;

                uint32_t a = 0, b = 0;
                {
                    auto& store = sliceStores[(size_t) t];
                    std::lock_guard<SpinMutex> g (store.m);
                    if (store.points.empty()) return;
                    const size_t i = (size_t) std::clamp (e.slice, 0, (int) store.points.size() - 1);
                    a = store.points[i];
                    b = i + 1 < store.points.size() ? store.points[i + 1] : s->numFrames();
                }

                const float semis = (ts.slicer.pitchFollow ? (float) (e.note - ts.seq.baseNote) : 0.0f) + ts.slicer.tune;
                const double rate = s->sampleRate / sr * std::pow (2.0, semis / 12.0);
                const float gain  = (0.45f + 0.55f * (float) e.velocity / 127.0f) * (e.accent ? 1.15f : 1.0f);
                v.start (s, a, b, rate, gain, ts.slicer.voice);
                slicerNote[(size_t) t] = e.note;
                sliceNow[(size_t) t].store (e.slice);
                return;
            }

            case EngineType::Synth:
            case EngineType::Drums:   // drums arrive in milestone 4; synth voice meanwhile
            default:
            {
                auto& v = synths[(size_t) t];
                v.setPolyphonic (ts.seq.algorithm == Algorithm::Counterpoint);
                if (e.noteOn) v.noteOn (e.note, e.velocity, e.accent, e.legato);
                else          v.noteOff (e.note);
                return;
            }
        }
    }

    void Engine::renderChunk (float* l, float* r, int n, int offsetInBlock, bool run,
                              double ppq, double bpm, std::vector<MidiOut>& midiOut)
    {
        std::fill (l, l + n, 0.0f);
        std::fill (r, r + n, 0.0f);

        for (int t = 0; t < numTracks; ++t)
        {
            auto& ts     = tracks[(size_t) t];
            auto& player = players[(size_t) t];
            events.clear();

            // the sequencer needs to know how many slices exist
            if (ts.engine == EngineType::Slicer)
            {
                auto& store = sliceStores[(size_t) t];
                std::lock_guard<SpinMutex> g (store.m);
                ts.seq.sliceCount = lastSample[(size_t) t] != nullptr ? (uint32_t) store.points.size() : 0u;
            }
            else
            {
                ts.seq.sliceCount = 0;
            }
            ts.seq.heldCount = heldInCount;
            ts.seq.held = heldIn;

            if (run && ts.enabled)
            {
                const double spq = allRates()[(size_t) std::clamp (ts.rateIndex, 0, (int) allRates().size() - 1)].stepsPerQuarter;
                auto& pat = patterns[(size_t) t];
                std::lock_guard<SpinMutex> g (pat.mutex());
                player.process (ppq * spq, bpm / 60.0 / sr * spq, n, pat, ts.seq, currentScale, events);
                playIdx[(size_t) t].store (player.currentIndex());
            }
            else
            {
                player.reset (&events, 0);
            }

            auto& synth  = synths[(size_t) t];
            auto& slicer = slicers[(size_t) t];
            const bool isSlicer = ts.engine == EngineType::Slicer;

            // engine switched: silence the one no longer used
            if (isSlicer && ! synth.isSilent())       synth.allNotesOff();
            if (! isSlicer && ! slicer.isSilent())    slicer.release();

            if (events.empty() && synth.isSilent() && slicer.isSilent())
            {
                if (isSlicer) { framePos[(size_t) t].store (-1.0); }
                continue;
            }

            std::fill (bufL.begin(), bufL.begin() + n, 0.0f);
            std::fill (bufR.begin(), bufR.begin() + n, 0.0f);

            auto renderRange = [&] (int from, int count)
            {
                if (count <= 0) return;
                if (! synth.isSilent())  synth.render (bufL.data(), from, count, ts.voice);
                if (! slicer.isSilent()) slicer.render (bufL.data() + 0, bufR.data(), from, count, ts.slicer.voice);
            };

            // synth renders mono into bufL; copy it to bufR afterwards. To keep that
            // simple, render the synth into its own pass when the track is a synth.
            int pos = 0;
            for (const auto& e : events)
            {
                if (e.sampleOffset > pos) { renderRange (pos, e.sampleOffset - pos); pos = e.sampleOffset; }
                handleEvent (t, e);
                midiOut.push_back ({ offsetInBlock + e.sampleOffset, t + 1 + (e.stream == 1 ? 8 : 0),
                                     e.noteOn, e.note, e.noteOn ? e.velocity : 0 });
            }
            renderRange (pos, n - pos);

            if (! isSlicer)
                std::copy (bufL.begin(), bufL.begin() + n, bufR.begin());   // mono synth → both sides
            else
                framePos[(size_t) t].store (slicer.position());

            const float pan = std::clamp (ts.pan, -1.0f, 1.0f);
            const float gl  = ts.level * globals.master * std::cos ((pan + 1.0f) * 0.25f * 3.14159265f) * 1.41421356f;
            const float gr  = ts.level * globals.master * std::sin ((pan + 1.0f) * 0.25f * 3.14159265f) * 1.41421356f;
            for (int i = 0; i < n; ++i)
            {
                l[i] += bufL[(size_t) i] * gl;
                r[i] += bufR[(size_t) i] * gr;
            }
        }
    }
}
