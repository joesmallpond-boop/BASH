// BASH! device preview (developer tool, no Max needed).
//   node max/preview.js <BASH!.maxpat> <javascript dir> <bash_ui.js> <messages.jsonl> <out.png> [page]
// Lays the device out from the patch's presentation rects, runs every jsui script with a
// stand-in for Max's mgraphics (drawing onto a canvas), replays the messages bash~ sent
// (from tools/DeviceSim) into them, and draws the live.* controls from their values.
// Fails loudly if a script throws or calls something Max's mgraphics doesn't have.
const fs = require("fs");
const vm = require("vm");
const path = require("path");
const { createCanvas } = require(process.env.CANVAS_MODULE || "@napi-rs/canvas");

const [patchFile, jsDir, uiJsFile, msgFile, outFile, pageArg, viewArg] = process.argv.slice(2);
const bigView = viewArg === "big";
const patch = JSON.parse(fs.readFileSync(patchFile, "utf8")).patcher;
const SCALE = 2;
let W = patch.devicewidth, H = 169;
const messages = fs.readFileSync(msgFile, "utf8").split("\n").filter(Boolean).map(l => JSON.parse(l));
const page = Number(pageArg || 0);

const rgba = (c, a) => `rgba(${Math.round(c[0] * 255)},${Math.round(c[1] * 255)},${Math.round(c[2] * 255)},${a === undefined ? (c[3] === undefined ? 1 : c[3]) : a})`;
// Live's light theme for the native controls (the jsui displays draw themselves)
const DEVICE = [0.76, 0.76, 0.76, 1], INK = [0.08, 0.08, 0.08, 1], QUIET = [0.33, 0.33, 0.33, 1];
const AMBER = [1.0, 0.75, 0.0, 1], CYAN = [0.12, 0.76, 0.88, 1];
const PANEL = [0.87, 0.87, 0.87, 1], ACTIVE = AMBER, RAISED = [0.36, 0.36, 0.36, 1];

// ---- a stand-in for Max's mgraphics on a 2D canvas ------------------------------------------
const MG_API = new Set(["init", "redraw", "size", "relative_coords", "autofill", "set_source_rgba", "rectangle",
    "rectangle_rounded", "arc", "move_to", "line_to", "set_line_width", "fill", "fill_preserve", "stroke",
    "stroke_preserve", "select_font_face", "set_font_size", "show_text", "text_measure", "save", "restore",
    "translate", "rotate", "scale", "set_line_cap", "set_line_join"]);
