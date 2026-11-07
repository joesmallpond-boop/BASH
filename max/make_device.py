#!/usr/bin/env python3
"""Builds BASH!.amxd, the Max for Live instrument, from BASH!'s parameter table.

    python3 max/make_device.py <params.json> <out dir>

params.json comes from the bash_params_json tool (the core's parameter list), so every
live.* control here matches the instrument. The device needs bash~ (the external) and
the bash_*.js display scripts on Max's search path; the installer puts them in a BASH
package in Max's Packages folder.

Layout (169 px tall, like every Live device):
  left    the BASH! badge, Play / Sync / Tempo, Live key, the dice
  middle  the sequencer: Generative or Step, its display, and its controls
  right   the voice: Synth / Slicer / Drums, or the MOD page (LFOs, Life, 6 mod slots)
  far right  Level, Pan and the output meter
"""
import json, os, struct, sys

PARAMS = json.load(open(sys.argv[1]))
OUT = sys.argv[2] if len(sys.argv) > 2 else "."
# full = BASH! (sequencer + voice), voice = BASH! Voice (instrument), seq = BASH! Sequencer (MIDI effect)
VARIANT = sys.argv[3] if len(sys.argv) > 3 else "full"
NAMES = {"full": "BASH!", "voice": "BASH! Voice", "seq": "BASH! Sequencer"}
CUR = ["core"]      # which part of the layout boxes belong to (the variants drop parts)
P = {p["id"]: p for p in PARAMS}

W, H = 1198, 169
AMBER = [1.0, 0.722, 0.0, 1.0]
AMBER_DIM = [0.541, 0.396, 0.031, 1.0]
ACTIVE = [0.29, 0.224, 0.063, 1.0]
BG = [0.114, 0.114, 0.114, 1.0]
PANEL = [0.145, 0.145, 0.145, 1.0]
RAISED = [0.18, 0.18, 0.18, 1.0]
INK = [0.914, 0.902, 0.875, 1.0]
QUIET = [0.545, 0.541, 0.525, 1.0]

boxes, lines = [], []
_n = [0]
groups = {"gen": [], "step": [], "synth": [], "slice": [], "drum": [], "padsample": [], "padfm": [], "mod": [], "free": [], "synced": []}
param_objects = []        # (box id, param id) for every live.* parameter

def new_id():
    _n[0] += 1
    return "obj-%d" % _n[0]

# patching positions: a simple flow so the patch is readable when opened in Max
_cursor = {"x": 20, "y": 700}
def ppos(w=60, h=22):
    x, y = _cursor["x"], _cursor["y"]
    _cursor["x"] += max(w, 60) + 20
    if _cursor["x"] > 2400:
        _cursor["x"] = 20
        _cursor["y"] += 40
    return [x, y, w, h]

def add(box, group=None):
    box["_sec"] = CUR[0]
    boxes.append({"box": box})
    if group:
        box.setdefault("varname", box["id"])
        groups[group].append(box["varname"])
    return box["id"]

def obj(text, ins, outs, outtypes=None, **extra):
    b = {"id": new_id(), "maxclass": "newobj", "text": text, "numinlets": ins, "numoutlets": outs,
         "patching_rect": ppos(max(60, 7 * len(text) + 16))}
    if outs:
        b["outlettype"] = outtypes or [""] * outs
    b.update(extra)
    return add(b)

def msg(text):
    b = {"id": new_id(), "maxclass": "message", "text": text, "numinlets": 2, "numoutlets": 1,
         "outlettype": [""], "patching_rect": ppos(max(50, 7 * len(text) + 16))}
    return add(b)

def wire(src, so, dst, di):
    lines.append({"patchline": {"source": [src, so], "destination": [dst, di]}})

def pres(b, rect):
    b["presentation"] = 1
    b["presentation_rect"] = rect
    b["patching_rect"] = ppos(rect[2], rect[3])
    return b

def comment(text, rect, size=9.0, color=None, group=None, bold=False):
    b = {"id": new_id(), "maxclass": "comment", "text": text, "numinlets": 1, "numoutlets": 0, "fontsize": size}
    if color: b["textcolor"] = color
    if bold: b["fontface"] = 1
    pres(b, rect)
    return add(b, group)

def panel(rect, color, group=None, rounded=6):
    b = {"id": new_id(), "maxclass": "panel", "numinlets": 1, "numoutlets": 0, "bgcolor": color,
         "border": 0, "rounded": rounded, "mode": 0, "background": 1}
    pres(b, rect)
    return add(b, group)

# ---- live.* parameters ---------------------------------------------------------------------
UNITSTYLE = {"Hz": 3, "s": 2, "st": 7, "bpm": 1, "": 1}

def valueof(p):
    v = {"parameter_longname": p["name"], "parameter_shortname": p["short"],
         "parameter_mmin": p["min"], "parameter_mmax": p["max"],
         "parameter_initial": [p["def"]], "parameter_initial_enable": 1}
    kind = p["kind"]
    if kind == "float":
        v["parameter_type"] = 0
        v["parameter_unitstyle"] = UNITSTYLE.get(p["unit"], 1)
        if p["unit"] == "s":
            v["parameter_unitstyle"] = 9          # custom: the values are seconds (Live's Time style means ms)
            v["parameter_units"] = "%0.3f s"
        if abs(p["exp"] - 1.0) > 1e-3:
            v["parameter_exponent"] = round(p["exp"], 4)
    elif kind == "int":
        v["parameter_type"] = 1
        v["parameter_unitstyle"] = 0
    else:
        v["parameter_type"] = 2
        v["parameter_enum"] = p["choices"] if kind == "enum" else ["off", "on"]
        v["parameter_mmax"] = len(v["parameter_enum"]) - 1
    return v

def live_param(cls, pid, rect, group=None, **extra):
    p = P[pid]
    outs = {"live.dial": (2, ["", "float"]), "live.menu": (3, ["", "", "float"]), "live.tab": (3, ["", "", "float"]),
            "live.text": (2, ["", ""]), "live.numbox": (2, ["", "float"])}[cls]
    b = {"id": new_id(), "maxclass": cls, "numinlets": 1, "numoutlets": outs[0], "outlettype": outs[1],
         "parameter_enable": 1, "varname": pid,
         "saved_attribute_attributes": {"valueof": valueof(p)}}
    # no colours of our own: Live's controls follow Live's theme, like the stock devices
    if cls == "live.dial":
        b.update({"showname": 1, "shownumber": 1})
    if cls == "live.tab":
        b.update({"num_lines_patching": 1, "num_lines_presentation": 1})
    b.update(extra)
    pres(b, rect)
    oid = add(b, group)
    param_objects.append((oid, pid))
    return oid

