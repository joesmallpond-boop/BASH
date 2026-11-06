#include "bash/Engine.h"

#include <algorithm>
#include <memory>
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
            lifes[(size_t) t].setSeed (mix64 (0x11FE0000ull + (uint64_t) t));
            playIdx[(size_t) t].store (0);
            sliceNow[(size_t) t].store (-1);
            framePos[(size_t) t].store (-1.0);
            samples[(size_t) t].store (nullptr);
            slicerNote[(size_t) t] = -1;
            sliceStores[(size_t) t].points.reserve (maxSlices + 1);
        }
        tracks[0].enabled = true;   // track 1 plays out of the box
        globals.returns[0].type = FxType::Plate;     globals.returns[0].a = 0.6f; globals.returns[0].b = 0.4f;
        globals.returns[1].type = FxType::TapeDelay; globals.returns[1].a = 0.55f; globals.returns[1].b = 0.4f;
        for (auto& p : trackPeak) p.store (0.0f);
        refreshScale();
    }

    void Engine::prepare (double sampleRate, int maxBlockSize)
    {
        sr = sampleRate;
        maxBlock = std::max (32, std::min (maxBlockSize, 128));   // internal chunk: modulation updates every <= 128 samples
        bufL.assign ((size_t) maxBlock, 0.0f);
        bufR.assign ((size_t) maxBlock, 0.0f);
        bufMono.assign ((size_t) maxBlock, 0.0f);
        for (int k = 0; k < 2; ++k) { retL[(size_t) k].assign ((size_t) maxBlock, 0.0f); retR[(size_t) k].assign ((size_t) maxBlock, 0.0f); }
        for (auto& tf : trackFx) for (auto& f : tf) f.prepare (sr, maxBlock);
        for (auto& f : returnFx) f.prepare (sr, maxBlock);
        microFx.prepare (sr, maxBlock);
        glue.prepare (sr);
        limiter.prepare (sr);
        events.reserve (256);
        for (auto& v : synths)  v.prepare (sr);
        for (auto& v : slicers) v.prepare (sr);
        for (auto& d : drums)   d.prepare (sr);
        auditionPlayer.prepare (sr);
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

    void Engine::setCell (int index, const DrumCell& c)
    {
        if (index < 0 || index >= numCells) return;
        const SampleData* old = nullptr;
        {
            std::lock_guard<SpinMutex> g (kitMutex);
            old = kit[(size_t) index].kind == DrumCell::Kind::Sample ? kit[(size_t) index].sample : nullptr;
            kit[(size_t) index] = c;
        }
        const SampleData* now = c.kind == DrumCell::Kind::Sample ? c.sample : nullptr;
        if (old != nullptr && old != now)
            retiredCellSamples[(size_t) index].store (old);   // audio thread stops voices using it
    }

    DrumCell Engine::cell (int index)
    {
        std::lock_guard<SpinMutex> g (kitMutex);
        return kit[(size_t) std::clamp (index, 0, numCells - 1)];
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

        for (auto& r : retiredCellSamples)
            if (const SampleData* old = r.exchange (nullptr))
            {
                for (auto& d : drums) d.stopVoicesUsing (old);
                auditionPlayer.stopVoicesUsing (old);
            }

        // Sync: always the DAW's tempo. While the DAW plays, follow its position; while it is
        // stopped, BASH!'s own Play still runs (at the DAW's tempo).
        const bool hostTempo = globals.hostSync && tp.hostAvailable;
        const bool useHost   = hostTempo && tp.hostPlaying && tp.hostPpqValid;
        const bool run       = useHost || globals.playing;
        const double bpm     = std::max (1.0, hostTempo ? tp.hostBpm : globals.tempo);
        double ppq           = useHost ? tp.hostPpq : internalPpq;

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
        auto& ts = live[(size_t) t];
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

            case EngineType::Drums:
            {
                if (! e.noteOn) return;   // one-shots
                const int idx = std::clamp (e.note - drumBaseNote, 0, numCells - 1);
                DrumCell c;
                {
                    std::lock_guard<SpinMutex> g (kitMutex);
                    c = kit[(size_t) idx];
                }
                drums[(size_t) t].trigger (c, idx, e.velocity, e.accent);
                sliceNow[(size_t) t].store (idx);
                return;
            }

            case EngineType::Synth:
            default:
            {
                auto& v = synths[(size_t) t];
                v.setPolyphonic (ts.stepMode || ts.seq.algorithm == Algorithm::Counterpoint);   // step lanes can play chords
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
        for (int k = 0; k < 2; ++k)
        {
            std::fill (retL[(size_t) k].begin(), retL[(size_t) k].begin() + n, 0.0f);
            std::fill (retR[(size_t) k].begin(), retR[(size_t) k].begin() + n, 0.0f);
        }
        anySolo = false;
        for (auto& tr : tracks) anySolo = anySolo || tr.solo;

        // modulation sources advance once per chunk
        const double chunkSeconds = n / sr;
        const float l1 = lfos[0].advance (globals.lfo[0], chunkSeconds, ppq, run);
        const float l2 = lfos[1].advance (globals.lfo[1], chunkSeconds, ppq, run);
        lfoPhaseUi[0].store (lfos[0].phase()); lfoPhaseUi[1].store (lfos[1].phase());
        expression[(size_t) ModSource::Lfo1] = l1;
        expression[(size_t) ModSource::Lfo2] = l2;
        for (int t = 0; t < numTracks; ++t)
        {
            lifes[(size_t) t].advance (chunkSeconds, tracks[(size_t) t].lifeRate);
            live[(size_t) t] = tracks[(size_t) t];
            applyModulation (t);
        }

        for (int t = 0; t < numTracks; ++t)
        {
            auto& ts     = live[(size_t) t];
            auto& player = players[(size_t) t];
            events.clear();

            // the sequencer needs to know how many slices exist
            ts.seq.drumTrack = ts.engine == EngineType::Drums;
            if (ts.engine == EngineType::Drums)
            {
                ts.seq.sliceCount = numCells;
            }
            else if (ts.engine == EngineType::Slicer)
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

            auto& stepPlayer = stepPlayers[(size_t) t];
            if (run && ts.enabled)
            {
                const double spq = allRates()[(size_t) std::clamp (ts.rateIndex, 0, (int) allRates().size() - 1)].stepsPerQuarter;
                if (ts.stepMode)
                {
                    player.reset (&events, 0);   // releases anything the generative side held
                    const auto ctx = stepContextFor (ts.engine, ts.seq, currentScale, spq, ts.fill, ts.autoFillBars);
                    auto& store = stepStores[(size_t) t];
                    std::lock_guard<SpinMutex> g (store.m);
                    stepPlayer.process (ppq * spq, bpm / 60.0 / sr * spq, n, store.pattern, ctx, events);
                    for (int l = 0; l < stepLanes; ++l) lanePos[(size_t) t][(size_t) l].store (stepPlayer.laneStep (l));
                }
                else
                {
                    stepPlayer.reset (&events, 0);
                    auto& pat = patterns[(size_t) t];
                    std::lock_guard<SpinMutex> g (pat.mutex());
                    player.process (ppq * spq, bpm / 60.0 / sr * spq, n, pat, ts.seq, currentScale, events);
                    playIdx[(size_t) t].store (player.currentIndex());
                }
            }
            else
            {
                player.reset (&events, 0);
                stepPlayer.reset (&events, 0);
                for (auto& lp : lanePos[(size_t) t]) lp.store (-1);
            }

            auto& synth  = synths[(size_t) t];
            auto& slicer = slicers[(size_t) t];
            auto& drum   = drums[(size_t) t];
            const bool isSlicer = ts.engine == EngineType::Slicer;
            const bool isDrums  = ts.engine == EngineType::Drums;

            // engine switched: silence the one no longer used
            if (ts.engine != EngineType::Synth && ! synth.isSilent()) synth.allNotesOff();
            if (! isSlicer && ! slicer.isSilent())    slicer.release();

            const bool insertsActive = ts.inserts[0].type != FxType::None || ts.inserts[1].type != FxType::None;
            if (events.empty() && synth.isSilent() && slicer.isSilent() && drum.isSilent())
            {
                if (isSlicer) { framePos[(size_t) t].store (-1.0); }
                trackPeak[(size_t) t].store (trackPeak[(size_t) t].load() * 0.9f);
                if (! insertsActive) continue;
                std::fill (bufL.begin(), bufL.begin() + n, 0.0f);
                std::fill (bufR.begin(), bufR.begin() + n, 0.0f);
                mixTrack (t, l, r, n, offsetInBlock, bpm);   // let insert reverbs / delays ring out
                continue;
            }

            std::fill (bufL.begin(), bufL.begin() + n, 0.0f);
            std::fill (bufR.begin(), bufR.begin() + n, 0.0f);
            std::fill (bufMono.begin(), bufMono.begin() + n, 0.0f);
            bool synthRendered = false;

            auto renderRange = [&] (int from, int count)
            {
                if (count <= 0) return;
                if (! synth.isSilent())  { synth.render (bufMono.data(), from, count, ts.voice); synthRendered = true; }
                if (! slicer.isSilent()) slicer.render (bufL.data() + 0, bufR.data(), from, count, ts.slicer.voice);
                if (! drum.isSilent())   drum.render (bufL.data(), bufR.data(), from, count);
            };

            int pos = 0;
            for (const auto& e : events)
            {
                if (e.sampleOffset > pos) { renderRange (pos, e.sampleOffset - pos); pos = e.sampleOffset; }
                handleEvent (t, e);
                midiOut.push_back ({ offsetInBlock + e.sampleOffset, t + 1 + (e.stream == 1 ? 8 : 0),
                                     e.noteOn, e.note, e.noteOn ? e.velocity : 0 });
            }
            renderRange (pos, n - pos);

            // the synth is mono: it goes to both sides equally, also while it rings out after
            // the track was switched to another engine (it used to land on the left only)
            if (synthRendered)
                for (int i = 0; i < n; ++i) { bufL[(size_t) i] += bufMono[(size_t) i]; bufR[(size_t) i] += bufMono[(size_t) i]; }
            if (isSlicer)
                framePos[(size_t) t].store (slicer.position());
            (void) isDrums;

            mixTrack (t, l, r, n, offsetInBlock, bpm);
        }

        // returns
        for (int k = 0; k < 2; ++k)
        {
            if (globals.returns[(size_t) k].type == FxType::None) continue;
            returnFx[(size_t) k].process (retL[(size_t) k].data(), retR[(size_t) k].data(), n, globals.returns[(size_t) k], bpm);
            const float g = globals.returnLevel[(size_t) k];
            for (int i = 0; i < n; ++i) { l[i] += retL[(size_t) k][(size_t) i] * g; r[i] += retR[(size_t) k][(size_t) i] * g; }
        }

        // MICRO: everything from the tracks and returns runs through it
        if (globals.micro.on)
            microFx.process (l, r, n, globals.micro, currentScale, bpm, ppq, run);

        // UI audition of a kit cell (goes through the master bus)
        const int req = auditionRequest.exchange (-1);
        if (req >= 0)
        {
            DrumCell c;
            {
                std::lock_guard<SpinMutex> g (kitMutex);
                c = kit[(size_t) std::clamp (req, 0, numCells - 1)];
            }
            auditionPlayer.trigger (c, req, 110, false);
        }
        if (! auditionPlayer.isSilent())
            auditionPlayer.render (l, r, 0, n);

        // master: glue compressor, fader, limiter
        glue.process (l, r, n, globals.glue);
        glueMeter.store (glue.reductionDb());
        for (int i = 0; i < n; ++i) { l[i] *= globals.master; r[i] *= globals.master; }
        if (globals.limiter.on)
        {
            limiter.process (l, r, n, globals.limiter);
            limiterMeter.store (limiter.reductionDb());
        }
        else limiterMeter.store (0.0f);
    }

    void Engine::mixTrack (int t, float* l, float* r, int n, int offsetInBlock, double bpm)
    {
        auto& ts = live[(size_t) t];
        for (int k = 0; k < 2; ++k)
            trackFx[(size_t) t][(size_t) k].process (bufL.data(), bufR.data(), n, ts.inserts[(size_t) k], bpm);

        const bool audible = ! ts.mute && (! anySolo || ts.solo);
        const float pan = std::clamp (ts.pan, -1.0f, 1.0f);
        const float lvl = audible ? ts.level : 0.0f;
        const float gl  = lvl * std::cos ((pan + 1.0f) * 0.25f * 3.14159265f) * 1.41421356f;
        const float gr  = lvl * std::sin ((pan + 1.0f) * 0.25f * 3.14159265f) * 1.41421356f;

        float* dl = directOuts[(size_t) (2 * t)];
        float* dr = directOuts[(size_t) (2 * t + 1)];
        // the main mix always gets every track unless "separate outs" is on; an enabled
        // direct output gets its own copy (hosts often switch all outputs on by default)
        const bool direct = dl != nullptr && dr != nullptr;
        float* outL = direct && globals.separateOuts ? dl + offsetInBlock : l;
        float* outR = direct && globals.separateOuts ? dr + offsetInBlock : r;
        float* copyL = direct && ! globals.separateOuts ? dl + offsetInBlock : nullptr;
        float* copyR = direct && ! globals.separateOuts ? dr + offsetInBlock : nullptr;
        const float sa = ts.sendA, sb = ts.sendB;
        auto& aL = retL[0]; auto& aR = retR[0]; auto& bL = retL[1]; auto& bR = retR[1];

        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float yl = bufL[(size_t) i] * gl, yr = bufR[(size_t) i] * gr;
            outL[i] += yl; outR[i] += yr;
            if (copyL != nullptr) { copyL[i] += yl; copyR[i] += yr; }
            aL[(size_t) i] += yl * sa; aR[(size_t) i] += yr * sa;
            bL[(size_t) i] += yl * sb; bR[(size_t) i] += yr * sb;
            peak = std::max (peak, std::max (std::abs (yl), std::abs (yr)));
        }
        trackPeak[(size_t) t].store (std::max (peak, trackPeak[(size_t) t].load() * 0.9f));
    }
}

