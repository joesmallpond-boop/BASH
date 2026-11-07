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
P = {p["id"]: p for p in PARAMS}

W, H = 1114, 169
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
groups = {"gen": [], "step": [], "synth": [], "slice": [], "drum": [], "mod": []}
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

def comment(text, rect, size=8.5, color=QUIET, group=None, bold=False):
    b = {"id": new_id(), "maxclass": "comment", "text": text, "numinlets": 1, "numoutlets": 0,
         "fontsize": size, "textcolor": color, "fontname": "Arial Bold" if bold else "Arial"}
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
    if cls == "live.dial":
        b.update({"showname": 1, "shownumber": 1, "textcolor": INK, "activedialcolor": AMBER, "activeneedlecolor": AMBER,
                  "dialcolor": RAISED, "needlecolor": INK, "fontsize": 8.0})
    if cls in ("live.menu", "live.tab", "live.text", "live.numbox"):
        b.update({"bgcolor": PANEL, "textcolor": INK, "activebgcolor": PANEL, "activetextcolor": INK,
                  "bgoncolor": ACTIVE, "activebgoncolor": ACTIVE, "textoncolor": AMBER, "activetextoncolor": AMBER,
                  "bordercolor": [0.227, 0.227, 0.227, 1.0], "focusbordercolor": AMBER, "fontsize": 8.5})
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
         "parameter_enable": 0, "bgcolor": PANEL, "bgfillcolor_color": PANEL, "textcolor": INK, "fontsize": 8.5,
         "arrow": 1, "items": ["(loading)"]}
    pres(b, rect)
    return add(b, group)

def drop(rect, label, group=None):
    b = {"id": new_id(), "maxclass": "live.drop", "numinlets": 1, "numoutlets": 2, "outlettype": ["", ""],
         "parameter_enable": 0, "bgcolor": PANEL, "bordercolor": AMBER_DIM, "textcolor": QUIET, "legend": label,
         "fontsize": 8.5}
    pres(b, rect)
    return add(b, group)

def plain_dial(label, lo, hi, init, rect, group=None, exp=1.0):
    """A live.dial that is not a Live parameter (per-pad settings live in BASH!'s own state)."""
    b = {"id": new_id(), "maxclass": "live.dial", "numinlets": 1, "numoutlets": 2, "outlettype": ["", "float"],
         "parameter_enable": 0, "showname": 1, "shownumber": 1, "textcolor": INK, "activedialcolor": AMBER,
         "dialcolor": RAISED, "fontsize": 8.0, "varname": "pad_" + label.lower(),
         "saved_attribute_attributes": {"valueof": {"parameter_longname": "Pad " + label, "parameter_shortname": label,
                                                    "parameter_type": 0, "parameter_mmin": lo, "parameter_mmax": hi,
                                                    "parameter_initial": [init], "parameter_unitstyle": 1,
                                                    "parameter_exponent": exp}}}
    pres(b, rect)
    return add(b, group)

# =====================================================================================
# the engine
bash = obj("bash~", 1, 3, ["signal", "signal", ""])
plugout = obj("plugout~ 1 2", 2, 2, ["signal", "signal"])
wire(bash, 0, plugout, 0)
wire(bash, 1, plugout, 1)

# background
panel([0, 0, W, H], BG, rounded=0)
panel([112, 0, 2, H], [0.18, 0.18, 0.18, 1.0], rounded=0)
panel([586, 0, 2, H], [0.18, 0.18, 0.18, 1.0], rounded=0)
panel([1062, 0, 2, H], [0.18, 0.18, 0.18, 1.0], rounded=0)

jsuis = []   # every display gets everything bash~ sends

# ---- column A: badge, clock, key, dice ---------------------------------------------------
jsuis.append(jsui("bash_badge.js", [4, 4, 104, 62]))
toggle("play", "PLAY", [4, 70, 50, 16])
toggle("sync", "SYNC", [58, 70, 50, 16])
live_param("live.numbox", "tempo", [4, 90, 50, 16])
toggle("keylock", "LIVE KEY", [58, 90, 50, 16])
live_param("live.menu", "root", [4, 110, 36, 16])
live_param("live.menu", "scale", [42, 110, 66, 16])
dice = jsui("bash_btns.js", [4, 130, 104, 18], args=["DICE|dice", "UNDO|undo"])
jsuis.append(dice)
toggle("thru", "THRU", [4, 151, 50, 15])
toggle("seqon", "SEQ ON", [58, 151, 50, 15])