def toggle(pid, label, rect, group=None):
    return live_param("live.text", pid, rect, group, mode=1, text=label, texton=label)

def jsui(filename, rect, group=None, args=None):
    b = {"id": new_id(), "maxclass": "jsui", "filename": filename, "numinlets": 1, "numoutlets": 1,
         "outlettype": [""], "parameter_enable": 0, "border": 0}
    if args:
        b["jsarguments"] = args
    pres(b, rect)
    return add(b, group)

def umenu(rect, group=None):
    b = {"id": new_id(), "maxclass": "umenu", "numinlets": 1, "numoutlets": 3, "outlettype": ["int", "", ""],
         "parameter_enable": 0, "fontsize": 9.0, "arrow": 1, "items": ["(loading)"]}
    pres(b, rect)
    return add(b, group)

def drop(rect, label, group=None):
    b = {"id": new_id(), "maxclass": "live.drop", "numinlets": 1, "numoutlets": 2, "outlettype": ["", ""],
         "parameter_enable": 0, "legend": label}
    pres(b, rect)
    return add(b, group)

def plain_dial(label, lo, hi, init, rect, group=None, exp=1.0):
    """A live.dial that is not a Live parameter (per-pad settings live in BASH!'s own state)."""
    b = {"id": new_id(), "maxclass": "live.dial", "numinlets": 1, "numoutlets": 2, "outlettype": ["", "float"],
         "parameter_enable": 0, "showname": 1, "shownumber": 1, "varname": "pad_" + label.lower(),
         "saved_attribute_attributes": {"valueof": {"parameter_longname": "Pad " + label, "parameter_shortname": label,
                                                    "parameter_type": 0, "parameter_mmin": lo, "parameter_mmax": hi,
                                                    "parameter_initial": [init], "parameter_unitstyle": 1,
                                                    "parameter_exponent": exp}}}
    pres(b, rect)
    return add(b, group)

# =====================================================================================
# the engine
bash = obj("bash~", 1, 5, ["signal", "signal", "signal", "signal", ""])
MSG = 4   # bash~ outlets: L, R, LFO 1 map, LFO 2 map, messages
plugout = obj("plugout~ 1 2", 2, 2, ["signal", "signal"])
wire(bash, 0, plugout, 0)
wire(bash, 1, plugout, 1)

# sections, like Live's own devices: a faint shade over Live's device colour (works in any theme)
SECTION = [0.0, 0.0, 0.0, 0.07]
FRAMES = {"full": [[1, 1, 110, 167], [115, 1, 472, 167], [590, 1, 512, 167], [1105, 1, 92, 167]],
          "voice": [[1, 1, 110, 167], [115, 1, 512, 167], [630, 1, 92, 167]],
          "seq": [[1, 1, 110, 167], [115, 1, 472, 167], [590, 1, 512, 167]]}
for rect in FRAMES[VARIANT]:
    panel(rect, SECTION, rounded=4)

def button(label, rect, message, group=None):
    """A Live button (not a parameter) that sends `message` to bash~ when clicked."""
    b = {"id": new_id(), "maxclass": "live.text", "numinlets": 1, "numoutlets": 2, "outlettype": ["", ""],
         "parameter_enable": 0, "mode": 0, "text": label, "texton": label, "varname": "btn_%s_%d" % (label.lower().replace(" ", "_"), _n[0] + 1)}
    pres(b, rect)
    oid = add(b, group)
    sel = obj("route bang 1", 1, 3)
    m = msg(message)
    wire(oid, 0, sel, 0)
    wire(sel, 0, m, 0)
    wire(sel, 1, m, 0)
    return oid, m

jsuis = []   # every display gets everything bash~ sends

# ---- column A: badge, clock, key, dice ---------------------------------------------------
CUR[0] = "key"
jsuis.append(jsui("bash_badge.js", [4, 4, 104, 62]))
# Scale: like Live's scale awareness, follow the set's key and scale (and Live's Scale on / off)
toggle("keylock", "Scale", [4, 70, 50, 16])
live_param("live.menu", "root", [4, 110, 36, 16])
live_param("live.menu", "scale", [42, 110, 66, 16])
CUR[0] = "clock"
toggle("sync", "Sync", [58, 70, 50, 16])
toggle("play", "Play", [4, 90, 50, 16])
live_param("live.numbox", "tempo", [58, 90, 50, 16], "free")
live_tempo = {"id": new_id(), "maxclass": "live.numbox", "numinlets": 1, "numoutlets": 2, "outlettype": ["", "float"],
              "parameter_enable": 1, "varname": "livetempo", "ignoreclick": 1,
              "saved_attribute_attributes": {"valueof": {"parameter_longname": "Live Tempo Display", "parameter_shortname": "Live tempo",
                                                         "parameter_type": 0, "parameter_mmin": 20.0, "parameter_mmax": 999.0,
                                                         "parameter_initial": [120.0], "parameter_unitstyle": 1, "parameter_invisible": 2}}}
pres(live_tempo, [58, 90, 50, 16])
live_tempo_id = add(live_tempo, "synced")
dice = jsui("bash_btns.js", [4, 130, 104, 18], args=["DICE|dice", "UNDO|undo"])
jsuis.append(dice)
toggle("thru", "Thru", [4, 151, 50, 15])
toggle("seqon", "Seq on", [58, 151, 50, 15])

# ---- column B: the sequencer --------------------------------------------------------------
CUR[0] = "seq"
live_param("live.tab", "mode", [118, 4, 116, 16])
live_param("live.menu", "rate", [238, 4, 44, 16])
live_param("live.menu", "algo", [286, 4, 108, 16], "gen")
groove_menu = umenu([286, 4, 108, 16], "step")
toggle("fill", "FILL", [398, 4, 32, 16])
live_param("live.menu", "autofill", [434, 4, 54, 16])
actions = []
actions.append(button("New", [492, 4, 44, 16], "regen", "gen"))
actions.append(button("Unlock", [540, 4, 44, 16], "unlockall", "gen"))
actions.append(button("Clear all", [492, 4, 92, 16], "steps clear", "step"))
jsuis.append(jsui("bash_seq.js", [118, 24, 466, 84]))
for i, pid in enumerate(["type", "density", "steps", "offset", "accent", "octave", "jitter"]):
    live_param("live.dial", pid, [118 + i * 46, 112, 44, 50], "gen")
jsuis.append(jsui("bash_lane.js", [118, 112, 318, 54], "step"))
live_param("live.dial", "gate", [440, 112, 44, 50])
live_param("live.dial", "swing", [486, 112, 44, 50])
live_param("live.menu", "direction", [534, 114, 50, 16], "gen")
toggle("scalelock", "In key", [534, 134, 50, 16])