function makeMgraphics(w, h)
{
    const canvas = createCanvas(Math.max(1, Math.round(w * SCALE)), Math.max(1, Math.round(h * SCALE)));
    const ctx = canvas.getContext("2d");
    ctx.scale(SCALE, SCALE);
    let open = false, cx = 0, cy = 0, font = "Arial", fsize = 10;
    const begin = () => { if (!open) { ctx.beginPath(); open = true; } };
    const setFont = () => { ctx.font = `${/Bold|Black/.test(font) ? "bold " : ""}${fsize}px Arial`; };
    const mg = {
        size: [w, h], relative_coords: 0, autofill: 0,
        init() {}, redraw() {},
        set_source_rgba(r, g, b, a)
        {
            if (Array.isArray(r)) throw new Error("set_source_rgba called with an array: pass 4 numbers");
            if ([r, g, b, a].some(v => typeof v !== "number" || isNaN(v))) throw new Error("set_source_rgba needs 4 numbers: " + [r, g, b, a]);
            ctx.fillStyle = ctx.strokeStyle = `rgba(${r * 255},${g * 255},${b * 255},${a})`;
        },
        rectangle(x, y, rw, rh) { begin(); ctx.rect(x, y, rw, rh); },
        rectangle_rounded(x, y, rw, rh, ox) { begin(); ctx.roundRect(x, y, Math.max(0, rw), Math.max(0, rh), Math.min(ox, rw / 2, rh / 2) || 0); },
        arc(x, y, r, a0, a1) { begin(); ctx.moveTo(x + r * Math.cos(a0), y + r * Math.sin(a0)); ctx.arc(x, y, Math.max(0, r), a0, a1); },
        move_to(x, y) { begin(); ctx.moveTo(x, y); cx = x; cy = y; },
        line_to(x, y) { begin(); ctx.lineTo(x, y); cx = x; cy = y; },
        set_line_width(lw) { ctx.lineWidth = lw; },
        set_line_cap(c) { ctx.lineCap = c; }, set_line_join(j) { ctx.lineJoin = j; },
        fill() { ctx.fill(); open = false; },
        fill_preserve() { ctx.fill(); },
        stroke() { ctx.stroke(); open = false; },
        stroke_preserve() { ctx.stroke(); },
        select_font_face(f) { font = String(f); setFont(); },
        set_font_size(s) { fsize = s; setFont(); },
        show_text(t) { setFont(); const s = String(t); ctx.fillText(s, cx, cy); cx += ctx.measureText(s).width; open = false; },
        text_measure(t) { setFont(); return [ctx.measureText(String(t)).width, fsize]; },
        save() { ctx.save(); }, restore() { ctx.restore(); },
        translate(x, y) { ctx.translate(x, y); }, rotate(a) { ctx.rotate(a); }, scale(x, y) { ctx.scale(x, y); }
    };
    const proxy = new Proxy(mg, { get(t, k) { if (typeof k === "string" && !MG_API.has(k) && !(k in t)) throw new Error("mgraphics has no '" + k + "'"); return t[k]; } });
    return { canvas, mg: proxy };
}

// ---- run a jsui script ------------------------------------------------------------------------
const sent = [];
function loadScript(file, w, h, args)
{
    const { canvas, mg } = makeMgraphics(w, h);
    const ctxObj = {
        mgraphics: mg, Math, Date, String, Number, Array, Object, JSON, isNaN, parseInt, parseFloat,
        jsarguments: [file].concat(args || []), inlets: 1, outlets: 1, messagename: "",
        post: () => {}, outlet: (i, ...rest) => sent.push([path.basename(file)].concat(rest.flat())),
        arrayfromargs: (a) => Array.prototype.slice.call(a)
    };
    ctxObj.box = { rect: [0, 0, w, h] };
    const context = vm.createContext(ctxObj);
    vm.runInContext(fs.readFileSync(path.join(jsDir, file), "utf8"), context, { filename: file });
    return {
        file, canvas, context,
        send(sel, args)
        {
            context.messagename = sel;
            if (typeof context.anything === "function") context.anything.apply(context, args);
        },
        paint() { if (typeof context.paint === "function") context.paint(); },
        call(fn, ...a) { if (typeof context[fn] === "function") context[fn].apply(context, a); }
    };
}

// ---- which objects are showing --------------------------------------------------------------------
let boxes = patch.boxes.map(b => b.box);
const mainBoxes = boxes;
if (bigView)
{
    // the expanded window: the subpatcher EXPAND opens
    const sub = boxes.find(b => b.patcher && b.text && b.text.startsWith("p ")).patcher;
    boxes = sub.boxes.map(b => b.box);
    W = sub.rect[2]; H = sub.rect[3];
}
const byName = {};
mainBoxes.forEach(b => { if (b.varname) byName[b.varname] = b; b.hidden = 0; });
const ui = loadScript(path.relative(jsDir, uiJsFile), 1, 1);
ui.context.this = undefined;
// bash_ui.js uses this.patcher.getnamed(...)
vm.runInContext("var __patcher = { getnamed: function (n) { return __boxes[n] || null; } };", ui.context);
ui.context.__boxes = byName;
const uiSrc = fs.readFileSync(uiJsFile, "utf8").replace(/this\.patcher/g, "__patcher");
vm.runInContext(uiSrc, ui.context);