# ---- column B: the sequencer --------------------------------------------------------------
live_param("live.tab", "mode", [118, 4, 116, 16])
live_param("live.menu", "rate", [238, 4, 44, 16])
live_param("live.menu", "algo", [286, 4, 108, 16], "gen")
groove_menu = umenu([286, 4, 108, 16], "step")
toggle("fill", "FILL", [398, 4, 32, 16])
live_param("live.menu", "autofill", [434, 4, 54, 16])
jsuis.append(jsui("bash_btns.js", [492, 4, 92, 16], "gen", args=["NEW|regen", "UNLOCK|unlockall"]))
jsuis.append(jsui("bash_btns.js", [492, 4, 92, 16], "step", args=["CLEAR ALL|steps clear"]))
jsuis.append(jsui("bash_seq.js", [118, 24, 466, 84]))
for i, pid in enumerate(["type", "density", "steps", "offset", "accent", "octave", "jitter"]):
    live_param("live.dial", pid, [118 + i * 46, 112, 44, 50], "gen")
jsuis.append(jsui("bash_lane.js", [118, 112, 318, 54], "step"))
live_param("live.dial", "gate", [440, 112, 44, 50])
live_param("live.dial", "swing", [486, 112, 44, 50])
live_param("live.menu", "direction", [534, 114, 50, 16], "gen")
toggle("scalelock", "SCALE", [534, 134, 50, 16])

# ---- column C: the voice ------------------------------------------------------------------
live_param("live.tab", "engine", [592, 4, 150, 16])
page_tab = {"id": new_id(), "maxclass": "live.tab", "numinlets": 1, "numoutlets": 3, "outlettype": ["", "", "float"],
            "parameter_enable": 1, "varname": "view", "num_lines_patching": 1, "num_lines_presentation": 1,
            "bgcolor": PANEL, "textcolor": INK, "bgoncolor": ACTIVE, "textoncolor": AMBER, "fontsize": 8.5,
            "saved_attribute_attributes": {"valueof": {"parameter_longname": "View", "parameter_shortname": "View",
                                                       "parameter_type": 2, "parameter_enum": ["VOICE", "MOD"],
                                                       "parameter_mmax": 1, "parameter_initial": [0],
                                                       "parameter_initial_enable": 1, "parameter_invisible": 1}}}
pres(page_tab, [746, 4, 88, 16])
page = add(page_tab)

# synth
live_param("live.menu", "machine", [838, 4, 110, 16], "synth")
live_param("live.menu", "ftype", [952, 4, 108, 16], "synth")
for i, pid in enumerate(["wave", "timbre", "cutoff", "reso", "envmod", "decay", "drive", "glide"]):
    live_param("live.dial", pid, [592 + i * 46, 26, 44, 50], "synth")
for i, pid in enumerate(["aatk", "adec", "asus", "arel"]):
    live_param("live.dial", pid, [592 + i * 46, 86, 44, 50], "synth")
jsuis.append(jsui("bash_keys.js", [778, 82, 282, 84], "synth"))
comment("AMP", [592, 140, 60, 14], group="synth")

# slicer
loop_menu = umenu([838, 4, 222, 16], "slice")
wave = jsui("bash_wave.js", [592, 24, 300, 90], "slice")
jsuis.append(wave)
jsuis.append(jsui("bash_tempo.js", [896, 24, 82, 44], "slice"))
slice_drop = drop([982, 24, 78, 44], "DROP SAMPLE", "slice")
toggle("warp", "WARP", [896, 72, 52, 16], "slice")
toggle("reverse", "REVERSE", [952, 72, 52, 16], "slice")
toggle("pitchfollow", "PITCH", [1008, 72, 52, 16], "slice")
live_param("live.menu", "slicemode", [896, 96, 100, 16], "slice")
for i, pid in enumerate(["winstart", "winsize", "tune", "sattack", "srelease"]):
    live_param("live.dial", pid, [592 + i * 46, 116, 44, 50], "slice")
comment("Slice mode: how the sequencer moves through the slices", [830, 118, 228, 30], 7.5, group="slice")