# ---- column C: the voice ------------------------------------------------------------------
CUR[0] = "engine"
if VARIANT == "seq":
    # the Sequencer's Target: what its notes are for
    eng_tab = live_param("live.tab", "engine", [592, 4, 220, 16])
    boxes[-1]["box"]["saved_attribute_attributes"]["valueof"]["parameter_enum"] = ["Notes", "Slices", "Drum Rack"]
    comment("Slices", [816, 5, 36, 14])
    live_param("live.numbox", "midislices", [852, 4, 40, 16])
else:
    live_param("live.tab", "engine", [592, 4, 150, 16])
CUR[0] = "voice"
page_tab = {"id": new_id(), "maxclass": "live.tab", "numinlets": 1, "numoutlets": 3, "outlettype": ["", "", "float"],
            "parameter_enable": 1, "varname": "view", "num_lines_patching": 1, "num_lines_presentation": 1,
            "saved_attribute_attributes": {"valueof": {"parameter_longname": "View", "parameter_shortname": "View",
                                                       "parameter_type": 2, "parameter_enum": ["VOICE", "MOD"],
                                                       "parameter_mmax": 1, "parameter_initial": [0],
                                                       "parameter_initial_enable": 1, "parameter_invisible": 1}}}
pres(page_tab, [746, 4, 88, 16])
CUR[0] = "page"
page = add(page_tab)
CUR[0] = "voice"

# synth
live_param("live.menu", "machine", [838, 4, 110, 16], "synth")
live_param("live.menu", "ftype", [992, 4, 108, 16], "synth")
for i, pid in enumerate(["wave", "timbre", "cutoff", "reso", "envmod", "decay", "drive", "glide"]):
    live_param("live.dial", pid, [592 + i * 46, 26, 44, 50], "synth")
for i, pid in enumerate(["aatk", "adec", "asus", "arel"]):
    live_param("live.dial", pid, [592 + i * 46, 86, 44, 50], "synth")
jsuis.append(jsui("bash_keys.js", [778, 82, 322, 84], "synth"))
comment("AMP", [592, 140, 60, 14], group="synth")

# slicer
loop_menu = umenu([838, 4, 262, 16], "slice")
wave = jsui("bash_wave.js", [592, 24, 340, 90], "slice")
jsuis.append(wave)
jsuis.append(jsui("bash_tempo.js", [936, 24, 82, 44], "slice"))
slice_drop = drop([1022, 24, 78, 44], "DROP SAMPLE", "slice")
toggle("warp", "WARP", [936, 72, 52, 16], "slice")
toggle("reverse", "REVERSE", [992, 72, 52, 16], "slice")
toggle("pitchfollow", "PITCH", [1048, 72, 52, 16], "slice")
live_param("live.menu", "slicemode", [936, 96, 100, 16], "slice")
for i, pid in enumerate(["winstart", "winsize", "tune", "sattack", "srelease"]):
    live_param("live.dial", pid, [592 + i * 46, 116, 44, 50], "slice")
comment("Slice mode: how the sequencer moves through the slices", [830, 118, 268, 30], 7.5, group="slice")

# drums: each pad is a sample or an FM drum (SAMPLE / FM), with its own controls
kit_menu = umenu([838, 4, 262, 16], "drum")
jsuis.append(jsui("bash_pads.js", [592, 24, 142, 142], "drum"))

def pad_param(cls, name, short, rect, group, extra_valueof, **extra):
    """A per-pad control: a Live parameter so it draws its name and value, but Hidden (not stored or
    automated) because it edits whichever pad is selected; the pad settings live in BASH!'s own state."""
    outs = {"live.dial": (2, ["", "float"]), "live.tab": (3, ["", "", "float"])}[cls]
    v = {"parameter_longname": name, "parameter_shortname": short, "parameter_invisible": 2}
    v.update(extra_valueof)
    b = {"id": new_id(), "maxclass": cls, "numinlets": 1, "numoutlets": outs[0], "outlettype": outs[1],
         "parameter_enable": 1, "varname": "pad_" + short.lower().replace(" ", "_"),
         "saved_attribute_attributes": {"valueof": v}}
    b.update(extra)
    pres(b, rect)
    return add(b, group)

pad_kind = pad_param("live.tab", "Pad Source", "Source", [738, 24, 84, 16], "drum",
                     {"parameter_type": 2, "parameter_enum": ["SAMPLE", "FM"], "parameter_mmax": 1},
                     num_lines_patching=1, num_lines_presentation=1)
sound_menu = umenu([826, 24, 274, 16], "padsample")
fm_menu = umenu([826, 24, 274, 16], "padfm")
pad_drop = drop([738, 44, 362, 18], "DROP A SAMPLE ON THE SELECTED PAD", "padsample")
comment("FM drum: pick a starting point above, then shape it", [738, 46, 362, 14], 8.0, group="padfm")

def pad_dial(field, label, lo, hi, init, x, group, exp=1.0, unit=""):
    vo = {"parameter_type": 0, "parameter_mmin": lo, "parameter_mmax": hi, "parameter_initial": [init],
          "parameter_unitstyle": 1, "parameter_exponent": exp}
    if unit:
        vo["parameter_unitstyle"] = 9
        vo["parameter_units"] = unit
    return pad_param("live.dial", ("Pad FM " if group == "padfm" else "Pad ") + label.title(), label.title(),
                     [x, 66, 40, 50], group, vo, showname=1, shownumber=1)

# sample pads (padedit order: tune gain pan decay start end cutoff choke)
pad_fields = [("tune", "TUNE", -24, 24, 0, 1.0, "%0.1f st"), ("gain", "GAIN", 0, 2, 1, 1.0, ""), ("pan", "PAN", -1, 1, 0, 1.0, ""),
              ("decay", "DECAY", 0.01, 4, 0.4, 2.0, "%0.2f s"), ("start", "START", 0, 1, 0, 1.0, ""), ("end", "END", 0, 1, 1, 1.0, ""),
              ("cutoff", "CUTOFF", 40, 20000, 20000, 3.0, "%0.0f Hz"), ("choke", "CHOKE", 0, 8, 0, 1.0, "%0.0f")]
pad_dials = {}
for i, (field, label, lo, hi, init, exp, unit) in enumerate(pad_fields):
    pad_dials[field] = pad_dial(field, label, lo, hi, init, 738 + i * 40, "padsample", exp, unit)
# FM pads (padedit order after the sample fields: pitch sweep fm ratio noise tone decay click drive)
fm_fields = [("pitch", "PITCH", 20, 2000, 55, 3.0, "%0.0f Hz"), ("sweep", "SWEEP", 0, 48, 24, 1.0, "%0.0f st"),
             ("fm", "FM", 0, 20, 0, 2.0, ""), ("ratio", "RATIO", 0.25, 8, 1, 2.0, ""), ("noise", "NOISE", 0, 1, 0, 1.0, ""),
             ("tone", "TONE", 0, 1, 0.5, 1.0, ""), ("decay", "BODY", 0.01, 4, 0.4, 2.0, "%0.2f s"),
             ("click", "CLICK", 0, 1, 0.3, 1.0, ""), ("drive", "DRIVE", 0, 1, 0.2, 1.0, "")]