// parameter values: initial, then whatever bash~ said
const values = {};
mainBoxes.forEach(b => { const v = b.saved_attribute_attributes && b.saved_attribute_attributes.valueof; if (v && b.varname) values[b.varname] = v.parameter_initial ? v.parameter_initial[0] : 0; });
const menus = {};

// ---- run the displays ---------------------------------------------------------------------------------
const scripts = [];
for (const b of boxes)
    if (b.maxclass === "jsui")
    {
        const r = b.presentation_rect;
        scripts.push({ box: b, s: loadScript(b.filename, r[2], r[3], b.jsarguments) });
    }
ui.send("page", [page]);
for (const m of messages)
{
    const [sel, ...args] = m;
    if (sel === "pv" || sel === "ui") values[args[0]] = args[1];
    if (sel === "menu") menus[args[0]] = args.slice(1);
    if (sel === "pv") ui.send("pv", args);
    for (const { s } of scripts) s.send(sel, args);
}
// a few clicks, to exercise the mouse code
for (const { s } of scripts) { s.call("onclick", 10, 10, 1, 0, 0, 0, 0, 0); s.call("ondrag", 10, 4, 1, 0, 0, 0, 0, 0); s.call("ondrag", 10, 4, 0); s.call("ondblclick", 30, 40); }
for (const { s } of scripts) s.paint();

// ---- draw the device --------------------------------------------------------------------------------
const canvas = createCanvas(W * SCALE, H * SCALE);
const g = canvas.getContext("2d");
g.scale(SCALE, SCALE);
g.fillStyle = rgba(DEVICE); g.fillRect(0, 0, W, H);
const text = (t, x, y, size, color, align) => { g.font = `${size}px Arial`; g.fillStyle = rgba(color); g.textAlign = align || "left"; g.fillText(String(t), x, y); g.textAlign = "left"; };

function valueText(b, v)
{
    const p = b.saved_attribute_attributes.valueof;
    if (p.parameter_enum) return p.parameter_enum[Math.round(v)] || "";
    if (p.parameter_type === 1) return String(Math.round(v));
    const unit = p.parameter_unitstyle === 3 ? " Hz" : p.parameter_unitstyle === 2 ? " s" : p.parameter_unitstyle === 7 ? " st" : "";
    return (Math.abs(v) >= 100 ? v.toFixed(0) : v.toFixed(2)) + unit;
}

