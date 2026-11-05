#!/usr/bin/env python3
"""Builds the "BASH! Key Lock" Max for Live MIDI effect.

Put it on the same MIDI track as BASH!, before it. It passes your MIDI straight
through, and sends Live's key and scale to BASH! whenever they change (and every
two seconds, so a freshly loaded BASH! picks them up):

  CC 102 = root note (0-11), CC 103 / 104 = the scale's notes as a 12-bit mask
  (low 7 bits, then high 5), all on MIDI channel 16.

Writes  m4l/BASH Key Lock.maxpat  (open in Max to edit)
and     m4l/BASH Key Lock.amxd    (drag into Live).
"""
import json, struct, os

boxes, lines = [], []

def box(id_, text, x, y, ins, outs, outtypes=None, cls="newobj", w=None, **extra):
    b = {"id": id_, "maxclass": cls, "numinlets": ins, "numoutlets": outs,
         "patching_rect": [x, y, w or max(60, 7 * len(text) + 16), 22]}
    if cls in ("newobj", "message", "comment"):
        b["text"] = text
    if outs:
        b["outlettype"] = outtypes or [""] * outs
    b.update(extra)
    boxes.append({"box": b})

def wire(src, so, dst, di):
    lines.append({"patchline": {"source": [src, so], "destination": [dst, di]}})

# MIDI passthrough: notes from your clips still reach BASH!
box("midiin",  "midiin",  30, 30, 1, 1, ["int"])
box("midiout", "midiout", 30, 70, 1, 0)
wire("midiin", 0, "midiout", 0)

# find the Live set once the device is ready
box("device",  "live.this.device", 200, 30, 1, 3, ["bang", "int", "int"])
box("init",    "t b b", 200, 60, 1, 2, ["bang", "bang"])
box("pathmsg", "path live_set", 260, 90, 2, 1, [""], cls="message")
box("path",    "live.path", 260, 120, 1, 3, ["", "", ""])
wire("device", 0, "init", 0)
wire("init", 1, "pathmsg", 0)
wire("pathmsg", 0, "path", 0)

# root note -> CC 102
box("rootobs", "live.observer root_note", 200, 160, 2, 2, ["", ""])
box("root",    "i", 200, 190, 2, 1, ["int"])
box("rootcc",  "ctlout 102 16", 200, 220, 3, 0)
wire("path", 0, "rootobs", 1)
wire("rootobs", 0, "root", 0)
wire("root", 0, "rootcc", 0)

# scale -> 12-bit mask -> CC 103 (low 7 bits), CC 104 (high 5 bits)
box("scaleobs", "live.observer scale_intervals", 420, 160, 2, 2, ["", ""])
box("bits",     "vexpr 1 << $i1", 420, 190, 1, 1, [""])
box("sum",      "zl.sum", 420, 220, 2, 2, ["", ""])
box("mask",     "i", 420, 250, 2, 1, ["int"])
box("order",    "t i i", 420, 280, 1, 2, ["int", "int"])
box("low",      "& 127", 500, 310, 2, 1, ["int"])
box("lowcc",    "ctlout 103 16", 500, 340, 3, 0)
box("high",     ">> 7", 420, 370, 2, 1, ["int"])
box("highcc",   "ctlout 104 16", 420, 400, 3, 0)
wire("path", 0, "scaleobs", 1)
wire("scaleobs", 0, "bits", 0)
wire("bits", 0, "sum", 0)
wire("sum", 0, "mask", 0)
wire("mask", 0, "order", 0)
wire("order", 1, "low", 0)      # right outlet fires first: CC 103 before CC 104
wire("low", 0, "lowcc", 0)
wire("order", 0, "high", 0)
wire("high", 0, "highcc", 0)

# resend every 2 s so a BASH! loaded later still gets the key
box("metro", "metro 2000", 120, 130, 2, 1, ["bang"])
wire("init", 0, "metro", 0)
wire("metro", 0, "root", 0)
wire("metro", 0, "mask", 0)

# what the device shows in Live
box("title", "BASH! Key Lock", 10, 10, 1, 0, cls="comment", w=180,
    presentation=1, presentation_rect=[8, 8, 170, 22], fontsize=14.0, fontface=1)
box("help", "Put before BASH! on this track. BASH! follows the set's key and scale.", 10, 450, 1, 0,
    cls="comment", w=380, presentation=1, presentation_rect=[8, 34, 170, 70], linecount=4)

patcher = {
    "patcher": {
        "fileversion": 1,
        "appversion": {"major": 8, "minor": 5, "revision": 0, "architecture": "x64", "modernui": 1},
        "classnamespace": "box",
        "rect": [100.0, 100.0, 700.0, 500.0],
        "openinpresentation": 1,
        "default_fontsize": 10.0,
        "gridsize": [15.0, 15.0],
        "boxes": boxes,
        "lines": lines,
        "description": "Sends Live's key and scale to BASH!",
        "digest": "",
        "tags": "BASH",
        "devicewidth": 190.0,
    }
}

here = os.path.dirname(os.path.abspath(__file__))
text = json.dumps(patcher, indent=2)
with open(os.path.join(here, "BASH Key Lock.maxpat"), "w") as f:
    f.write(text)

# .amxd = 'ampf' header (device type 'mmmm' = MIDI effect), 'meta', then the patch JSON in 'ptch'
payload = text.encode("utf-8") + b"\x00"
amxd = (b"ampf" + struct.pack("<I", 4) + b"mmmm" +
        b"meta" + struct.pack("<I", 4) + struct.pack("<I", 0) +
        b"ptch" + struct.pack("<I", len(payload)) + payload)
with open(os.path.join(here, "BASH Key Lock.amxd"), "wb") as f:
    f.write(amxd)
print("wrote BASH Key Lock.maxpat and BASH Key Lock.amxd")