fm_dials = {}
for i, (field, label, lo, hi, init, exp, unit) in enumerate(fm_fields):
    fm_dials[field] = pad_dial(field, label, lo, hi, init, 738 + i * 40, "padfm", exp, unit)
actions.append(button("Play pad", [738, 122, 82, 18], "pad play -1", "drum"))
actions.append(button("Clear pad", [824, 122, 84, 18], "pad clear -1", "drum"))
pad_rev = {"id": new_id(), "maxclass": "live.text", "numinlets": 1, "numoutlets": 2, "outlettype": ["", ""],
           "parameter_enable": 0, "mode": 1, "text": "REVERSE", "texton": "REVERSE", "varname": "pad_reverse"}
pres(pad_rev, [912, 122, 70, 18])
pad_rev_id = add(pad_rev, "padsample")
comment("Click a pad to play it, shift-click to select, alt-click to clear.", [738, 144, 362, 22], 8.0, group="drum")

CUR[0] = "mod"
# mod page: two LFOs (each can MAP to any parameter in Live, like Live's LFO), Life, six slots
map_buttons = {}
for k, y in ((1, 24), (2, 82)):
    jsuis.append(jsui("bash_lfo.js", [592, y, 80, 52], "mod", args=[k]))
    live_param("live.dial", "lfo%drate" % k, [674, y, 44, 50], "mod")
    live_param("live.menu", "lfo%dshape" % k, [720, y + 2, 70, 16], "mod")
    live_param("live.menu", "lfo%ddiv" % k, [720, y + 20, 70, 16], "mod")
    toggle("lfo%dsync" % k, "Sync", [720, y + 38, 70, 14], "mod")
    mb = {"id": new_id(), "maxclass": "live.text", "numinlets": 1, "numoutlets": 2, "outlettype": ["", ""],
          "parameter_enable": 0, "mode": 1, "text": "Map", "texton": "Map", "varname": "map%d" % k}
    pres(mb, [794, y + 2, 48, 16])
    mbid = add(mb, "mod")
    unmap, unmap_msg = button("x", [844, y + 2, 20, 16], "mapclear %d" % k, "mod")
    live_param("live.numbox", "lfo%dmin" % k, [794, y + 20, 34, 16], "mod")
    live_param("live.numbox", "lfo%dmax" % k, [830, y + 20, 34, 16], "mod")
    comment("Min   Max", [796, y + 36, 70, 14], 8.0, group="mod")
    map_buttons[k] = (mbid, unmap_msg)
    actions.append((unmap, unmap_msg))
jsuis.append(jsui("bash_lfo.js", [592, 140, 80, 26], "mod", args=[3]))
comment("Life", [674, 142, 30, 14], group="mod")
live_param("live.numbox", "liferate", [702, 142, 88, 16], "mod")
comment("Source", [868, 24, 82, 14], group="mod")
comment("Destination", [952, 24, 100, 14], group="mod")
comment("Amount", [1054, 24, 46, 14], group="mod")
for k in range(1, 7):
    y = 40 + (k - 1) * 21
    live_param("live.menu", "m%dsrc" % k, [868, y, 82, 16], "mod")
    live_param("live.menu", "m%ddst" % k, [952, y, 100, 16], "mod")
    live_param("live.numbox", "m%damt" % k, [1054, y, 46, 16], "mod")

# ---- column D: level, pan, meter -------------------------------------------------------------
CUR[0] = "out"
live_param("live.dial", "level", [1108, 22, 44, 50])
live_param("live.dial", "pan", [1152, 22, 44, 50])
meter = {"id": new_id(), "maxclass": "live.meter~", "numinlets": 1, "numoutlets": 1, "outlettype": ["list"],
         "orientation": 1}
pres(meter, [1110, 76, 84, 8])
meter_id = add(meter)
wire(bash, 0, meter_id, 0)

for _, m in actions:
    wire(m, 0, bash, 0)

# ---- LOOP OUT: the loop as a MIDI clip / a WAV on a new audio track (+ files in User Library > Kalabash > Captures) --
CUR[0] = "export"
comment("LOOP OUT", [1108, 88, 88, 12], 8.0)
bars_menu = umenu([1108, 102, 88, 16])
boxes[-1]["box"]["items"] = ["1 bar", ",", "2 bars", ",", "4 bars", ",", "8 bars", ",", "16 bars"]
bars_calc = obj("expr 1 << $i1", 1, 1)
wire(bars_menu, 0, bars_calc, 0)
midi_btn, midi_msg = button("MIDI", [1108, 122, 42, 18], "bang")
midi_bars = obj("i 4", 2, 1, ["int"])
wire(bars_calc, 0, midi_bars, 1)
wire(midi_msg, 0, midi_bars, 0)
midi_p = obj("prepend export midi", 2, 1)
wire(midi_bars, 0, midi_p, 0)
wire(midi_p, 0, bash, 0)
CUR[0] = "exportwav"
wav_btn, wav_msg = button("WAV", [1152, 122, 44, 18], "bang")
wav_bars = obj("i 4", 2, 1, ["int"])
wire(bars_calc, 0, wav_bars, 1)
wire(wav_msg, 0, wav_bars, 0)
wav_p = obj("prepend export wav", 2, 1)
wire(wav_bars, 0, wav_p, 0)
wire(wav_p, 0, bash, 0)
comment("into a clip + Captures", [1108, 144, 88, 20], 7.0)
CUR[0] = "export"
export_js = obj("js bash_export.js", 1, 1)
wire(bash, MSG, export_js, 0)
wire(export_js, 0, bash, 0)
CUR[0] = "expand"

# ---- the expanded window (like EQ Eight's): EXPAND opens a big view in its own window ------------
expand_btn, expand_msg = button("Expand", [1108, 4, 88, 16], "open")
expand_front = msg("vis, front")
wire(expand_msg, 0, expand_front, 0)
pctl = obj("pcontrol", 1, 1)
wire(expand_msg, 0, pctl, 0)
BIG_W, BIG_H = 1200, 660
big_boxes, big_lines = [], []
_bn = [0]
def big_add(box):
    _bn[0] += 1
    box["id"] = "big-%d" % _bn[0]
    big_boxes.append({"box": box})
    return box["id"]
