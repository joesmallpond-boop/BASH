#!/usr/bin/env python3
"""Checks the generated BASH! device: every connection points at a real inlet / outlet,
ids and parameter names are unique, every display script exists, everything sits inside
the device, and no two controls overlap in any view (Generative / Step x Synth / Slicer /
Drums x Voice / Mod).   python3 max/check_device.py <BASH!.maxpat> <bash_ui.js> <javascript dir>"""
import json, os, re, sys

patch = json.load(open(sys.argv[1]))["patcher"]
ui_js = open(sys.argv[2]).read()
js_dir = sys.argv[3]
groups = json.loads(re.search(r"var groups = (\{.*?\});", ui_js, re.S).group(1))
boxes = [b["box"] for b in patch["boxes"]]
errors = []

ids = [b["id"] for b in boxes]
if len(ids) != len(set(ids)): errors.append("duplicate ids")
by_id = {b["id"]: b for b in boxes}
for l in patch["lines"]:
    (s, so), (d, di) = l["patchline"]["source"], l["patchline"]["destination"]
    if s not in by_id or d not in by_id: errors.append("line to a missing object %s -> %s" % (s, d)); continue
    if so >= by_id[s]["numoutlets"]: errors.append("%s (%s) has no outlet %d" % (s, by_id[s].get("text", by_id[s]["maxclass"]), so))
    if di >= by_id[d]["numinlets"]: errors.append("%s (%s) has no inlet %d" % (d, by_id[d].get("text", by_id[d]["maxclass"]), di))

names = {}
for b in boxes:
    v = b.get("saved_attribute_attributes", {}).get("valueof")
    if v and b.get("parameter_enable"):
        n = v["parameter_longname"]
        if n in names: errors.append("parameter name used twice: " + n)
        names[n] = b["id"]
        if len(n) > 31: errors.append("parameter name too long: " + n)
    if b["maxclass"] == "jsui" and not os.path.exists(os.path.join(js_dir, b["filename"])):
        errors.append("missing script " + b["filename"])
    if "presentation_rect" in b:
        x, y, w, h = b["presentation_rect"]
        if x < 0 or y < 0 or x + w > patch["devicewidth"] + 0.01 or y + h > 169.01:
            errors.append("%s %s is outside the device: %s" % (b["maxclass"], b.get("varname", b["id"]), b["presentation_rect"]))
for g, members in groups.items():
    for m in members:
        if not any(b.get("varname") == m for b in boxes): errors.append("view group %s names a missing object %s" % (g, m))

member_of = {}
for g, members in groups.items():
    for m in members: member_of[m] = g
interactive = [b for b in boxes if b.get("presentation") and b["maxclass"] not in ("panel", "comment")]
for mode in (0, 1):
    for engine in (0, 1, 2):
        for page in (0, 1):
            show = {"gen": mode == 0, "step": mode == 1, "synth": page == 0 and engine == 0,
                    "slice": page == 0 and engine == 1, "drum": page == 0 and engine == 2, "mod": page == 1}
            vis = [b for b in interactive if show.get(member_of.get(b.get("varname")), True)]
            for i in range(len(vis)):
                for j in range(i + 1, len(vis)):
                    a, c = vis[i]["presentation_rect"], vis[j]["presentation_rect"]
                    if a[0] < c[0] + c[2] - 0.5 and c[0] < a[0] + a[2] - 0.5 and a[1] < c[1] + c[3] - 0.5 and c[1] < a[1] + a[3] - 0.5:
                        errors.append("overlap in view mode %d engine %d page %d: %s %s / %s %s" % (mode, engine, page,
                                      vis[i]["maxclass"], vis[i].get("varname", ""), vis[j]["maxclass"], vis[j].get("varname", "")))
for e in sorted(set(errors)): print("ERROR", e)
print("%d objects, %d connections, %d parameters: %s" % (len(boxes), len(patch["lines"]), len(names), "OK" if not errors else "%d problems" % len(set(errors))))
sys.exit(1 if errors else 0)
