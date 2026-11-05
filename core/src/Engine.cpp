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

    Engine::Engine()
    {
        for (int t = 0; t < numTracks; ++t)
        {
            patterns[(size_t) t].setSeed (mix64 (0xBA54ull + (uint64_t) t));
            playIdx[(size_t) t].store (0);
        }
        tracks[0].enabled = true;   // track 1 plays out of the box
        refreshScale();
    }

    void Engine::prepare (double sampleRate, int maxBlockSize)
    {
        sr = sampleRate;
        maxBlock = std::max (32, std::min (maxBlockSize, 2048));
        mono.assign ((size_t) maxBlock, 0.0f);
        events.reserve (256);
        for (auto& v : voices) v.prepare (sr);
        for (auto& p : players) p.reset();
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

    void Engine::process (float* left, float* right, int numSamples,
                          const Transport& tp, std::vector<MidiOut>& midiOut)
    {
        refreshScale();

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
            auto& voice  = voices[(size_t) t];
            events.clear();

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

            if (events.empty() && voice.isSilent())
                continue;

            // render the voice between events
            std::fill (mono.begin(), mono.begin() + n, 0.0f);
            int pos = 0;
            for (const auto& e : events)
            {
                if (e.sampleOffset > pos)
                {
                    voice.render (mono.data(), pos, e.sampleOffset - pos, ts.voice);
                    pos = e.sampleOffset;
                }
                if (e.noteOn) voice.noteOn (e.note, e.velocity, e.accent, e.legato);
                else          voice.noteOff (e.note);

                midiOut.push_back ({ offsetInBlock + e.sampleOffset, t + 1, e.noteOn, e.note, e.noteOn ? e.velocity : 0 });
            }
            if (pos < n)
                voice.render (mono.data(), pos, n - pos, ts.voice);

            const float pan = std::clamp (ts.pan, -1.0f, 1.0f);
            const float gl  = ts.level * globals.master * std::cos ((pan + 1.0f) * 0.25f * 3.14159265f);
            const float gr  = ts.level * globals.master * std::sin ((pan + 1.0f) * 0.25f * 3.14159265f);
            for (int i = 0; i < n; ++i)
            {
                l[i] += mono[(size_t) i] * gl;
                r[i] += mono[(size_t) i] * gr;
            }
        }
    }
}