def big_jsui(filename, rect, args=None):
    b = {"maxclass": "jsui", "filename": filename, "numinlets": 1, "numoutlets": 1, "outlettype": [""],
         "parameter_enable": 0, "border": 0, "patching_rect": [rect[0], rect[1] + 60, rect[2], rect[3]],
         "presentation": 1, "presentation_rect": rect}
    if args: b["jsarguments"] = args
    return big_add(b)
big_in = big_add({"maxclass": "inlet", "numinlets": 0, "numoutlets": 1, "outlettype": [""], "patching_rect": [20, 10, 30, 30], "comment": ""})
big_tp = big_add({"maxclass": "newobj", "text": "thispatcher", "numinlets": 1, "numoutlets": 2, "outlettype": ["", ""],
                  "patching_rect": [20, 40, 80, 22]})
big_lines.append({"patchline": {"source": [big_in, 0], "destination": [big_tp, 0]}})
big_r = big_add({"maxclass": "newobj", "text": "r ---bashview", "numinlets": 0, "numoutlets": 1, "outlettype": [""], "patching_rect": [80, 10, 100, 22]})
big_s = big_add({"maxclass": "newobj", "text": "s ---bashcmd", "numinlets": 1, "numoutlets": 0, "patching_rect": [200, 10, 100, 22]})
big_bg = big_add({"maxclass": "panel", "numinlets": 1, "numoutlets": 0, "bgcolor": [0.0, 0.0, 0.0, 0.07], "rounded": 0,
                  "background": 1, "patching_rect": [0, 0, BIG_W, BIG_H], "presentation": 1, "presentation_rect": [0, 0, BIG_W, BIG_H]})
BIG = {
    "full":  [("bash_seq.js", [8, 8, 1184, 330], None), ("bash_lane.js", [8, 344, 560, 54], None),
              ("bash_wave.js", [8, 404, 560, 248], None), ("bash_pads.js", [576, 344, 308, 308], None),
              ("bash_keys.js", [892, 344, 300, 96], None), ("bash_lfo.js", [892, 446, 300, 64], [1]),
              ("bash_lfo.js", [892, 516, 300, 64], [2]), ("bash_lfo.js", [892, 586, 300, 66], [3])],
    "voice": [("bash_wave.js", [8, 8, 770, 330], None), ("bash_pads.js", [786, 8, 406, 406], None),
              ("bash_keys.js", [8, 344, 770, 160], None), ("bash_lfo.js", [8, 510, 380, 142], [1]),
              ("bash_lfo.js", [396, 510, 382, 142], [2]), ("bash_lfo.js", [786, 420, 406, 232], [3])],
    "seq":   [("bash_seq.js", [8, 8, 1184, 440], None), ("bash_lane.js", [8, 456, 760, 60], None),
              ("bash_keys.js", [8, 522, 760, 130], None), ("bash_lfo.js", [776, 456, 416, 62], [1]),
              ("bash_lfo.js", [776, 524, 416, 62], [2]), ("bash_lfo.js", [776, 592, 416, 60], [3])],
}
for f, rect, args in BIG[VARIANT]:
    j = big_jsui(f, rect, args)
    big_lines.append({"patchline": {"source": [big_r, 0], "destination": [j, 0]}})
    big_lines.append({"patchline": {"source": [j, 0], "destination": [big_s, 0]}})
big_patcher = {"fileversion": 1, "appversion": {"major": 8, "minor": 6, "revision": 0, "architecture": "x64", "modernui": 1},
               "classnamespace": "box", "rect": [80.0, 80.0, float(BIG_W), float(BIG_H)], "openinpresentation": 1,
               "default_fontsize": 10.0, "gridsize": [15.0, 15.0], "toolbarvisible": 0, "statusbarvisible": 0,
               "title": NAMES[VARIANT], "boxes": big_boxes, "lines": big_lines}
big = add({"id": new_id(), "maxclass": "newobj", "text": "p BASH_view", "numinlets": 1, "numoutlets": 0,
           "patching_rect": ppos(80), "patcher": big_patcher, "saved_object_attributes": {"globalpatchername": ""}})
wire(pctl, 0, big, 0)
wire(expand_front, 0, big, 0)
view_send = obj("s ---bashview", 1, 0)
wire(bash, MSG, view_send, 0)
view_cmd = obj("r ---bashcmd", 0, 1)
cmd_route = obj("route browse", 1, 2)
wire(view_cmd, 0, cmd_route, 0)
wire(cmd_route, 1, bash, 0)

CUR[0] = "core"
# =====================================================================================
# wiring: parameters -> bash~
for oid, pid in param_objects:
    pre = obj("prepend param " + pid, 2, 1)
    wire(oid, 0, pre, 0)
    wire(pre, 0, bash, 0)

# bash~ messages -> every display + the view switcher + routers
UI_JS = {"full": "bash_ui.js", "voice": "bash_ui_voice.js", "seq": "bash_ui_seq.js"}[VARIANT]
ui_js = obj("js " + UI_JS, 1, 1)
for j in jsuis:
    wire(bash, MSG, j, 0)
wire(bash, MSG, ui_js, 0)
for j in jsuis:
    if j == wave:
        continue
    wire(j, 0, bash, 0)

router = obj("route ui menu state padedit stateid", 1, 6)
wire(bash, MSG, router, 0)

# ui <id> <value> -> the parameter's object (it then reports back as "param", which bash~ ignores)
ids = [pid for _, pid in param_objects]
obj_for = {pid: oid for oid, pid in param_objects}
for start in range(0, len(ids), 24):
    chunk = ids[start:start + 24]
    r = obj("route " + " ".join(chunk), 1, len(chunk) + 1)
    wire(router, 0, r, 0)
    for k, pid in enumerate(chunk):
        wire(r, k, obj_for[pid], 0)

# menus
mroute = obj("route loops kits sounds grooves fmsounds", 1, 6)
wire(router, 1, mroute, 0)
for k, (menu, action) in enumerate([(loop_menu, "loop"), (kit_menu, "kit pick"), (sound_menu, "pad sound -1"), (groove_menu, "groove"), (fm_menu, "pad fm -1")]):
    trig = obj("t l b", 1, 2)
    clear = msg("clear")
    it = obj("zl.iter 1 @zlmaxsize 4096", 2, 2)
    app = obj("prepend append", 2, 1)
    wire(mroute, k, trig, 0)
    wire(trig, 1, clear, 0)
    wire(clear, 0, menu, 0)
    wire(trig, 0, it, 0)
    wire(it, 0, app, 0)
    wire(app, 0, menu, 0)
    out = obj("prepend " + action, 2, 1)
    wire(menu, 0, out, 0)
    wire(out, 0, bash, 0)

