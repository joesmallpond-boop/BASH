# BASH!

**An all-in-one kalabash solution.** A generative groovebox: eight tracks, each driven by its own generative acid sequencer, playing a slice sampler, a drum grid (samples + FM drums) or a machine-style mono synth, into a mixer with gnarly effects.

Formats: VST3, AU, Standalone (Mac and Windows), AUv3 + standalone app on iPad.

Full spec: *BASH! — Software Requirements Specification* (Claude doc).

## Status

**Milestone 1 — core engine** ✅

- 8 tracks, host sync (DAW tempo/transport) or internal clock
- Global key and scale (15 scales), per-track scale lock and octave
- Generative sequencer: Core Acid V1 / V2, Type morph, Density, Steps (1-64), Offset, Direction, Rate, Accent, Gate, slide
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

**Milestone 4 — drum kit** ✅

- 4×4 pad grid (notes 36–51). Each pad is a sample or an FM drum; click to audition
- Drop samples on pads (several files fill several pads); sample editor: start/end (drag on the waveform), fade in/out, transient attack/sustain, tune, low-pass, reverse
- FM drum engine: pitch + sweep, FM ratio/amount/decay, noise with tone, clap bursts, click, drive; starting points for Kick, Snare, Clap, Cymbal, Hat, Tom plus 30 FM sounds
- 5 factory kits (Acid House, Electro, Industrial, Minimal, Big Room), choke groups, level/pan per pad
- Drum tracks: Groove mode gives each pad in the window a role (kick, snare, closed hat, open hat, perc…) and plays several hits per step; other slice modes pick one pad per step
- Kit (pads, sounds, sample paths) saves with the session

**Milestone 5 — mixer and effects** ✅

- Mixer view: per track 2 inserts, Send A/B (post-fader), pan, mute/solo, fader and meter; Return A (Plate) and Return B (Tape Delay) with their own effect; MICRO channel; Master with glue compressor and limiter
- 21 effects, each with A/B knobs, a Gnarl macro (tasteful → wrecked) and Mix:
  distortion (Fuzz, Hard Clip, Wavefolder, Bitcrush, Rectifier), saturation (Tape with wow, Tube, Transformer), reverb (Plate, Hall, Spring, Shimmer; full Gnarl freezes into an infinite wash), delay (Tape, Digital, Ping-Pong, Grain; tempo-synced, feedback past 100% held by a soft limiter), modulation (Chorus, Flanger, Phaser, Ring Mod, Frequency Shifter)
- Multi-out: 8 extra stereo outputs ("Track 1"–"Track 8"); enable one in your DAW and that track leaves the main mix

**Milestone 6 — modulation, Life, Roll the Dice** ✅

- MOD view: LFO 1 and LFO 2 (sine, triangle, saw, square, S&H, smooth; free Hz or synced 1/16 to 32 bars), a Life drift generator per track (smooth wandering noise, its own rate), and a matrix per track: 21 destinations (Type, Density, Steps, Offset, Accent, Gate, Cutoff, Resonance, Character, Timbre, Env Mod, Decay, Drive, Tune, Window, Level, Pan, Sends, FX Gnarl) × 7 sources (LFO 1, LFO 2, Life, Velocity, Aftertouch/MPE slide, Mod wheel, Pitch bend). Drag a cell to set its amount
- Roll the Dice (top-right die): click to roll, right-click for scope (everything / this track / its sequencer / its sound / its effects), chaos amount (10%–anything goes), whether key, tempo, levels and patterns are included, and Undo (16 rolls). Right-click any knob to protect it from the dice (its label turns coral). Track on/off, engine choice, mute/solo, master and the limiter are never rolled

**Milestone 7 — MIDI and audio in/out** ✅

- Drag out: every track has MIDI and WAV drag chips (track view, top-right of the Sequencer panel) and each mixer strip has a WAV chip; the master strip's MIX chip renders all 8 tracks with returns and master FX as one WAV. Drag into your DAW or onto the desktop; click a chip to save the file to Music/BASH Exports and show it
- Pick 1–32 bars and whether to render with FX. Audio renders offline (faster than real time) at the session tempo, 24-bit WAV, with effect tails wrapped round to the start so loops are seamless
- MIDI export keeps swing, jitter, gate lengths, direction and odd lengths; Counterpoint bass goes on channel 2
- MIDI import: drop a .mid on the step grid. Its notes, rhythm and accents become the track's motif: Type 0 plays it exactly, turning Type up morphs away from it (Clear MIDI removes it). Drum tracks map notes 36–51 to the pads
- Swing and Jitter per track

**Milestone 8 — iPad** ✅ (needs your Apple team to install: `scripts/build-ipad.sh`)

- Standalone iPad app + AUv3 from the same code; the whole UI scales to any window (also resizable in DAWs); on iPad the MIDI/WAV chips open the share sheet

**Milestone 9 — Ableton key lock** ✅ (untested in Live yet)

- `m4l/BASH Key Lock.amxd`: put it before BASH! on the track and BASH! follows the set's key and scale (any Live scale maps to BASH!'s nearest). See `m4l/README.md`

**0.5.0** ✅ — MICRO and the master bus

