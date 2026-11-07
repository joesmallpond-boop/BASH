# BASH!

A one-channel groovebox for Ableton Live, as a Max for Live instrument: a sequencer
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

- **1.0.2** - LFO 1 and 2 can MAP to any parameter in Live, like Live's LFO: press Map, click a parameter (any device, any track), the LFO drives it between Min and Max; x unmaps; the target shows in the LFO display and is saved with the set.
- **1.0.1** - Looks like Live's own devices: Live's knobs, menus and buttons in your Live theme, displays in Live's screen colours (they follow the theme), grouped sections; EXPAND opens a big window (all 16 step lanes, a large waveform, the pads, keys and LFOs), like EQ Eight; the Decompression Tapes mark as the play light, the lane arrows and a quiet signature in the big view.
- **1.0.0** - BASH! becomes a one-channel Max for Live instrument. The 8-track JUCE plugin
  (mixer, effects, MICRO) is retired; its sequencers, voices, library, grooves, warp and
  modulation carry over unchanged. Tempo and key come straight from Live.
- 0.1 - 0.5.2 - the VST3 / AU / standalone plugin (still on the Releases page).
