#include <climits>
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

        // Sync: the host's tempo, position and play / stop. Sync off: the device's own tempo and Play.
        const bool hostTempo = set.hostSync && tp.hostAvailable;
        const bool useHost   = hostTempo && tp.hostPlaying && tp.hostPpqValid;
        // synced to Live: Live's play / stop starts and stops the sequencer (Play is for when Sync is off)
        const bool run       = hostTempo ? tp.hostPlaying : set.playing;
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
                synth.setPolyphonic (ts.stepMode || ts.seq.algorithm == Algorithm::Counterpoint || ts.seq.algorithm == Algorithm::Chords
                                     || isPolyMachine (ts.voice.machine));   // chords, step lanes, the poly voices
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

    // ---- fills ------------------------------------------------------------------------------------
    // While a fill plays, the pattern's notes make way for random hits: drum rolls running down the
    // toms, stuttering slices, or a run of notes in the key, with ratchets and a rising velocity.
    // "Same" plays the same fill every time; "New" a different one each time (both follow the
    // pattern's seed, so a new pattern brings new fills).

    void Engine::flushFill (bool sendOffs)
    {
        if (sendOffs)
            for (int i = 0; i < fillQn; ++i)
                if (! fillQ[(size_t) i].on)
                {
                    NoteEvent e;
                    e.sampleOffset = 0;
                    e.noteOn = false;
                    e.note = fillQ[(size_t) i].note;
                    e.slice = fillQ[(size_t) i].slice;
                    events.push_back (e);
                }
        fillQn = 0;
        fillLastPos = -1.0;
        fillNext = INT64_MIN;
        fillNow.store (false);
    }

    void Engine::fillStep (const Settings& ts, int64_t step, int k, int fillSteps, int64_t cycle)
    {
        uint64_t seed;
        {
            std::lock_guard<SpinMutex> g (pat.mutex());
            seed = pat.getSeed() ^ 0xF111F111ull;
        }
        const int64_t occurrence = (ts.fillNew && ! ts.fill && cycle > 0) ? (step >= 0 ? step / cycle : (step - cycle + 1) / cycle)
                                 : (ts.fillNew && ts.fill) ? step / std::max (1, fillSteps) : 0;
        const uint64_t base = hash3 (seed, (uint64_t) occurrence, 0x5EED);
        auto rnd = [&] (uint64_t salt) { return unitFloat (base, (uint64_t) k, salt); };
        const float t = fillSteps > 1 ? (float) k / (float) (fillSteps - 1) : 1.0f;   // how far into the fill

        if (k > 0 && rnd (1) < 0.12f) return;                                     // a breath
        int ratchet = 1;
        const float rr = rnd (2);
        if (rr < 0.08f + 0.22f * t) ratchet = rnd (3) < 0.5f ? 3 : 4;
        else if (rr < 0.3f + 0.35f * t) ratchet = 2;
        const int vel = std::clamp ((int) std::lround (64.0f + 60.0f * t * (0.75f + 0.25f * rnd (4))), 1, 127);
        const bool accent = rnd (5) < 0.25f + 0.4f * t;

        int note = ts.seq.baseNote, slice = 0;
        if (ts.engine == EngineType::Drums)
        {
            // snare rolls, kicks, claps and the toms running from high to low
            const float x = rnd (6);
            int pad;
            if (x < 0.36f)      pad = 1;                                             // snare
            else if (x < 0.52f) pad = 0;                                             // kick
            else if (x < 0.84f) pad = t < 0.34f ? 11 : t < 0.67f ? 7 : 6;           // toms, high -> low
            else if (x < 0.93f) pad = 4;                                             // clap
            else                pad = 8;                                             // rim
            note = drumBaseNote + pad;
        }
        else if (ts.engine == EngineType::Slicer)
        {
            const uint32_t count = ts.seq.sliceCount;
            if (count == 0) return;
            slice = (int) uniformInt (base, (uint64_t) k, 7, count);
            if (ratchet == 1 && rnd (8) < 0.35f) ratchet = 2 + (int) uniformInt (base, (uint64_t) k, 9, 3);   // stutters
        }
        else
        {
            // a run in the key: steps up and down from the last fill note, leaping an octave near the end
            if (k == 0) fillNote = ts.seq.baseNote + 12;
            const int move = (int) uniformInt (base, (uint64_t) k, 10, 7) - 3;
            fillNote = currentScale.stepFrom (fillNote, move == 0 ? 1 : move);
            fillNote = std::clamp (fillNote, ts.seq.baseNote - 5, ts.seq.baseNote + 24);
            note = fillNote + (t > 0.6f && rnd (11) < 0.3f ? 12 : 0);
            if (! ts.seq.scaleLock) note += rnd (12) < 0.1f ? 1 : 0;
            note = std::clamp (note, 0, 127);
        }

        for (int j = 0; j < ratchet; ++j)
        {
            const double at = (double) step + (double) j / ratchet;
            const int v = std::clamp (vel - (ratchet - 1 - j) * 6, 1, 127);
            if (fillQn < (int) fillQ.size()) fillQ[(size_t) fillQn++] = { at, note, slice, v, true, accent && j == 0 };
            if (ts.engine != EngineType::Drums && fillQn < (int) fillQ.size())
                fillQ[(size_t) fillQn++] = { at + 0.8 / ratchet, note, slice, 0, false, false };
        }
    }

    void Engine::runFill (Settings& ts, double pos0, double inc, int n, size_t seqFrom)
    {
        const double pos1 = pos0 + inc * n;
        // the transport jumped (start, loop, locate): forget what was queued
        if (fillLastPos >= 0.0 && std::abs (pos0 - fillLastPos) > 0.5) flushFill (true);
        fillLastPos = pos1;

        const double stepsPerBeat = allRates()[(size_t) std::clamp (ts.rateIndex, 0, (int) allRates().size() - 1)].stepsPerQuarter;
        const int fillSteps = std::max (1, (int) std::lround (stepsPerBeat * ts.fillBeats));
        const int64_t cycle = ts.autoFillBars > 0 ? (int64_t) std::lround (stepsPerBeat * 4.0 * ts.autoFillBars) : 0;
        auto fillIndex = [&] (int64_t st) -> int   // where step st is in a fill (-1: not in one)
        {
            if (ts.fill) return (int) (((st % fillSteps) + fillSteps) % fillSteps);
            if (cycle <= 0) return -1;
            const int64_t m = ((st % cycle) + cycle) % cycle;
            const int64_t len = std::min<int64_t> (fillSteps, cycle);
            return m >= cycle - len ? (int) (m - (cycle - len)) : -1;
        };

        const bool any = ts.fill || cycle > 0;
        if (! any) fillNext = INT64_MIN;
        if (! any && fillQn == 0) { fillNow.store (false); return; }

        // the pattern makes way: its notes inside a fill don't play (the slicer's note-offs neither,
        // since the fill's own slices would be cut by them)
        if (any)
        {
            size_t w = seqFrom;
            for (size_t i = seqFrom; i < events.size(); ++i)
            {
                const auto& e = events[i];
                const int64_t st = (int64_t) std::floor (pos0 + e.sampleOffset * inc + 1e-6);
                const bool inFill = fillIndex (st) >= 0;
                const bool drop = inFill && (e.noteOn || ts.engine == EngineType::Slicer);
                if (! drop) events[w++] = e;
            }
            events.resize (w);

            // new fill steps starting in this chunk (counted on from the last one, so rounding in the
            // song position can't skip or repeat a step)
            if (fillNext == INT64_MIN || (double) fillNext < pos0 - 2.0 || (double) fillNext > pos1 + 2.0)
                fillNext = (int64_t) std::ceil (pos0 - 1e-6);
            for (; (double) fillNext < pos1 - 1e-9; ++fillNext)
            {
                const int k = fillIndex (fillNext);
                if (k >= 0) fillStep (ts, fillNext, k, fillSteps, cycle);
            }
            fillNow.store (fillIndex ((int64_t) std::floor (pos0)) >= 0);
        }

        // play what's due
        for (int i = 0; i < fillQn;)
        {
            const auto h = fillQ[(size_t) i];
            if (h.at < pos1)
            {
                NoteEvent e;
                e.sampleOffset = std::clamp ((int) ((h.at - pos0) / std::max (1e-12, inc)), 0, std::max (0, n - 1));
                e.noteOn = h.on;
                e.note = h.note;
                e.slice = h.slice;
                e.velocity = h.velocity;
                e.accent = h.accent;
                events.push_back (e);
                fillQ[(size_t) i] = fillQ[(size_t) --fillQn];
            }
            else ++i;
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
            ts.seq.sliceCount = midiOutOn.load (std::memory_order_relaxed) ? (uint32_t) std::clamp (ts.midiSlices, 1, maxSlices)
                              : lastSample != nullptr ? (uint32_t) sliceStore.points.size() : 0u;
        }
        else
            ts.seq.sliceCount = 0;
        ts.seq.heldCount = heldInCount;
        ts.seq.held = heldIn;

        const size_t seqFrom = events.size();   // events before this came from MIDI in
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
            runFill (ts, ppq * spq, bpm / 60.0 / sr * spq, n, seqFrom);
        }
        else
        {
            player.reset (&events, 0);
            stepPlayer.reset (&events, 0);
            for (auto& lp : lanePos) lp.store (-1);
            flushFill (true);
        }
        // Strum: a chord's notes come in one after another (up to ~45 ms apart), held back across
        // blocks when they must be; a note's off never overtakes its own delayed on
        {
            const double gap = std::clamp (ts.strum, 0.0f, 1.0f) * 0.045 * sr;
            for (int i = 0; i < strumQn;)
            {
                auto& q = strumQ[(size_t) i];
                if (q.samplesLeft < n)
                {
                    NoteEvent e = q.e;
                    e.sampleOffset = (int) std::max (0.0, q.samplesLeft);
                    events.push_back (e);
                    strumQ[(size_t) i] = strumQ[(size_t) --strumQn];
                }
                else { q.samplesLeft -= n; ++i; }
            }
            if (gap > 0.0 || strumQn > 0)
            {
                // where this block's delayed note-ons land (an off of the same note must come after)
                struct Landing { int note, from; double at; };
                std::array<Landing, 64> landed {};
                int nl = 0;
                for (const auto& e : events)
                    if (e.noteOn && e.strumIndex > 0 && gap > 0.0 && nl < (int) landed.size())
                        landed[(size_t) nl++] = { e.note, e.sampleOffset, e.sampleOffset + e.strumIndex * gap };
                size_t w = 0;
                for (size_t i = 0; i < events.size(); ++i)
                {
                    NoteEvent e = events[i];
                    double delay = 0.0;
                    if (e.noteOn && e.strumIndex > 0 && gap > 0.0) delay = e.strumIndex * gap;
                    else if (! e.noteOn)
                    {
                        for (int k = 0; k < strumQn; ++k)
                            if (strumQ[(size_t) k].e.noteOn && strumQ[(size_t) k].e.note == e.note)
                                delay = std::max (delay, strumQ[(size_t) k].samplesLeft + 1.0 - e.sampleOffset + n);
                        for (int k = 0; k < nl; ++k)
                            if (landed[(size_t) k].note == e.note && e.sampleOffset > landed[(size_t) k].from && landed[(size_t) k].at >= e.sampleOffset)
                                delay = std::max (delay, landed[(size_t) k].at + 1.0 - e.sampleOffset);
                    }
                    e.strumIndex = 0;
                    const double at = e.sampleOffset + delay;
                    if (at < n) { e.sampleOffset = (int) at; events[w++] = e; }
                    else if (strumQn < (int) strumQ.size()) strumQ[(size_t) strumQn++] = { at - n, e };
                }
                events.resize (w);
            }
        }

        // events from MIDI (offset 0) and the sequencer must stay in time order
        std::stable_sort (events.begin(), events.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.sampleOffset < b.sampleOffset; });

        const bool isSlicer = ts.engine == EngineType::Slicer;

        // engine switched: cut the one no longer used dead, so nothing rings over the new one
        if ((int) ts.engine != lastEngine)
        {
            if (lastEngine >= 0)
            {
                if (ts.engine != EngineType::Synth) synth.reset();
                if (! isSlicer) { slicer.reset(); sliceAuditioner.reset(); slicerNote = -1; }
                if (ts.engine != EngineType::Drums) { drums.stopAll(); auditionPlayer.stopAll(); }
            }
            lastEngine = (int) ts.engine;
        }
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