namespace bash
{
    float Engine::sourceValue (int t, ModSource s) const
    {
        return sourceUi[(size_t) (t * numModSources + (int) s)].load (std::memory_order_relaxed);
    }

    void Engine::applyModulation (int t)
    {
        float src[numModSources];
        for (int s = 0; s < numModSources; ++s) src[s] = expression[(size_t) s];
        src[(int) ModSource::Life] = lifes[(size_t) t].value();
        for (int s = 0; s < numModSources; ++s) sourceUi[(size_t) (t * numModSources + s)].store (src[s], std::memory_order_relaxed);

        const auto& m = matrices[(size_t) t];
        float sum[numModDests];
        bool anyRoute = false;
        for (int d = 0; d < numModDests; ++d)
        {
            float acc = 0.0f;
            for (int s = 0; s < numModSources; ++s)
            {
                const float a = m.amount[(size_t) d][(size_t) s].load (std::memory_order_relaxed);
                if (a != 0.0f) { acc += a * src[s]; anyRoute = true; }
            }
            sum[d] = acc;
        }
        if (! anyRoute) return;

        auto& L = live[(size_t) t];
        auto unit = [] (float v) { return std::clamp (v, 0.0f, 1.0f); };
        auto d = [&] (ModDest x) { return sum[(int) x]; };

        L.seq.type    = unit (L.seq.type + d (ModDest::Type));
        L.seq.density = unit (L.seq.density + d (ModDest::Density));
        if (d (ModDest::Steps) != 0.0f)
            L.seq.length = (uint32_t) std::clamp ((int64_t) L.seq.length + (int64_t) std::lround (d (ModDest::Steps) * 64.0f), (int64_t) 1, (int64_t) 0xFFFFFFFF);
        if (d (ModDest::Offset) != 0.0f)
            L.seq.offset = (uint32_t) std::max<int64_t> (0, (int64_t) L.seq.offset + std::lround (d (ModDest::Offset) * 32.0f));
        L.seq.accentAmount = unit (L.seq.accentAmount + d (ModDest::Accent));
        L.seq.gate = std::clamp (L.seq.gate + d (ModDest::Gate) * 1.5f, 0.05f, 2.0f);

        L.voice.cutoff    = std::clamp (L.voice.cutoff * std::pow (2.0f, d (ModDest::Cutoff) * 5.0f), 20.0f, 20000.0f);
        L.voice.resonance = unit (L.voice.resonance + d (ModDest::Resonance));
        L.voice.character = unit (L.voice.character + d (ModDest::Character));
        L.voice.timbre    = unit (L.voice.timbre + d (ModDest::Timbre));
        L.voice.envMod    = unit (L.voice.envMod + d (ModDest::EnvMod));
        L.voice.decay     = std::clamp (L.voice.decay * std::pow (2.0f, d (ModDest::Decay) * 3.0f), 0.01f, 8.0f);
        L.voice.drive     = unit (L.voice.drive + d (ModDest::Drive));
        if (d (ModDest::Tune) != 0.0f)
        {
            L.slicer.tune += d (ModDest::Tune) * 24.0f;
            if (L.engine == EngineType::Synth)
                L.seq.baseNote = std::clamp (L.seq.baseNote + (int) std::lround (d (ModDest::Tune) * 24.0f), 0, 127);
        }
        if (d (ModDest::WindowStart) != 0.0f)
            L.seq.windowStart = (uint32_t) std::max<int64_t> (0, (int64_t) L.seq.windowStart + std::lround (d (ModDest::WindowStart) * 32.0f));
        L.level = unit (L.level + d (ModDest::Level));
        L.pan   = std::clamp (L.pan + d (ModDest::Pan), -1.0f, 1.0f);
        L.sendA = unit (L.sendA + d (ModDest::SendA));
        L.sendB = unit (L.sendB + d (ModDest::SendB));
        L.inserts[0].gnarl = unit (L.inserts[0].gnarl + d (ModDest::Fx1Gnarl));
        L.inserts[1].gnarl = unit (L.inserts[1].gnarl + d (ModDest::Fx2Gnarl));
    }
}