- **MICRO** (its own tab, and a channel on the mixer): everything from the tracks and returns runs through it before the master. 11 effects in four families, each with variations A-D: Micro Loop (Mosaic, Sequencer, Glide), Granules (Haze, Tunnel, Strum), Glitch (Blocks, Interrupt, Arp), Multi Delay (Pattern, Warp). Activity, Repeats, Shape, Time (locked to the tempo), Filter + Resonance with an LFO, Space (Room, Plate, Hall, Cloud reverb) and Decay, Mix, Level, Reverse / Alternate, Hold (freeze) and a die
- A live waveform of the last 8 s with a marker wherever MICRO is reading from, and the looper's phrase underneath with its playhead
- **Looper**: 60 s, REC / PLAY / DUB, Stop, Undo, Erase, Burst (records while held), Reverse, Speed (x1/4 to x4), Fade, Post FX / Pre FX / Looper Only, Quantize to beats; drag the phrase out as a WAV
- **Master**: SSL-style glue compressor (threshold, ratio 2/4/10, attack, release incl. Auto, makeup, mix, sidechain HPF, GR meter) before the fader, and the limiter now has drive, ceiling and release with a GR meter. The master insert slots are gone
- The 0.4.0 granular section is replaced by MICRO


- **133 drum sounds**, all synthesised by BASH! (nothing sampled from anyone's records), voiced for UK bass, DnB, dubstep, UKG, broken beat, techno, electro, IDM and beat tape: sub kicks tuned to F/G/A/A#, halfstep, roller, garage, warehouse (with rumble), dusty and broken kicks; halfstep, roller, garage, dusty, ghost and industrial snares and rims; claps; closed/open hats; rides and crashes; shakers; congas, bongos, timbales, woodblocks, bleeps, zaps, metal and glass hits; toms; FX (sub drop, impact, horn stab, dub siren, reese and wobble stabs, risers); glitch. Drum panel > **Library** puts any of them on the selected pad (the menu stays open so you can click through and hear them)
- **12 kits** with starter beats: Halfstep Pressure, 2-Step Shuffle, Rollers, Jungle Ruff, Bleep & Bass, Warehouse, Broken Electro, Glass Garden, Beat Tape, Glitch Lab, Sub Weight, Funky Percs (Drum panel > **Kits**)
- **50 groove presets** in the step sequencer (**Grooves**): UK Garage, UK Funky, Dubstep / Halfstep, Drum & Bass, Jungle, Footwork, Broken Beat, UK Bass / Bleep, Techno / Warehouse, Electro, IDM / Glitch, Beat Tape / Wonky. Each sets the pattern, swing and rate; written for the kit lane layout, so they work with every kit
- **32 slicer loops** (Slice Sampler > **Loops**): breaks (jungle, rollers, halfstep, 2-step, funky, broken beat, beat tape, wonky, IDM, techno, electro), basses (reese, wobble, sub, garage, bleep), chords (garage organ, dub chords, pads, glass arp, beat-tape keys, bleep melody), vox chops and pads, textures. They load at their own tempo and warp to yours
- The library is written once to **Music/BASH Library** the first time BASH! runs (a few seconds), so the sounds are ordinary WAVs you can also drag into your DAW
- Warp reads the tempo from file names ("loop 140bpm", "Reese 174") before guessing

**0.2.0** ✅

- **Step sequencer** on any track (GENERATIVE / STEP switch in the Sequencer box). 16 lanes: drum pads, slices, or scale notes on synth tracks; up to 64 steps per lane (4 pages). Per step: velocity, chance, ratchet (1-8, flat/up/down), condition (Fill, Not fill, First, Not first, 1:2 ... 4:4), nudge. Per lane: length and speed (polymeter, x1/4 to x4), mute, an algorithm (Euclid, Random, Groove, Rolls, Mutate, Humanize) with its own die, and Evolve (the lane varies a little every loop). Per track: Fill latch and Auto fill every 2/4/8/16 bars. Click to toggle, drag to paint, Shift-drag for velocity, right-click a step for everything else
- **Roll the Dice** is now per track, on the Sequencer panel, and only rolls that track's sequencer (pattern settings, or in step mode the lanes); right-click for chaos and undo
- **Warp** (Slice Sampler): dropped audio is tempo-detected and time-stretched, pitch unchanged, to the session tempo; follows tempo changes; Warp switch; editable BPM with /2 and x2. Slices stay on the music
- Random slicing spreads slices evenly across the whole file; up to 256 slices as before
- Generative patterns are 1-64 steps
- Fixed: reverbs could drone forever on the left side (Shimmer feedback leaking into every reverb, unbalanced injection); synth release tails after switching engine played left only
- App icon: BASH!

**Final UI pass** ✅

- Charcoal and amber, after the reference in `design/`: thick amber arc knobs with amber values, amber-on-dark menus, solid amber selection, dim amber steps and pads, locked steps outlined in amber, the die in the top-right corner

## Getting builds

**On your Mac (recommended):** install CMake (`brew install cmake`), then in the repo folder run

```
./scripts/build-mac.sh
```

It builds VST3 + AU + the standalone app, runs all the tests, makes `BASH-mac.dmg` (containing **Install BASH!.pkg**, which installs the app into Applications and the VST3/AU plugins into /Library/Audio/Plug-Ins) and installs everything for you.

**iPad:** `./scripts/build-ipad.sh YOURTEAMID` makes the Xcode project and opens it; pick your iPad and press Run. You get the standalone app plus the AUv3 for AUM, Logic for iPad, Cubasis and others. On iPad the MIDI/WAV chips open the share sheet instead of dragging.

**GitHub Actions:** every push runs the tests (Linux) and builds Windows (download **BASH-windows** from the run). Mac (BASH-mac: DMG with the installer) and iPad builds use 10x-billed macOS minutes on a private repo, so they run only for version tags (`v0.1.0` also publishes a Release) or when you start them by hand: Actions → Build → Run workflow.

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