# this device's State ID: a hidden, saved parameter naming its backup state file (see Instrument)
sid = {"id": new_id(), "maxclass": "live.numbox", "numinlets": 1, "numoutlets": 2, "outlettype": ["", "float"],
       "parameter_enable": 1, "varname": "stateid", "patching_rect": ppos(60, 15),
       "saved_attribute_attributes": {"valueof": {"parameter_longname": "BASH State ID", "parameter_shortname": "StateID",
                                                  "parameter_type": 1, "parameter_mmin": 0, "parameter_mmax": 16777215,
                                                  "parameter_initial": [0], "parameter_initial_enable": 1,
                                                  "parameter_invisible": 1, "parameter_unitstyle": 0}}}
sid_id = add(sid)
sidp = obj("prepend stateid", 2, 1)
wire(sid_id, 0, sidp, 0)
wire(sidp, 0, bash, 0)
wire(router, 4, sid_id, 0)

# saved state: pattr blobs in the Live set
CHUNKS = 64   # = Instrument::stateChunks (250 ints each)
sroute = obj("route " + " ".join(str(k) for k in range(CHUNKS)), 1, CHUNKS + 1)
wire(router, 2, sroute, 0)
pattrs = []
for k in range(CHUNKS):
    pa = {"id": new_id(), "maxclass": "newobj", "text": "pattr bashstate%d" % k, "numinlets": 1, "numoutlets": 3,
          "outlettype": ["", "", ""], "patching_rect": ppos(120), "parameter_enable": 1, "varname": "bashstate%d" % k,
          "saved_attribute_attributes": {"valueof": {"parameter_longname": "BASH state %d" % k,
                                                     "parameter_shortname": "state%d" % k,
                                                     "parameter_type": 3, "parameter_invisible": 1,
                                                     "parameter_initial_enable": 0}}}
    pid = add(pa)
    pattrs.append(pid)
    back = obj("prepend restore %d" % k, 2, 1)
    wire(sroute, k, pid, 0)
    wire(pid, 0, back, 0)
    wire(back, 0, bash, 0)

# pad editor: padedit <i> <name> <kind> <tune> <gain> <pan> <decay> <start> <end> <cutoff> <choke> <reverse>
#                     <pitch> <sweep> <fm> <ratio> <noise> <tone> <body> <click> <drive>
unp = obj("unpack i s i f f f f f f f i i f f f f f f f f f", 1, 21, ["int", "", "int"] + ["float"] * 7 + ["int", "int"] + ["float"] * 9)
wire(router, 3, unp, 0)
for k, (field, *_rest) in enumerate(pad_fields):
    setter = obj("prepend set", 2, 1)
    wire(unp, 3 + k, setter, 0)
    wire(setter, 0, pad_dials[field], 0)
    out = obj("prepend pad set -1 " + field, 2, 1)
    wire(pad_dials[field], 0, out, 0)
    wire(out, 0, bash, 0)
for k, (field, *_rest) in enumerate(fm_fields):
    setter = obj("prepend set", 2, 1)
    wire(unp, 12 + k, setter, 0)
    wire(setter, 0, fm_dials[field], 0)
    out = obj("prepend pad set -1 " + field, 2, 1)
    wire(fm_dials[field], 0, out, 0)
    wire(out, 0, bash, 0)
rev_set = obj("prepend set", 2, 1)
wire(unp, 11, rev_set, 0)
wire(rev_set, 0, pad_rev_id, 0)
rev_out = obj("prepend pad set -1 reverse", 2, 1)
wire(pad_rev_id, 0, rev_out, 0)
wire(rev_out, 0, bash, 0)
# SAMPLE / FM: shows the pad's source; clicking it switches the pad (to a matching sample or FM starting point)
kind_is_fm = obj("== 2", 2, 1, ["int"])
kind_set = obj("prepend set", 2, 1)
wire(unp, 2, kind_is_fm, 0)
wire(kind_is_fm, 0, kind_set, 0)
wire(kind_set, 0, pad_kind, 0)
kind_out = obj("prepend pad kind -1", 2, 1)
wire(pad_kind, 0, kind_out, 0)
wire(kind_out, 0, bash, 0)

# the slicer display can ask for a file browser
wroute = obj("route browse", 1, 2)
wire(wave, 0, wroute, 0)
wire(wroute, 1, bash, 0)
dialog = obj("opendialog sound", 1, 2)
loadp = obj("prepend load", 2, 1)
wire(wroute, 0, dialog, 0)
wire(cmd_route, 0, dialog, 0)   # the big waveform can ask for a file too
wire(dialog, 0, loadp, 0)
wire(loadp, 0, bash, 0)

# drops
dl = obj("prepend load", 2, 1)
wire(slice_drop, 0, dl, 0)
wire(dl, 0, bash, 0)
pl = obj("prepend pad load -1", 2, 1)
wire(pad_drop, 0, pl, 0)
wire(pl, 0, bash, 0)

# the VOICE / MOD page and the view switcher
pagep = obj("prepend page", 2, 1)
wire(page, 0, pagep, 0)
wire(pagep, 0, ui_js, 0)

# MIDI in: notes, mod wheel, aftertouch, pitch bend, all-notes-off
midiin = obj("midiin", 1, 1, ["int"])
parse = obj("midiparse", 1, 8, ["", "", "", "int", "int", "", "int", ""])
wire(midiin, 0, parse, 0)
notep = obj("prepend note", 2, 1)
wire(parse, 0, notep, 0)
wire(notep, 0, bash, 0)
ccr = obj("route 1 123", 1, 3)
wire(parse, 2, ccr, 0)
mw = obj("/ 127.", 2, 1, ["float"])
mwp = obj("prepend modwheel", 2, 1)
wire(ccr, 0, mw, 0); wire(mw, 0, mwp, 0); wire(mwp, 0, bash, 0)
off = msg("allnotesoff")
wire(ccr, 1, off, 0); wire(off, 0, bash, 0)
at = obj("/ 127.", 2, 1, ["float"])
atp = obj("prepend touch", 2, 1)
wire(parse, 4, at, 0); wire(at, 0, atp, 0); wire(atp, 0, bash, 0)
pb = obj("expr ($i1 - 64) / 64.", 1, 1)
pbp = obj("prepend bend", 2, 1)
wire(parse, 5, pb, 0); wire(pb, 0, pbp, 0); wire(pbp, 0, bash, 0)

