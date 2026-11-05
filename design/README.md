# BASH! UI direction

Reference: `ui-reference.png` (Joe, 5 Nov 2026). Applies to the final GUI pass.

- Dark charcoal background (~#232323), panels a shade lighter, one accent: warm amber/yellow (~#FFB800). Dimmed amber (~#7A5C10) for secondary/inactive states. No other hues.
- Big, thick arc knobs (amber value arc over a dark track, no pointer); values in amber below, labels in grey above.
- Small parameters as minimal glyph controls (dot clusters) with large numeric readouts.
- Thin-line amber icons in the header (mode, root keyboard, scale, transpose, view switches, settings).
- **No smiley** (Joe, 5 Nov). Roll the Dice is a dice icon button in the top corner of the header.
- Modulation matrix as a grid: rows = destinations, columns = sources (LFO 1, LFO 2, aftertouch, velocity), amounts as small amber bars.
- LFO panels draw their waveform as a single amber line with the current position as a dot; vertical sliders beside them.
- Pattern lanes (Note / Accent / Gate / Oct) as a dark piano-roll with rounded amber pills; muted pills dimmed, locked pills outlined.
- Track / pattern selection as a row of 8 tabs plus a 2×8 pad grid; active pad filled amber, used pads dim amber, empty pads outlined.
- Clean modern sans (Manrope / Inter-like), generous spacing, everything rounded.

Implemented (Nov 2026): palette roles in plugin/UiCommon.h (background, panel, raised, line, ink, quiet, accent, accentDim, active) and the look-and-feel in plugin/PluginEditor.cpp. Change the palette there to re-theme everything.
