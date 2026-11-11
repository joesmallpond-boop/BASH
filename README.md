# BASH!

By Kalabash. Three Max for Live devices: **BASH!** (the whole thing), **BASH! Voice** (the
synth / slicer / drums on their own) and **BASH! Sequencer** (the sequencers as a MIDI effect for
any instrument: melodic notes, slices from C1 for Simpler / Sampler, or Drum Rack pads from C1).

BASH! is a one-channel groovebox for Ableton Live: a sequencer
(the generative acid sequencer or a step sequencer) driving one voice (a synth, a slice
sampler or a 16-pad drum kit), with LFO / Life modulation, locked to Live's tempo and key.
Voiced for UK bass, drum & bass, dubstep, UKG and broken / wonky beats.

**Download:** the latest Mac installer is on the [Releases](https://github.com/joesmallpond-boop/BASH/releases) page. See `packaging/INSTALL-mac.txt`.

## The device

| Area | What's there |
| --- | --- |
| Badge | BASH! bouncing to the beat and flaring on hits, Live's key, PLAY / SYNC / TEMPO, LIVE KEY, DICE + UNDO, THRU, SEQ ON |
| Sequencer | GENERATIVE: algorithm (Core Acid V1/V2, Counterpoint, Arpeggiator, Dedicated Bass...), Type, Density, Steps, Offset, Accent, Octave, Jitter, Direction; click a step to mute it, shift-click to lock it. STEP: 16 lanes (8 shown, the selected lane's page), per-step velocity (drag), ratchets (shift), conditions (alt), probability (cmd); per lane length, speed, algorithm (Euclid, Random, Groove, Rolls, Mutate, Humanize), evolve, mute, die; grooves menu. Both: Rate, Gate, Swing, Fill, Auto fill, Scale lock |
| Synth | Machine, filter type, Character, Timbre, Cutoff, Resonance, Env mod, Decay, Drive, Glide, amp ADSR; a keyboard that lights up and blows bubbles |
| Slicer | Drop / load a sample or pick a library loop; warp to Live's tempo (sample BPM found from the name or by listening, halve / double / drag to fix); transients / grid / random slicing (up to 256), drag / add / delete slices, click to audition; slice mode, window, tune, attack, release, reverse, pitch follow |
| Drums | 4x4 pads with ripples on every hit; library kits (UK bass / DnB / dubstep / UKG...) and FM kits; a sound per pad from the library or FM presets, or drop your own; per-pad tune, gain, pan, decay, start, end, cutoff, choke, reverse |
| Mod | LFO 1 / 2 (shape, rate or sync division, MAP to any Live parameter with Min / Max), Life, six slots: source (LFO 1/2, Life, velocity, aftertouch, mod wheel, pitch bend) to destination (sequencer and voice) with an amount |

Every control is a Live parameter. Pattern, steps, slices, sample paths and the kit are
saved in the Live set with the device.

## How it's built

- `core/` - plain C++17: the engine (sequencers, synth, slicer, drums, modulation), the
  factory library (all sounds synthesised in code), and `Instrument`, which is everything the
  device does (parameters, samples, kit, step and slice editing, dice, saved state, display data).
- `max/bash~/` - the Max external: a thin wrapper that connects `Instrument` to Max and Live's transport.
- `max/make_device.py` - generates the device (`BASH!.amxd`) from the core's parameter table.
- `max/javascript/` - the displays (jsui): badge, sequencer, lane strip, waveform, pads, LFOs, keys.
- `max/check_device.py`, `max/check_es5.js`, `max/preview.js` - checks and a preview renderer,
  run in CI on every push (no Max needed).
- `tests/` - core unit tests.

Build on a Mac: `./scripts/build-mac.sh` (add `--install` to install it for you).

## History

- **1.4.2** - If Live is still running an older BASH! engine after an update, the device says so (quit and reopen Live).
- **1.4.1** - Picking a poly voice switches the sequencer to Chords (Jazz and Strum appear) and gives the voice a patch made for chords, and picking Chords picks a poly voice; the poly voices sit lower so full chords don't clip.
- **1.4.0** - Poly voices (Poly Saw, Pad, Keys, Organ; 8 voices); the Chords algorithm: progressions in key, with Jazz (plain triads up to rootless 9ths / 11ths / 13ths, sus and altered tones) and Strum; LOOP OUT up to 64 bars; a duplicated device comes up exactly like its original.
- **1.3.0** - Fills that work in every mode: FILL plays a random fill while it's on; "every" puts one at the end of every 1-16 bars; "length" sets how long (1 beat to 2 bars); Same / Vary picks the same fill each time or a new one. Drum fills roll the snare and run down the toms, slicer fills stutter, synth fills run up the key.
- **1.2.6** - GRAB CLIP: click an audio clip in Live, then GRAB CLIP to load it into the slicer (with its tempo, if warped) or the selected drum pad. The drop zones are proper Live drop targets.
- **1.2.5** - Warp on in the starting patch too.
- **1.2.4** - A freshly added device always starts with Sync and Seq On on, and the slicer's Pitch and Reverse off.
- **1.2.3** - Sessions reopen as they were saved: the saved state is kept in shorter chunks, nothing is saved until the set's own state has loaded, and every device also keeps a backup copy (by a hidden State ID) that brings it back if Live doesn't. Live's tempo shows in the Tempo box while synced.
- **1.2.2** - Click the BASH! badge ("CLICK TO GENERATE") for a new pattern with the same sound; DICE now randomises everything (pattern, sequencer, synth, slicer, kit, LFOs, mod slots), and UNDO brings all of it back.
- **1.2.1** - Drums: each pad switches between SAMPLE and FM (FM picks a matching starting point: kick, snare, hats, toms, cymbals, clap, perc...), with its own FM controls (Pitch, Sweep, FM, Ratio, Noise, Tone, Body, Click, Drive); the pad knobs now show their names and values and work.
- **1.2.0** - Follows Live's tempo and play / stop (via the Live API, with Max's transport as backup); LOOP OUT puts the loop in a MIDI clip or a WAV on a new audio track (files also in User Library > Kalabash > Captures); switching Synth / Slicer / Drums cuts the old voice dead; wider voice section; Expand opens reliably.
- **1.1.1** - Installs as one Kalabash folder in the User Library (devices + bash~ + display scripts together), so the displays and page switching load; the DMG also has that folder to drag in by hand.
- **1.1.0** - Three devices in one installer, under Kalabash: BASH!, BASH! Voice (instrument) and BASH! Sequencer (MIDI effect: Notes / Slices / Drum Rack targets). Scale button like Live's scale awareness: follows the set's key and Live's Scale on / off.
- **1.0.2** - LFO 1 and 2 can MAP to any parameter in Live, like Live's LFO: press Map, click a parameter (any device, any track), the LFO drives it between Min and Max; x unmaps; the target shows in the LFO display and is saved with the set.
- **1.0.1** - Looks like Live's own devices: Live's knobs, menus and buttons in your Live theme, displays in Live's screen colours (they follow the theme), grouped sections; EXPAND opens a big window (all 16 step lanes, a large waveform, the pads, keys and LFOs), like EQ Eight; the Decompression Tapes mark as the play light, the lane arrows and a quiet signature in the big view.
- **1.0.0** - BASH! becomes a one-channel Max for Live instrument. The 8-track JUCE plugin
  (mixer, effects, MICRO) is retired; its sequencers, voices, library, grooves, warp and
  modulation carry over unchanged. Tempo and key come straight from Live.
- 0.1 - 0.5.2 - the VST3 / AU / standalone plugin (still on the Releases page).