# drums
kit_menu = umenu([838, 4, 222, 16], "drum")
jsuis.append(jsui("bash_pads.js", [592, 24, 142, 142], "drum"))
sound_menu = umenu([738, 24, 170, 16], "drum")
pad_drop = drop([912, 24, 148, 40], "DROP ON PAD", "drum")
comment("SOUND FOR THE SELECTED PAD", [738, 42, 172, 14], 7.5, group="drum")
pad_fields = [("tune", "TUNE", -24, 24, 0, 1.0), ("gain", "GAIN", 0, 2, 1, 1.0), ("pan", "PAN", -1, 1, 0, 1.0),
              ("decay", "DECAY", 0.01, 4, 0.4, 2.0), ("start", "START", 0, 1, 0, 1.0), ("end", "END", 0, 1, 1, 1.0),
              ("cutoff", "CUTOFF", 40, 20000, 20000, 3.0), ("choke", "CHOKE", 0, 8, 0, 1.0)]
pad_dials = {}
for i, (field, label, lo, hi, init, exp) in enumerate(pad_fields):
    pad_dials[field] = plain_dial(label, lo, hi, init, [738 + i * 40, 66, 40, 50], "drum", exp)
jsuis.append(jsui("bash_btns.js", [738, 122, 170, 18], "drum", args=["PLAY|pad play -1", "CLEAR|pad clear -1"]))
pad_rev = {"id": new_id(), "maxclass": "live.text", "numinlets": 1, "numoutlets": 2, "outlettype": ["", ""],
           "parameter_enable": 0, "mode": 1, "text": "REVERSE", "texton": "REVERSE", "varname": "pad_reverse",
           "bgcolor": PANEL, "textcolor": INK, "bgoncolor": ACTIVE, "textoncolor": AMBER, "fontsize": 8.5}
pres(pad_rev, [912, 122, 70, 18])
pad_rev_id = add(pad_rev, "drum")
comment("Click a pad to play it, shift-click to select, alt-click to clear.", [738, 144, 322, 22], 8.0, group="drum")

# mod page
for k, y in ((1, 24), (2, 82)):
    jsuis.append(jsui("bash_lfo.js", [592, y, 100, 52], "mod", args=[k]))
    live_param("live.dial", "lfo%drate" % k, [696, y, 44, 50], "mod")
    live_param("live.menu", "lfo%dshape" % k, [744, y + 2, 82, 16], "mod")
    live_param("live.menu", "lfo%ddiv" % k, [744, y + 20, 82, 16], "mod")
    toggle("lfo%dsync" % k, "SYNC", [744, y + 38, 82, 14], "mod")
jsuis.append(jsui("bash_lfo.js", [592, 140, 100, 26], "mod", args=[3]))
comment("LIFE", [696, 140, 30, 14], group="mod")
live_param("live.numbox", "liferate", [728, 142, 98, 16], "mod")
comment("SOURCE", [832, 24, 70, 14], group="mod")
comment("DESTINATION", [906, 24, 90, 14], group="mod")
comment("AMOUNT", [1000, 24, 60, 14], group="mod")
for k in range(1, 7):
    y = 40 + (k - 1) * 21
    live_param("live.menu", "m%dsrc" % k, [832, y, 70, 16], "mod")
    live_param("live.menu", "m%ddst" % k, [906, y, 90, 16], "mod")
    live_param("live.numbox", "m%damt" % k, [1000, y, 60, 16], "mod")

# ---- column D: level, pan, meter -------------------------------------------------------------
live_param("live.dial", "level", [1068, 22, 44, 50])
live_param("live.dial", "pan", [1068, 80, 44, 50])
meter = {"id": new_id(), "maxclass": "live.meter~", "numinlets": 1, "numoutlets": 1, "outlettype": ["list"],
         "coldcolor": AMBER_DIM, "warmcolor": AMBER, "hotcolor": [1.0, 0.5, 0.0, 1.0], "overloadcolor": [1.0, 0.2, 0.1, 1.0],
         "orientation": 1}
pres(meter, [1072, 136, 36, 28])
meter_id = add(meter)
wire(bash, 0, meter_id, 0)