namespace bash
{
    StepContext stepContextFor (EngineType engine, const SequencerParams& seq, const Scale& scale, double spq,
                                bool fill, int autoFillBars)
    {
        StepContext c;
        c.stepsPerQuarter = spq;
        c.fill = fill;
        c.autoFillBars = autoFillBars;
        c.swing = seq.swing;
        c.gate = seq.gate;
        c.oneShot = engine == EngineType::Drums;
        for (int l = 0; l < stepLanes; ++l)
        {
            switch (engine)
            {
                case EngineType::Drums:
                    c.laneNote[(size_t) l] = drumBaseNote + l;
                    c.laneSlice[(size_t) l] = l;
                    break;
                case EngineType::Slicer:
                    // lane 1 plays the window's first slice, lane 2 the next, ...
                    c.laneNote[(size_t) l] = seq.baseNote;
                    c.laneSlice[(size_t) l] = (int) seq.windowStart + l;
                    break;
                case EngineType::Synth:
                default:
                    // lanes are scale notes upwards from the track's root and octave
                    c.laneNote[(size_t) l] = std::clamp (seq.scaleLock ? scale.stepFrom (seq.baseNote, l) : seq.baseNote + l, 0, 127);
                    c.laneSlice[(size_t) l] = 0;
                    break;
            }
        }
        return c;
    }

