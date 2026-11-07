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
        pat.setSeed (mix64 (0xBA54ull));
        life.setSeed (mix64 (0x11FE0000ull));
        sliceStore.points.reserve (maxSlices + 1);
        for (auto& l : lanePos) l.store (-1);
        for (auto& h : hits) h.store (0);
        for (auto& s : sourceUi) s.store (0.0f);
        for (auto& p : lfoPhaseUi) p.store (0.0);
        for (auto& r : retiredCellSamples) r.store (nullptr);
        refreshScale();
    }

    void Engine::prepare (double sampleRate, int maxBlockSize)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        maxBlock = std::max (32, std::min (maxBlockSize, 128));   // modulation updates every <= 128 samples
        bufL.assign ((size_t) maxBlock, 0.0f);
        bufR.assign ((size_t) maxBlock, 0.0f);
        bufMono.assign ((size_t) maxBlock, 0.0f);
        events.reserve (256);
        synth.prepare (sr);
        slicer.prepare (sr);
        sliceAuditioner.prepare (sr);
        drums.prepare (sr);
        auditionPlayer.prepare (sr);
        player.reset();
        stepPlayer.reset();
    }

    // ---- MIDI in -----------------------------------------------------------------
    void Engine::queueNote (int note, int velocity)
    {
        std::lock_guard<SpinMutex> g (inMutex);
        if (inCount < (int) inQueue.size())
            inQueue[(size_t) inCount++] = { std::clamp (note, 0, 127), std::clamp (velocity, 0, 127) };
    }

    void Engine::queueAllNotesOff()
    {
        std::lock_guard<SpinMutex> g (inMutex);
        inCount = 0;
        inAllOff = true;
    }

    void Engine::heldNote (int note, int velocity)
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
        if (set.root != scaleRoot || set.scaleIndex != scaleIdx)
        {
            currentScale = Scale (set.root, set.scaleIndex);
            scaleRoot = set.root;
            scaleIdx  = set.scaleIndex;
        }
    }

    // ---- UI-thread helpers -----------------------------------------------------------
    std::vector<StepData> Engine::snapshot (const SequencerParams& params, const Scale& sc, uint32_t first, uint32_t count)
    {
        std::vector<StepData> out;
        out.reserve (count);
        std::lock_guard<SpinMutex> g (pat.mutex());
        for (uint32_t i = 0; i < count; ++i)
            out.push_back (pat.step (first + i, params, sc));
        return out;
    }

    void Engine::lockStep (const SequencerParams& params, const Scale& sc, uint32_t index, uint8_t mask)
    {
        std::lock_guard<SpinMutex> g (pat.mutex());
        const StepData current = pat.step (index, params, sc);
        pat.lock (index, mask, current);
    }

    void Engine::setSlices (std::vector<uint32_t> s)
    {
        if (s.size() > (size_t) maxSlices) s.resize ((size_t) maxSlices);
        std::lock_guard<SpinMutex> g (sliceStore.m);
        sliceStore.points.assign (s.begin(), s.end());   // capacity reserved: no allocation
    }

    std::vector<uint32_t> Engine::slices()
    {
        std::lock_guard<SpinMutex> g (sliceStore.m);
        return sliceStore.points;
    }

    uint32_t Engine::sliceCount()
    {
        std::lock_guard<SpinMutex> g (sliceStore.m);
        return (uint32_t) sliceStore.points.size();
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
            retiredCellSamples[(size_t) index].store (old);   // the audio thread stops voices using it
    }

    DrumCell Engine::cell (int index)
    {
        std::lock_guard<SpinMutex> g (kitMutex);
        return kit[(size_t) std::clamp (index, 0, numCells - 1)];
    }

    // ---- audio -------------------------------------------------------------------------
    void Engine::process (float* left, float* right, int numSamples, const Transport& tp)
    {
        refreshScale();

        // drop voices still reading a sample that has just been replaced
        const SampleData* now = sampleNow.load();
        if (now != lastSample)
        {
            slicer.reset();
            sliceAuditioner.reset();
            slicerNote = -1;
            lastSample = now;
        }
        for (auto& r : retiredCellSamples)
            if (const SampleData* old = r.exchange (nullptr))
            {
                drums.stopVoicesUsing (old);
                auditionPlayer.stopVoicesUsing (old);
            }

        // Sync: always the host's tempo. While the host plays, follow its position; while it
        // is stopped, the device's own Play still runs (at the host's tempo).
        const bool hostTempo = set.hostSync && tp.hostAvailable;
        const bool useHost   = hostTempo && tp.hostPlaying && tp.hostPpqValid;
        const bool run       = useHost || set.playing;
        const double bpm     = std::max (1.0, hostTempo ? tp.hostBpm : set.tempo);
        double ppq           = useHost ? tp.hostPpq : internalPpq;

        if (run && ! wasRunning && ! useHost)
            ppq = internalPpq = 0.0;            // the internal clock restarts from bar 1
        wasRunning = run;
        running.store (run);
        bpmNow.store (bpm);

        const double ppqPerSample = bpm / 60.0 / sr;
        float pk = 0.0f;
        int done = 0;
        while (done < numSamples)
        {
            const int n = std::min (maxBlock, numSamples - done);
            renderChunk (left + done, right + done, n, run, ppq, bpm);
            for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::abs (left[done + i]), std::abs (right[done + i])));
            ppq += ppqPerSample * n;
            done += n;
        }
        beatNow.store (ppq);
        peak.store (std::max (pk, peak.load() * 0.85f));

        if (! useHost && run)
            internalPpq = ppq;

        blockCount.fetch_add (1);
    }

    void Engine::sendMidi (int note, int velocity)
    {
        std::lock_guard<SpinMutex> g (midiMutex);
        if (midiCount < (int) midiQueue.size()) midiQueue[(size_t) midiCount++] = { std::clamp (note, 0, 127), std::clamp (velocity, 0, 127) };
    }

    int Engine::takeMidi (MidiNote* dest, int maxCount)
    {
        std::lock_guard<SpinMutex> g (midiMutex);
        const int n = std::min (maxCount, midiCount);
        for (int i = 0; i < n; ++i) dest[i] = midiQueue[(size_t) i];
        for (int i = n; i < midiCount; ++i) midiQueue[(size_t) (i - n)] = midiQueue[(size_t) i];
        midiCount -= n;
        return n;
    }

    void Engine::handleEvent (const NoteEvent& e)
    {
        auto& ts = live;
        if (midiOutOn.load (std::memory_order_relaxed))
        {
            // the sequencer as a MIDI device: send the note on instead of playing it
            int note = e.note;
            if (ts.engine == EngineType::Slicer)
            {
                if (e.noteOn) { note = drumBaseNote + std::max (0, e.slice); lastSliceOut = note; }
                else          { note = lastSliceOut; lastSliceOut = -1; if (note < 0) return; }
            }
            if (e.noteOn)
            {
                sendMidi (note, std::max (1, e.velocity));
                lastNoteUi.store (note);
                noteCounter.fetch_add (1);
                hits[(size_t) ((ts.engine == EngineType::Synth ? note : note - drumBaseNote + 1600) % numCells)].fetch_add (1);
                if (ts.engine == EngineType::Drums && pendingCount < (int) pendingOffs.size())
                    pendingOffs[(size_t) pendingCount++] = { note, sr * 0.08 };   // drum hits get a short note
                if (ts.engine == EngineType::Slicer) sliceNow.store (e.slice);
            }
            else if (ts.engine != EngineType::Drums)
                sendMidi (note, 0);
            return;
        }
        switch (ts.engine)
        {
            case EngineType::Slicer:
            {
                if (! e.noteOn)
                {
                    if (e.note == slicerNote) { slicer.release(); slicerNote = -1; }
                    return;
                }
                const SampleData* s = lastSample;
                if (s == nullptr) return;
                uint32_t a = 0, b = 0;
                {
                    std::lock_guard<SpinMutex> g (sliceStore.m);
                    if (sliceStore.points.empty()) return;
                    const size_t i = (size_t) std::clamp (e.slice, 0, (int) sliceStore.points.size() - 1);
                    a = sliceStore.points[i];
                    b = i + 1 < sliceStore.points.size() ? sliceStore.points[i + 1] : s->numFrames();
                }
                const float semis = (ts.slicer.pitchFollow ? (float) (e.note - ts.seq.baseNote) : 0.0f) + ts.slicer.tune;
                const double rate = s->sampleRate / sr * std::pow (2.0, semis / 12.0);
                const float gain  = (0.45f + 0.55f * (float) e.velocity / 127.0f) * (e.accent ? 1.15f : 1.0f);
                slicer.start (s, a, b, rate, gain, ts.slicer.voice);
                slicerNote = e.note;
                sliceNow.store (e.slice);
                hits[(size_t) (e.slice % numCells)].fetch_add (1);
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
                drums.trigger (c, idx, e.velocity, e.accent);
                sliceNow.store (idx);
                hits[(size_t) idx].fetch_add (1);
                return;
            }

            case EngineType::Synth:
            default:
            {
                synth.setPolyphonic (ts.stepMode || ts.seq.algorithm == Algorithm::Counterpoint);   // step lanes can play chords
                if (e.noteOn)
                {
                    synth.noteOn (e.note, e.velocity, e.accent, e.legato);
                    lastNoteUi.store (e.note);
                    noteCounter.fetch_add (1);
                    hits[(size_t) (e.note % numCells)].fetch_add (1);
                }
                else synth.noteOff (e.note);
                return;
            }
        }
    }

    void Engine::renderChunk (float* l, float* r, int n, bool run, double ppq, double bpm)
    {
        std::fill (l, l + n, 0.0f);
        std::fill (r, r + n, 0.0f);

        // modulation sources advance once per chunk
        const double chunkSeconds = n / sr;
        expression[(size_t) ModSource::Lfo1] = lfos[0].advance (set.lfo[0], chunkSeconds, ppq, run);
        expression[(size_t) ModSource::Lfo2] = lfos[1].advance (set.lfo[1], chunkSeconds, ppq, run);
        lfoPhaseUi[0].store (lfos[0].phase()); lfoPhaseUi[1].store (lfos[1].phase());
        life.advance (chunkSeconds, set.lifeRate);
        live = set;
        applyModulation();

        auto& ts = live;
        events.clear();

        // MIDI in: the Arpeggiator's held notes, and (thru) notes that play the voice directly
        {
            std::lock_guard<SpinMutex> g (inMutex);
            if (inAllOff)
            {
                inAllOff = false;
                heldInCount = 0;
                if (ts.engine == EngineType::Synth) synth.allNotesOff();
            }
            const bool arp = ! ts.stepMode && ts.seq.algorithm == Algorithm::Arpeggiator && ts.sequencerOn;
            for (int i = 0; i < inCount; ++i)
            {
                const auto in = inQueue[(size_t) i];
                heldNote (in.note, in.velocity);
                if (arp || ! ts.midiThru) continue;
                NoteEvent e;
                e.sampleOffset = 0;
                e.noteOn = in.velocity > 0;
                e.velocity = in.velocity;
                e.accent = in.velocity >= 110;
                e.note = in.note;
                if (ts.engine == EngineType::Slicer)
                    e.slice = (int) ts.seq.windowStart + std::max (0, in.note - drumBaseNote);   // C1 = the window's first slice
                events.push_back (e);
            }
            inCount = 0;
        }

        // the sequencer needs to know how many slices exist
        ts.seq.drumTrack = ts.engine == EngineType::Drums;
        if (ts.engine == EngineType::Drums)
            ts.seq.sliceCount = numCells;
        else if (ts.engine == EngineType::Slicer)
        {
            std::lock_guard<SpinMutex> g (sliceStore.m);
            ts.seq.sliceCount = lastSample != nullptr ? (uint32_t) sliceStore.points.size() : 0u;
        }
        else
            ts.seq.sliceCount = 0;
        ts.seq.heldCount = heldInCount;
        ts.seq.held = heldIn;

        if (run && ts.sequencerOn)
        {
            const double spq = allRates()[(size_t) std::clamp (ts.rateIndex, 0, (int) allRates().size() - 1)].stepsPerQuarter;
            if (ts.stepMode)
            {
                player.reset (&events, 0);   // releases anything the generative side held
                const auto ctx = stepContextFor (ts.engine, ts.seq, currentScale, spq, ts.fill, ts.autoFillBars);
                std::lock_guard<SpinMutex> g (stepStore.m);
                stepPlayer.process (ppq * spq, bpm / 60.0 / sr * spq, n, stepStore.pattern, ctx, events);
                for (int l = 0; l < stepLanes; ++l) lanePos[(size_t) l].store (stepPlayer.laneStep (l));
            }
            else
            {
                stepPlayer.reset (&events, 0);
                std::lock_guard<SpinMutex> g (pat.mutex());
                player.process (ppq * spq, bpm / 60.0 / sr * spq, n, pat, ts.seq, currentScale, events);
                playIdx.store (player.currentIndex());
            }
        }
        else
        {
            player.reset (&events, 0);
            stepPlayer.reset (&events, 0);
            for (auto& lp : lanePos) lp.store (-1);
        }
        // events from MIDI (offset 0) and the sequencer must stay in time order
        std::stable_sort (events.begin(), events.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.sampleOffset < b.sampleOffset; });

        const bool isSlicer = ts.engine == EngineType::Slicer;

        // engine switched: silence the one no longer used
        if (ts.engine != EngineType::Synth && ! synth.isSilent()) synth.allNotesOff();
        if (! isSlicer && ! slicer.isSilent()) slicer.release();

        // UI auditions (a pad or a slice clicked in the device)
        const int padReq = auditionRequest.exchange (-1);
        if (padReq >= 0)
        {
            DrumCell c;
            {
                std::lock_guard<SpinMutex> g (kitMutex);
                c = kit[(size_t) std::clamp (padReq, 0, numCells - 1)];
            }
            auditionPlayer.trigger (c, padReq, 110, false);
            hits[(size_t) std::clamp (padReq, 0, numCells - 1)].fetch_add (1);
        }
        const int sliceReq = sliceAudition.exchange (-1);
        if (sliceReq >= 0 && lastSample != nullptr)
        {
            std::lock_guard<SpinMutex> g (sliceStore.m);
            if (! sliceStore.points.empty())
            {
                const size_t i = (size_t) std::clamp (sliceReq, 0, (int) sliceStore.points.size() - 1);
                const uint32_t a = sliceStore.points[i];
                const uint32_t b = i + 1 < sliceStore.points.size() ? sliceStore.points[i + 1] : lastSample->numFrames();
                sliceAuditioner.start (lastSample, a, b, lastSample->sampleRate / sr * std::pow (2.0, ts.slicer.tune / 12.0), 0.9f, ts.slicer.voice);
                sliceNow.store ((int) i);
                hits[i % numCells].fetch_add (1);
            }
        }

        if (midiOutOn.load (std::memory_order_relaxed))
        {
            // MIDI out: notes go out in time order; nothing plays here; drum hits end after 80 ms
            for (const auto& e : events) handleEvent (e);
            for (int i = 0; i < pendingCount;)
            {
                pendingOffs[(size_t) i].samplesLeft -= n;
                if (pendingOffs[(size_t) i].samplesLeft <= 0.0)
                {
                    sendMidi (pendingOffs[(size_t) i].note, 0);
                    pendingOffs[(size_t) i] = pendingOffs[(size_t) --pendingCount];
                }
                else ++i;
            }
            return;
        }

        const bool idle = events.empty() && synth.isSilent() && slicer.isSilent() && drums.isSilent()
                          && auditionPlayer.isSilent() && sliceAuditioner.isSilent();
        if (idle)
        {
            if (isSlicer) framePos.store (-1.0);
            return;
        }

        std::fill (bufL.begin(), bufL.begin() + n, 0.0f);
        std::fill (bufR.begin(), bufR.begin() + n, 0.0f);
        std::fill (bufMono.begin(), bufMono.begin() + n, 0.0f);
        bool synthRendered = false;

        auto renderRange = [&] (int from, int count)
        {
            if (count <= 0) return;
            if (! synth.isSilent())  { synth.render (bufMono.data(), from, count, ts.voice); synthRendered = true; }
            if (! slicer.isSilent()) slicer.render (bufL.data(), bufR.data(), from, count, ts.slicer.voice);
            if (! drums.isSilent())  drums.render (bufL.data(), bufR.data(), from, count);
        };

        int pos = 0;
        for (const auto& e : events)
        {
            const int at = std::clamp (e.sampleOffset, 0, n);
            if (at > pos) { renderRange (pos, at - pos); pos = at; }
            handleEvent (e);
        }
        renderRange (pos, n - pos);

        if (! sliceAuditioner.isSilent()) sliceAuditioner.render (bufL.data(), bufR.data(), 0, n, ts.slicer.voice);
        if (! auditionPlayer.isSilent())  auditionPlayer.render (bufL.data(), bufR.data(), 0, n);

        // the synth is mono: both sides equally
        if (synthRendered)
            for (int i = 0; i < n; ++i) { bufL[(size_t) i] += bufMono[(size_t) i]; bufR[(size_t) i] += bufMono[(size_t) i]; }
        if (isSlicer)
            framePos.store (! slicer.isSilent() ? slicer.position() : ! sliceAuditioner.isSilent() ? sliceAuditioner.position() : -1.0);

        const float pan = std::clamp (ts.pan, -1.0f, 1.0f);
        const float lvl = std::clamp (ts.level, 0.0f, 1.0f);
        const float gl  = lvl * std::cos ((pan + 1.0f) * 0.25f * 3.14159265f) * 1.41421356f;
        const float gr  = lvl * std::sin ((pan + 1.0f) * 0.25f * 3.14159265f) * 1.41421356f;
        for (int i = 0; i < n; ++i) { l[i] = bufL[(size_t) i] * gl; r[i] = bufR[(size_t) i] * gr; }
    }

    void Engine::applyModulation()
    {
        float src[numModSources];
        for (int s = 0; s < numModSources; ++s) src[s] = expression[(size_t) s];
        src[(int) ModSource::Life] = life.value();
        for (int s = 0; s < numModSources; ++s) sourceUi[(size_t) s].store (src[s], std::memory_order_relaxed);

        float sum[numModDests];
        bool anyRoute = false;
        for (int d = 0; d < numModDests; ++d)
        {
            float acc = 0.0f;
            for (int s = 0; s < numModSources; ++s)
            {
                const float a = mods.amount[(size_t) d][(size_t) s].load (std::memory_order_relaxed);
                if (a != 0.0f) { acc += a * src[s]; anyRoute = true; }
            }
            sum[d] = acc;
        }
        if (! anyRoute) return;

        auto& L = live;
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
    }

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
                    // lanes are scale notes upwards from the root and octave
                    c.laneNote[(size_t) l] = std::clamp (seq.scaleLock ? scale.stepFrom (seq.baseNote, l) : seq.baseNote + l, 0, 127);
                    c.laneSlice[(size_t) l] = 0;
                    break;
            }
        }
        return c;
    }
}