# Live's key and scale (Live 12): root_note + scale_intervals -> "key <root> <mask>"
dev = obj("live.thisdevice", 1, 3, ["bang", "int", "int"])
start = obj("t b b b", 1, 3, ["bang", "bang", "bang"])
wire(dev, 0, start, 0)
pathmsg = msg("path live_set")
lpath = obj("live.path", 1, 3, ["", "", ""])
wire(start, 0, pathmsg, 0)
wire(pathmsg, 0, lpath, 0)
rootobs = obj("live.observer root_note", 2, 2)
scaleobs = obj("live.observer scale_intervals", 2, 2)
wire(lpath, 0, rootobs, 1)
wire(lpath, 0, scaleobs, 1)
bits = obj("vexpr 1 << $i1", 1, 1)
total = obj("zl.sum", 2, 2)
keypak = obj("pak i i", 2, 1)
keyp = obj("prepend key", 2, 1)
wire(rootobs, 0, keypak, 0)
wire(scaleobs, 0, bits, 0)
wire(bits, 0, total, 0)
wire(total, 0, keypak, 1)
wire(keypak, 0, keyp, 0)
wire(keyp, 0, bash, 0)
# Live's transport: tempo, play / stop and song position (BASH! follows them while Sync is on)
for prop, sel in (("tempo", "hosttempo"), ("is_playing", "hostplaying"), ("current_song_time", "hostbeats")):
    o = obj("live.observer " + prop, 2, 2)
    pp = obj("prepend " + sel, 2, 1)
    wire(lpath, 0, o, 1)
    wire(o, 0, pp, 0)
    wire(pp, 0, bash, 0)
    if prop == "tempo":
        CUR[0] = "clock"
        tset = msg("set $1")
        wire(o, 0, tset, 0)
        wire(tset, 0, live_tempo_id, 0)
        CUR[0] = "core"
# Live's Scale switch on / off (scale awareness): with Scale off, BASH! stops quantising to it
modeobs = obj("live.observer scale_mode", 2, 2)
modep = obj("prepend scalemode", 2, 1)
wire(lpath, 0, modeobs, 1)
wire(modeobs, 0, modep, 0)
wire(modep, 0, bash, 0)

# ---- LFO MAP (like Live's LFO): press Map, click any parameter in Live, the LFO drives it -----------
# live_set view's selected_parameter tells us what was clicked; live.remote~ takes it over.
selpath_msg = msg("path live_set view")
selpath = obj("live.path", 1, 3, ["", "", ""])
selobs = obj("live.observer selected_parameter", 2, 2)
wire(start, 0, selpath_msg, 0)
wire(selpath_msg, 0, selpath, 0)
wire(selpath, 0, selobs, 1)
restore_route = obj("route 1 2", 1, 3)
maproute = obj("route maprestore", 1, 2)
wire(bash, MSG, maproute, 0)
wire(maproute, 0, restore_route, 0)
for k in (1, 2):
    mbid, unmap_msg = map_buttons[k]
    # Map lit = waiting: the gate lets the next clicked parameter through
    gate = obj("gate 1", 2, 1)
    waitp = obj("prepend mapwait %d" % k, 2, 1)
    wire(mbid, 0, gate, 0)
    wire(mbid, 0, waitp, 0)
    wire(waitp, 0, bash, 0)
    wire(selobs, 0, gate, 1)
    # a clicked parameter (or a saved one found again): id N
    rid = obj("route id", 1, 2)
    nonzero = obj("sel 0", 2, 2, ["bang", ""])
    wire(gate, 0, rid, 0)
    wire(rid, 0, nonzero, 0)
    found = obj("t b b b b b b i", 1, 7, ["bang", "bang", "bang", "bang", "bang", "bang", "int"])
    wire(nonzero, 1, found, 0)
    remote = obj("live.remote~", 2, 1)
    idp = obj("prepend id", 2, 1)
    wire(found, 6, idp, 0)
    wire(idp, 0, remote, 1)
    wire(bash, 1 + k, remote, 0)          # the LFO, already in the parameter's units
    info = obj("live.object", 2, 1)
    wire(idp, 0, info, 1)
    for idx, text in ((5, "getpath"), (4, "get min"), (3, "get max"), (2, "get name"), (1, "get canonical_parent")):
        mm = msg(text)
        wire(found, idx, mm, 0)
        wire(mm, 0, info, 0)
    # done: Map goes dark, the gate closes
    done = msg("set 0")
    close = msg("0")
    stopw = msg("mapwait %d 0" % k)
    wire(found, 0, done, 0); wire(done, 0, mbid, 0)
    wire(found, 0, close, 0); wire(close, 0, gate, 0)
    wire(found, 0, stopw, 0); wire(stopw, 0, bash, 0)
    # what live.object says -> bash~ (saved with the set, shown in the LFO display)
    iroute = obj("route path min max name canonical_parent", 1, 6)
    wire(info, 0, iroute, 0)
    for idx, sel in ((0, "mappath"), (1, "mapmin"), (2, "mapmax"), (3, "mapname")):
        pp = obj("prepend %s %d" % (sel, k), 2, 1)
        wire(iroute, idx, pp, 0)
        wire(pp, 0, bash, 0)
    # the device's name: canonical_parent id M -> its name
    devid = obj("route id", 1, 2)
    wire(iroute, 4, devid, 0)
    devobj = obj("live.object", 2, 1)
    devget = obj("t b i", 1, 2, ["bang", "int"])
    devidp = obj("prepend id", 2, 1)
    devname = msg("get name")
    wire(devid, 0, devget, 0)
    wire(devget, 1, devidp, 0); wire(devidp, 0, devobj, 1)
    wire(devget, 0, devname, 0); wire(devname, 0, devobj, 0)
    devroute = obj("route name", 1, 2)
    devp = obj("prepend mapdevice %d" % k, 2, 1)
    wire(devobj, 0, devroute, 0); wire(devroute, 0, devp, 0); wire(devp, 0, bash, 0)
    # unmap: let go of the parameter
    zero = msg("id 0")
    unsel = obj("t b b", 1, 2, ["bang", "bang"])
    wire(unmap_msg, 0, unsel, 0)
    wire(unsel, 0, zero, 0); wire(zero, 0, remote, 1)
    # a saved mapping: maprestore k <path> -> live.path -> id N -> the same chain
    rpath = obj("prepend path", 2, 1)
    rlp = obj("live.path", 1, 3, ["", "", ""])
    wire(restore_route, k - 1, rpath, 0)
    wire(rpath, 0, rlp, 0)
    wire(rlp, 0, rid, 0)

# when the device opens: every parameter and saved chunk reports in, then the displays fill
refresh = msg("refresh")
wire(start, 1, refresh, 0)
wire(refresh, 0, bash, 0)
for oid, _ in param_objects:
    wire(start, 2, oid, 0)
for pid in pattrs:
    wire(start, 2, pid, 0)
wire(start, 2, sid_id, 0)
wire(start, 2, page, 0)
if VARIANT != "voice":
    bars_init = msg("set 2")
    wire(start, 2, bars_init, 0)
    wire(bars_init, 0, bars_menu, 0)