    void Engine::copyStateFrom (Engine& other)
    {
        tracks  = other.tracks;
        globals = other.globals;
        for (int t = 0; t < numTracks; ++t)
        {
            std::string text;
            {
                auto& src = other.patterns[(size_t) t];
                std::lock_guard<SpinMutex> g (src.mutex());
                text = src.serialise();
            }
            {
                auto& dst = patterns[(size_t) t];
                std::lock_guard<SpinMutex> g (dst.mutex());
                dst.deserialise (text);
            }
            {
                StepPattern sp;
                {
                    std::lock_guard<SpinMutex> g (other.stepMutex (t));
                    sp = other.stepPattern (t);
                }
                std::lock_guard<SpinMutex> g (stepMutex (t));
                stepPattern (t) = sp;
            }
            setSlices (t, other.slices (t));
            setSample (t, other.sample (t));
            for (int d = 0; d < numModDests; ++d)
                for (int s = 0; s < numModSources; ++s)
                    matrices[(size_t) t].set ((ModDest) d, (ModSource) s, other.matrices[(size_t) t].get ((ModDest) d, (ModSource) s));
        }
        for (int c = 0; c < numCells; ++c)
            setCell (c, other.cell (c));
    }

    void Engine::renderOffline (Engine& live, const RenderOptions& o, std::vector<float>& left, std::vector<float>& right)
    {
        auto e = std::make_unique<Engine>();
        constexpr int block = 512;
        e->prepare (o.sampleRate, block);
        e->copyStateFrom (live);

        auto& g = e->globals;
        g.hostSync = false;
        g.playing  = true;
        g.tempo    = o.bpm;
        for (auto& t : e->tracks) { t.mute = false; t.solo = false; }

        if (o.track >= 0)
        {
            for (int t = 0; t < numTracks; ++t)
                if (t != o.track) { e->tracks[(size_t) t].enabled = false; e->tracks[(size_t) t].sendA = e->tracks[(size_t) t].sendB = 0.0f; }
            e->tracks[(size_t) o.track].enabled = true;
            g.micro.on = false;
            g.glue.on = false;
            g.limiter.on = false;
            g.master  = 1.0f;
            if (! o.withFx)
            {
                auto& tr = e->tracks[(size_t) o.track];
                tr.inserts[0].type = tr.inserts[1].type = FxType::None;
                tr.sendA = tr.sendB = 0.0f;
            }
        }
        else if (! o.withFx)
        {
            for (auto& tr : e->tracks) { tr.inserts[0].type = tr.inserts[1].type = FxType::None; tr.sendA = tr.sendB = 0.0f; }
            g.glue.on = false;
        }

        const double barSeconds = 4.0 * 60.0 / std::max (20.0, o.bpm);
        const auto loopFrames = (size_t) std::llround (barSeconds * std::max (1, o.bars) * o.sampleRate);
        const auto tailFrames = (size_t) std::llround (std::max (0.0, o.tailSeconds) * o.sampleRate);
        std::vector<float> l (loopFrames + tailFrames + block, 0.0f), r (l.size(), 0.0f);
        std::vector<MidiOut> midi;
        Transport none;
        size_t pos = 0;
        while (pos < loopFrames + tailFrames)
        {
            const int n = (int) std::min<size_t> (block, loopFrames + tailFrames - pos);
            // stop sequencing at the loop end; let voices and effects ring into the tail
            if (pos >= loopFrames) { for (auto& t : e->tracks) t.enabled = false; }
            midi.clear();
            e->process (l.data() + pos, r.data() + pos, n, none, midi);
            pos += (size_t) n;
        }

        // wrap the tail onto the start so the loop plays seamlessly
        left.assign (l.begin(), l.begin() + (long) loopFrames);
        right.assign (r.begin(), r.begin() + (long) loopFrames);
        for (size_t i = 0; i < tailFrames; ++i)
        {
            left[i % loopFrames]  += l[loopFrames + i];
            right[i % loopFrames] += r[loopFrames + i];
        }
    }
}