# =====================================================================================
# wiring: parameters -> bash~
for oid, pid in param_objects:
    pre = obj("prepend param " + pid, 2, 1)
    wire(oid, 0, pre, 0)
    wire(pre, 0, bash, 0)

# bash~ messages -> every display + the view switcher + routers
ui_js = obj("js bash_ui.js", 1, 1)
for j in jsuis:
    wire(bash, 2, j, 0)
wire(bash, 2, ui_js, 0)
for j in jsuis:
    if j == wave:
        continue
    wire(j, 0, bash, 0)

router = obj("route ui menu state padedit", 1, 5)
wire(bash, 2, router, 0)

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
mroute = obj("route loops kits sounds grooves", 1, 5)
wire(router, 1, mroute, 0)
for k, (menu, action) in enumerate([(loop_menu, "loop"), (kit_menu, "kit pick"), (sound_menu, "pad sound -1"), (groove_menu, "groove")]):
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

# saved state: 48 pattr blobs in the Live set
CHUNKS = 48
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
unp = obj("unpack i s i f f f f f f f i i", 1, 12, ["int", "", "int", "float", "float", "float", "float", "float", "float", "float", "int", "int"])
wire(router, 3, unp, 0)
for k, (field, *_rest) in enumerate(pad_fields):
    setter = obj("prepend set", 2, 1)
    wire(unp, 3 + k, setter, 0)
    wire(setter, 0, pad_dials[field], 0)
    out = obj("prepend pad set -1 " + field, 2, 1)
    wire(pad_dials[field], 0, out, 0)
    wire(out, 0, bash, 0)
rev_set = obj("prepend set", 2, 1)
wire(unp, 11, rev_set, 0)
wire(rev_set, 0, pad_rev_id, 0)
rev_out = obj("prepend pad set -1 reverse", 2, 1)
wire(pad_rev_id, 0, rev_out, 0)
wire(rev_out, 0, bash, 0)

# the slicer display can ask for a file browser
wroute = obj("route browse", 1, 2)
wire(wave, 0, wroute, 0)
wire(wroute, 1, bash, 0)
dialog = obj("opendialog sound", 1, 2)
loadp = obj("prepend load", 2, 1)
wire(wroute, 0, dialog, 0)
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

# when the device opens: every parameter and saved chunk reports in, then the displays fill
refresh = msg("refresh")
wire(start, 1, refresh, 0)
wire(refresh, 0, bash, 0)
for oid, _ in param_objects:
    wire(start, 2, oid, 0)
for pid in pattrs:
    wire(start, 2, pid, 0)
wire(start, 2, page, 0)

# =====================================================================================
# the view switcher script (generated: it knows which objects belong to which view)
ui_js_text = """// BASH! view switcher (generated by make_device.py): shows the objects for the
// sequencer mode (Generative / Step), the voice (Synth / Slicer / Drums) and the page (Voice / Mod).
inlets = 1;
outlets = 1;
var groups = %s;
var mode = 0, engine = 0, page = 0;

function apply()
{
    var show = {
        gen: mode === 0, step: mode === 1,
        synth: page === 0 && engine === 0, slice: page === 0 && engine === 1, drum: page === 0 && engine === 2,
        mod: page === 1
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
    }
    else if (messagename === "page") { page = a[0]; apply(); }
}

function loadbang() { apply(); }
""" % json.dumps(groups, indent=1)

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
text = json.dumps(patcher, indent=1)
with open(os.path.join(OUT, "BASH!.maxpat"), "w") as f:
    f.write(text)
with open(os.path.join(OUT, "bash_ui.js"), "w") as f:
    f.write(ui_js_text)

# .amxd: 'ampf' header (device type 'iiii' = instrument), 'meta', then the patch JSON in 'ptch'
payload = text.encode("utf-8") + b"\x00"
amxd = (b"ampf" + struct.pack("<I", 4) + b"iiii" +
        b"meta" + struct.pack("<I", 4) + struct.pack("<I", 0) +
        b"ptch" + struct.pack("<I", len(payload)) + payload)
with open(os.path.join(OUT, "BASH!.amxd"), "wb") as f:
    f.write(amxd)
print("wrote BASH!.amxd, BASH!.maxpat and bash_ui.js to", OUT, "-", len(boxes), "objects,", len(lines), "connections,", len(param_objects), "parameters")