# =====================================================================================
# the variants
if VARIANT == "seq":
    # MIDI effect: the sequencer's notes go out as MIDI; other MIDI (CCs, bend, aftertouch) passes through
    init = msg("midiout 1, param seqon 1")
    wire(start, 2, init, 0); wire(init, 0, bash, 0)
    mroute2 = obj("route midi", 1, 2)
    wire(bash, MSG, mroute2, 0)
    fmt = obj("midiformat 1", 7, 1, ["int"])
    mout = obj("midiout", 1, 0)
    wire(mroute2, 0, fmt, 0)
    wire(fmt, 0, mout, 0)
    for k in (2, 3, 4, 5):
        wire(parse, k, fmt, k)
if VARIANT == "voice":
    # an instrument: notes from clips / a keyboard play the voice; no sequencer
    init = msg("param seqon 0, param thru 1, param mode 0")
    wire(start, 2, init, 0); wire(init, 0, bash, 0)

if VARIANT != "full":
    drop = {"voice": {"clock", "seq", "export", "exportwav"}, "seq": {"voice", "page", "out", "exportwav"}}[VARIANT]
    removed = {b["box"]["id"] for b in boxes if b["box"]["_sec"] in drop}
    if VARIANT == "seq":
        removed.add(plugout)
    boxes[:] = [b for b in boxes if b["box"]["id"] not in removed]
    lines[:] = [l for l in lines if l["patchline"]["source"][0] not in removed and l["patchline"]["destination"][0] not in removed]
    keep = {b["box"].get("varname") for b in boxes}
    for g in groups:
        groups[g] = [v for v in groups[g] if v in keep]
    param_objects[:] = [(o, p) for o, p in param_objects if o not in removed]
    if VARIANT == "voice":
        W = 724
        for b in boxes:
            if b["box"].get("filename") == "bash_badge.js":
                b["box"]["jsarguments"] = ["nogen"]
        for b in boxes:
            if b["box"].get("varname") in ("root", "scale"):
                b["box"]["presentation_rect"][1] = 90
        for b in boxes:
            bx = b["box"]
            if bx["_sec"] in ("engine", "voice", "page", "mod", "out", "expand") and bx.get("presentation"):
                bx["presentation_rect"][0] -= 474
    if VARIANT == "seq":
        W = 1104
        for b in boxes:
            bx = b["box"]
            if bx["_sec"] == "expand" and bx.get("presentation"):
                bx["presentation_rect"] = [1056, 4, 44, 16]
            if bx["_sec"] == "export" and bx.get("presentation"):
                if bx["maxclass"] == "comment": bx["presentation_rect"] = [896, 5, 60, 14]
                elif bx["maxclass"] == "umenu": bx["presentation_rect"] = [946, 4, 52, 16]
                else: bx["presentation_rect"] = [1002, 4, 50, 16]
for b in boxes:
    b["box"].pop("_sec", None)

# =====================================================================================
# the view switcher script (generated: it knows which objects belong to which view)
ui_js_text = """// BASH! view switcher (generated by make_device.py): shows the objects for the
// sequencer mode (Generative / Step), the voice (Synth / Slicer / Drums) and the page (Voice / Mod).
inlets = 1;
outlets = 1;
var groups = %s;
var mode = 0, engine = 0, sync = 1, padfm = 0, page = %d;

function apply()
{
    var show = {
        gen: mode === 0, step: mode === 1,
        synth: page === 0 && engine === 0, slice: page === 0 && engine === 1, drum: page === 0 && engine === 2,
        padsample: page === 0 && engine === 2 && padfm === 0, padfm: page === 0 && engine === 2 && padfm === 1,
        mod: page === 1, free: sync === 0, synced: sync === 1
    };
    for (var g in groups)
    {
        var names = groups[g];
        for (var i = 0; i < names.length; i++)
        {
            var o = this.patcher.getnamed(names[i]);
            if (o) o.hidden = show[g] ? 0 : 1;
        }
    }
}

function anything()
{
    var a = arrayfromargs(arguments);
    if (messagename === "pv")
    {
        if (a[0] === "mode") { mode = a[1]; apply(); }
        else if (a[0] === "engine") { engine = a[1]; apply(); }
        else if (a[0] === "sync") { sync = a[1] > 0.5 ? 1 : 0; apply(); }
    }
    else if (messagename === "page") { page = a[0]; apply(); }
    else if (messagename === "padedit") { var f = a[2] === 2 ? 1 : 0; if (f !== padfm) { padfm = f; apply(); } }
}

function loadbang() { apply(); }
""" % (json.dumps(groups, indent=1), 1 if VARIANT == "seq" else 0)

patcher = {
    "patcher": {
        "fileversion": 1,
        "appversion": {"major": 8, "minor": 6, "revision": 0, "architecture": "x64", "modernui": 1},
        "classnamespace": "box",
        "rect": [40.0, 80.0, 1400.0, 900.0],
        "openinpresentation": 1,
        "default_fontsize": 10.0,
        "default_fontname": "Arial",
        "gridsize": [15.0, 15.0],
        "boxes": boxes,
        "lines": lines,
        "description": "BASH! one-channel groovebox: generative acid / step sequencer driving a synth, slicer or drum kit.",
        "digest": "",
        "tags": "BASH",
        "devicewidth": float(W),
    }
}

os.makedirs(OUT, exist_ok=True)
NAME = NAMES[VARIANT]
patcher["patcher"]["devicewidth"] = float(W)
patcher["patcher"]["description"] = {
    "full": "BASH! one-channel groovebox: generative acid / step sequencer driving a synth, slicer or drum kit. By Kalabash.",
    "voice": "BASH! Voice: the synth, slicer and drum kit from BASH!, played from clips or a keyboard. By Kalabash.",
    "seq": "BASH! Sequencer: BASH!'s generative acid and step sequencers as MIDI, for any instrument or Drum Rack. By Kalabash."}[VARIANT]
patcher["patcher"]["tags"] = "Kalabash BASH"
text = json.dumps(patcher, indent=1)
with open(os.path.join(OUT, NAME + ".maxpat"), "w") as f:
    f.write(text)
with open(os.path.join(OUT, UI_JS), "w") as f:
    f.write(ui_js_text)

# .amxd: 'ampf' header (device type 'iiii' instrument / 'mmmm' MIDI effect), 'meta', then the patch JSON
payload = text.encode("utf-8") + b"\x00"
amxd = (b"ampf" + struct.pack("<I", 4) + (b"mmmm" if VARIANT == "seq" else b"iiii") +
        b"meta" + struct.pack("<I", 4) + struct.pack("<I", 0) +
        b"ptch" + struct.pack("<I", len(payload)) + payload)
with open(os.path.join(OUT, NAME + ".amxd"), "wb") as f:
    f.write(amxd)
print("wrote", NAME + ".amxd", "to", OUT, "-", len(boxes), "objects,", len(lines), "connections,", len(param_objects), "parameters")