for (const b of boxes)
{
    if (!b.presentation || b.hidden) continue;
    const [x, y, w, h] = b.presentation_rect;
    const v = values[b.varname];
    switch (b.maxclass)
    {
        case "panel": g.fillStyle = rgba(b.bgcolor); g.beginPath(); g.roundRect(x, y, w, h, b.rounded || 0); g.fill(); break;
        case "comment": text(b.text, x + 2, y + 10, b.fontsize || 9, b.textcolor || QUIET); break;
        case "jsui": { const s = scripts.find(q => q.box === b).s; g.drawImage(s.canvas, x, y, w, h); break; }
        case "live.dial":
        {
            const p = b.saved_attribute_attributes.valueof;
            const norm = Math.max(0, Math.min(1, Math.pow(((v ?? 0) - p.parameter_mmin) / (p.parameter_mmax - p.parameter_mmin), 1 / (p.parameter_exponent || 1))));
            // Live's knob: name above, a grey track with a cyan arc and a needle, the value below
            text(p.parameter_shortname, x + w / 2, y + 9, 8.5, INK, "center");
            const cxx = x + w / 2, cyy = y + 25, r = 10;
            g.lineCap = "butt";
            g.lineWidth = 2.6; g.strokeStyle = rgba(RAISED); g.beginPath(); g.arc(cxx, cyy, r, Math.PI * 0.75, Math.PI * 2.25); g.stroke();
            g.strokeStyle = rgba(CYAN); g.beginPath(); g.arc(cxx, cyy, r, Math.PI * 0.75, Math.PI * (0.75 + 1.5 * norm)); g.stroke();
            const a = Math.PI * (0.75 + 1.5 * norm);
            g.strokeStyle = rgba(INK); g.lineWidth = 1.6; g.beginPath(); g.moveTo(cxx, cyy); g.lineTo(cxx + Math.cos(a) * (r - 1), cyy + Math.sin(a) * (r - 1)); g.stroke();
            text(valueText(b, v ?? 0), x + w / 2, y + h - 4, 8.5, INK, "center");
            break;
        }
        case "live.menu": case "live.numbox": case "umenu":
        {
            g.fillStyle = rgba(PANEL); g.fillRect(x, y, w, h);
            g.strokeStyle = rgba([0.55, 0.55, 0.55, 1]); g.lineWidth = 1; g.strokeRect(x + 0.5, y + 0.5, w - 1, h - 1);
            let t = b.maxclass === "umenu" ? ((menus[b.varname] || [])[0] || (b.items || [""])[0]) : valueText(b, v ?? 0);
            if (b.maxclass === "umenu")
            {
                // which menu is this? the router order: loops, kits, sounds, grooves
                const which = { loops: "loops", kits: "kits", sounds: "sounds", grooves: "grooves" };
                t = "choose...";
            }
            g.save(); g.beginPath(); g.rect(x, y, w - 8, h); g.clip();
            text(t, x + 4, y + h - 4.5, 8.5, INK); g.restore();
            g.fillStyle = rgba(QUIET); g.beginPath(); g.moveTo(x + w - 9, y + h / 2 - 1.5); g.lineTo(x + w - 3, y + h / 2 - 1.5); g.lineTo(x + w - 6, y + h / 2 + 2); g.fill();
            break;
        }
        case "live.tab":
        {
            const items = b.saved_attribute_attributes.valueof.parameter_enum;
            const sw = w / items.length;
            items.forEach((it, i) =>
            {
                const on = Math.round(b.varname === "view" ? page : (v ?? 0)) === i;
                g.fillStyle = rgba(on ? ACTIVE : PANEL); g.fillRect(x + i * sw, y, sw, h);
                g.strokeStyle = rgba([0.55, 0.55, 0.55, 1]); g.lineWidth = 1; g.strokeRect(x + i * sw + 0.5, y + 0.5, sw - 1, h - 1);
                text(it, x + i * sw + sw / 2, y + h - 4.5, 8.5, INK, "center");
            });
            break;
        }
        case "live.text":
        {
            const on = (v ?? 0) > 0.5;
            g.fillStyle = rgba(on ? ACTIVE : PANEL); g.fillRect(x, y, w, h);
            g.strokeStyle = rgba([0.55, 0.55, 0.55, 1]); g.lineWidth = 1; g.strokeRect(x + 0.5, y + 0.5, w - 1, h - 1);
            text(b.text, x + w / 2, y + h - 4.5, 8.5, INK, "center");
            break;
        }
        case "live.drop":
            g.setLineDash([3, 3]); g.strokeStyle = rgba(QUIET); g.strokeRect(x + 0.5, y + 0.5, w - 1, h - 1); g.setLineDash([]);
            text(b.legend, x + w / 2, y + h / 2 + 3, 8, QUIET, "center");
            break;
        case "live.meter~":
            for (let i = 0; i < 2; i++) { g.fillStyle = rgba(PANEL); g.fillRect(x + 6 + i * 12, y, 9, h); g.fillStyle = rgba([0.35, 0.8, 0.3, 1]); g.fillRect(x + 6 + i * 12, y + h * 0.35, 9, h * 0.65); }
            break;
        default: break;
    }
}
fs.writeFileSync(outFile, canvas.toBuffer("image/png"));
console.log("preview", outFile, "-", scripts.length, "displays,", sent.length, "messages from clicks:", JSON.stringify(sent.slice(0, 12)));
