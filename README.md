# BASH!

**An all-in-one kalabash solution.** A generative groovebox: eight tracks, each driven by its own generative acid sequencer, playing a slice sampler, a drum grid (samples + FM drums) or a machine-style mono synth, into a mixer with gnarly effects.

Formats: VST3, AU, Standalone (Mac and Windows), AUv3 + standalone app on iPad.

Full spec: *BASH! — Software Requirements Specification* (Claude doc).

## Status

**Milestone 1 — core engine** ✅ in progress

- 8 tracks, host sync (DAW tempo/transport) or internal clock
- Global key and scale (15 scales), per-track scale lock and octave
- Generative sequencer: Core Acid V1 / V2, Type morph, Density, Steps, Cycle (patterns up to 2³² steps), Offset, Direction, Rate, Accent, Gate, slide
- Seed locking: click a step to lock it; Regenerate keeps locked steps
- Acid test voice on every track (saw/square, resonant 24 dB filter, accent, glide)
- Generated MIDI out on channel = track number
- Patterns and locks save with the DAW session

**Milestone 2 — slice sampler** ✅

- Per-track engine choice: Synth or Slicer (Drums coming)
- Drag an audio file onto the waveform (or Load); auto-sliced by transients
- Up to 256 slices: Transients (sensitivity), Grid (4–256), Random (count), or by hand: double-click to add, drag to move, right-click to delete
- Slice modes: Window (start + size, shaded on the waveform), Bounce (anywhere), Sequential
- Pitch follow (sequencer notes repitch slices), Tune, Attack, Release, Reverse; click-free retriggers
- Slice choices can be locked per step; samples and slices save with the session

**Milestone 3 — machine synth + all four algorithms** ✅

- Machines: Acid (saw↔square + sub), SID (pulse width + bit-crush), FM+ (2-op with feedback), SuperWave (7 detuned saws), VO (vowel formants), Pulse-Interval, Noise (resonant pitched noise)
- Character and Timbre knobs change meaning per machine; 24 dB low/band/high-pass filter, filter envelope, accent, drive, glide, ADSR amp envelope; 4-voice polyphony for chords
- Algorithms: Core Acid V1/V2, Counterpoint (bar-wise harmony → in-key chords on the track's channel, complementary bass on channel + 8), Arpeggiator (plays only the notes you hold on MIDI in), Dedicated Bass (grid-locked driving low end)
- MIDI CC 102/103 set Key/Scale (used by the Ableton key-lock device)

Next: drum grid → modulation, Life, Roll the Dice, mixer + FX → MIDI import/export → iPad → Max for Live companion.

## Getting builds

Every push to `main` builds on GitHub Actions. Open the **Actions** tab, pick the latest run, and download:

- **BASH-mac** — a DMG with `BASH.vst3`, `BASH.component` (AU) and `BASH.app`
- **BASH-windows** — a zip with `BASH.vst3` and `BASH.exe`

Install steps are in the `INSTALL` file inside each download. Pushing a tag like `v0.1.0` also publishes a GitHub Release with both.

## Layout

```
core/      Pure C++17 engine, no JUCE: sequencer, scales, voices, engine.
           Shared by every plugin format, the iPad app and the tests.
plugin/    JUCE wrapper: parameters, host sync, state, editor.
tests/     Core unit tests (run: build/bash_core_tests).
packaging/ Install notes bundled with builds.
```

## Building locally

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j 8
./build/bash_core_tests
```

JUCE 8 is fetched automatically. Plugins land in `build/plugin/BASH_artefacts/Release/`.

## How the sequencer works

Each step is computed from `hash(seed, step)`, never stored, so a pattern can run for billions of steps and the same seed always gives the same music. Locks are a small list of per-step overrides that Regenerate skips. Density compares a fixed per-step priority against the knob, so raising it only ever adds notes and never changes the motif.
