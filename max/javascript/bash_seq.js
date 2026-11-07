// BASH! sequencer display.
// Generative mode: the pattern as bars (height = pitch / pad / slice), accents bright,
// slides joined, locked steps marked; the playhead step glows and pops.
//   click a step: mute / unmute it        shift- or right-click: lock / unlock it
// Step mode: 8 lanes at a time (the selected lane's page). Steps light up as they play.
//   click: on / off     drag up / down: velocity     shift-click: ratchet (x2 x3 x4)
//   alt-click: condition (fill, 1:2 ...)      cmd- or right-click: probability
//   click a lane name: select it       shift-click a lane name: mute it
// Messages in: seq, lane, lanesel, frame, lanepos, hits, pv
inlets = 1;
outlets = 1;
mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

// colours: Live's theme when Max can tell us (max.getcolor), else Live's default dark display
var C = { bg: [0.11, 0.11, 0.11, 1], ink: [0.86, 0.86, 0.86, 1], quiet: [0.5, 0.5, 0.5, 1],
          amber: [1.0, 0.66, 0.18, 1], alt: [0.36, 0.84, 0.95, 1], line: [0.24, 0.24, 0.24, 1] };
var FONT = "Ableton Sans Medium", FONT_BOLD = "Ableton Sans Bold";
var THEME = [["bg", "live_lcd_bg"], ["ink", "live_lcd_title"], ["amber", "live_lcd_control_fg"],
             ["alt", "live_lcd_control_fg_alt"], ["line", "live_lcd_frame"]];
function mix(a, b, t) { return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t, 1]; }
function derive()
{
    C.panel = mix(C.bg, C.ink, 0.07); C.raised = mix(C.bg, C.ink, 0.13);
    C.amberDim = mix(C.bg, C.amber, 0.5); C.active = mix(C.bg, C.amber, 0.22); C.quiet = mix(C.bg, C.ink, 0.5);
}
derive();
// The Decompression Tapes mark: a thin rounded chevron trailing a bold one.
// dir: 0 right, 1 left, 2 up. Drawn in a size x size box centred on (cx, cy).
function drawMark(cx, cy, size, dir, c, alpha)
{
    var g = mgraphics;
    g.save();
    g.translate(cx, cy);
    g.rotate(dir === 1 ? Math.PI : dir === 2 ? -Math.PI / 2 : 0);
    try { g.set_line_cap("round"); g.set_line_join("round"); } catch (e) {}
    col(c, alpha);
    var s = size;
    g.set_line_width(Math.max(1, s * 0.17));
    g.move_to(-0.08 * s, -0.42 * s); g.line_to(0.34 * s, 0); g.line_to(-0.08 * s, 0.42 * s);
    g.stroke();
    g.set_line_width(Math.max(0.8, s * 0.1));
    g.move_to(-0.42 * s, -0.3 * s); g.line_to(-0.12 * s, 0); g.line_to(-0.42 * s, 0.3 * s);
    g.stroke();
    g.restore();
}
function refreshTheme()
{
    var changed = false;
    for (var i = 0; i < THEME.length; i++)
    {
        try
        {
            var c = max.getcolor(THEME[i][1]);
            if (c && c.length >= 3) { C[THEME[i][0]] = [c[0], c[1], c[2], 1]; changed = true; }
        }
        catch (e) { return; }
    }
    if (changed) derive();
}
function col(c, a) { mgraphics.set_source_rgba(c[0], c[1], c[2], a === undefined ? c[3] : a); }
var condNames = ["", "F", "!F", "1st", "!1", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4"];
var NAMEW = 58, ROWS = 8;
// the expanded window is tall enough for all 16 lanes and wider lane names
function fit() { var sz = mgraphics.size; ROWS = sz[1] >= 150 ? 16 : 8; NAMEW = sz[0] >= 700 ? 120 : 58; }

var mode = 0, engine = 0;
var seqLen = 16, seqSteps = [];          // [active, value, accent, slide, locked]
var lanes = [];                          // per lane: {name, len, mute, steps[64]}
var lanePos = [], lastHits = [], laneFlash = [], selLane = 0;
var playIndex = -1, running = 0, lastPlay = -1, pop = 0;
var drag = null;

for (var i = 0; i < 16; i++) { lanes.push({ name: "", len: 16, mute: 0, steps: [] }); lanePos.push(-1); laneFlash.push(0); }

function size() { return mgraphics.size; }
function page() { return ROWS >= 16 ? 0 : (selLane >= ROWS ? 1 : 0); }

function anything()
{
    var a = arrayfromargs(arguments);
    switch (messagename)
    {
        case "pv":
            if (a[0] === "mode") { mode = a[1]; mgraphics.redraw(); }
            if (a[0] === "engine") { engine = a[1]; mgraphics.redraw(); }
            break;
        case "seq":
            seqLen = a[0];
            seqSteps = [];
            for (var s = 0; s < seqLen; s++)
            {
                var b = 2 + s * 5;
                seqSteps.push([a[b], a[b + 1], a[b + 2], a[b + 3], a[b + 4]]);
            }
            mgraphics.redraw();
            break;
        case "lane":
            var l = a[0];
            if (l < 0 || l > 15) break;
            var steps = [];
            for (var k = 0; k < 64; k++) steps.push(a[10 + k] || 0);
            lanes[l] = { name: String(a[1]), len: a[2], speed: a[3], mute: a[4], algo: a[5], steps: steps };
            mgraphics.redraw();
            break;
        case "lanesel":
            selLane = a[0];
            mgraphics.redraw();
            break;
        case "lanepos":
            lanePos = a;
            break;
        case "hits":
            for (var h = 0; h < a.length && h < 16; h++)
                if (lastHits.length === a.length && a[h] !== lastHits[h]) laneFlash[h] = 1;
            lastHits = a;
            break;
        case "frame":
            running = a[0];
            playIndex = running ? a[3] : -1;
            if (playIndex !== lastPlay) { pop = 1; lastPlay = playIndex; }
            pop *= 0.7;
            for (var f = 0; f < 16; f++) laneFlash[f] *= 0.78;
            mgraphics.redraw();
            break;
        default:
            break;
    }
}

// ---- drawing -------------------------------------------------------------------------
function paint()
{
    refreshTheme();
    fit();
    var w = size()[0], h = size()[1];
    col(C.bg);
    mgraphics.rectangle_rounded(0, 0, w, h, 6, 6);
    mgraphics.fill();
    if (mode === 0) paintGenerative(w, h); else paintSteps(w, h);
    if (w >= 700) drawMark(w - 22, h - 18, 18, 0, C.ink, 0.07);   // the expanded view: a quiet signature
}

function paintGenerative(w, h)
{
    var n = Math.max(1, seqLen);
    var x0 = 4, y0 = 4, gw = w - 8, gh = h - 12;
    var cw = gw / n;
    var lo = 1000, hi = -1000;
    for (var i = 0; i < seqSteps.length; i++)
        if (seqSteps[i][0]) { lo = Math.min(lo, seqSteps[i][1]); hi = Math.max(hi, seqSteps[i][1]); }
    if (lo > hi) { lo = 0; hi = 1; }
    if (hi - lo < 6) { lo -= 3; hi += 3; }

    // beat lines
    for (var b = 0; b <= n; b++)
    {
        col(b % 4 === 0 ? C.line : C.panel);
        mgraphics.rectangle(x0 + b * cw, y0, 1, gh);
        mgraphics.fill();
    }
    var play = playIndex >= 0 ? playIndex % n : -1;
    var prev = null;
    for (var s = 0; s < seqSteps.length; s++)
    {
        var st = seqSteps[s];
        var x = x0 + s * cw;
        if (s === play)
        {
            col(C.active);
            mgraphics.rectangle(x, y0, cw, gh);
            mgraphics.fill();
        }
        if (! st[0]) { prev = null; continue; }
        var frac = (st[1] - lo) / (hi - lo);
        var bh = 6 + frac * (gh - 10);
        var grow = s === play ? pop * 4 : 0;
        var y = y0 + gh - bh - grow;
        col(st[2] ? C.amber : C.amberDim, s === play ? 1 : (st[2] ? 0.95 : 0.85));
        mgraphics.rectangle_rounded(x + 1, y, Math.max(2, cw - 2), bh + grow, 3, 3);
        mgraphics.fill();
        if (st[3] && prev !== null)
        {
            // slide: a bridge from the previous note
            col(C.amber, 0.6);
            mgraphics.set_line_width(1.5);
            mgraphics.move_to(prev[0], prev[1]);
            mgraphics.line_to(x + cw * 0.5, y + 2);
            mgraphics.stroke();
        }
        if (st[4])
        {
            col(C.ink);
            mgraphics.rectangle(x + cw * 0.5 - 1.5, y0 + gh + 2, 3, 3);
            mgraphics.fill();
        }
        prev = [x + cw * 0.5, y + 2];
    }
}

function paintSteps(w, h)
{
    var first = page() * ROWS;
    var maxLen = 1;
    for (var l = first; l < first + ROWS; l++) maxLen = Math.max(maxLen, lanes[l].len || 16);
    var gx = NAMEW + 2, gw = w - gx - 4, rh = (h - 4) / ROWS;
    var cw = gw / maxLen;
    mgraphics.select_font_face(FONT);
    mgraphics.set_font_size(8.5);
    for (var r = 0; r < ROWS; r++)
    {
        var lane = first + r;
        var L = lanes[lane];
        var y = 2 + r * rh;
        // name
        col(lane === selLane ? C.active : C.panel);
        mgraphics.rectangle(2, y + 0.5, NAMEW - 2, rh - 1);
        mgraphics.fill();
        if (laneFlash[lane] > 0.05)
        {
            col(C.amber, laneFlash[lane] * 0.6);
            mgraphics.rectangle(2, y + 0.5, (NAMEW - 2) * laneFlash[lane], rh - 1);
            mgraphics.fill();
        }
        col(L.mute ? C.quiet : (lane === selLane ? C.amber : C.ink), L.mute ? 0.6 : 1);
        mgraphics.move_to(5, y + rh - 2.5);
        var nm = (lane + 1) + " " + L.name;
        while (nm.length > 1 && mgraphics.text_measure(nm)[0] > NAMEW - 6) nm = nm.substr(0, nm.length - 1);
        mgraphics.show_text(nm);
        // steps
        var pos = lanePos[lane] !== undefined ? lanePos[lane] : -1;
        for (var s = 0; s < maxLen; s++)
        {
            var x = gx + s * cw;
            var inLane = s < L.len;
            var v = L.steps[s] || 0;
            var on = v & 1;
            col(! inLane ? C.bg : (s % 4 === 0 ? C.raised : C.panel));
            mgraphics.rectangle(x + 0.5, y + 0.5, cw - 1, rh - 1);
            mgraphics.fill();
            if (inLane && on)
            {
                var vel = (v >> 1) & 127, rat = ((v >> 8) & 7) + 1, prob = (v >> 11) & 3, cond = (v >> 13) & 15;
                var a = (0.35 + 0.65 * vel / 127) * (prob === 0 ? 1 : 0.55);
                var hit = s === pos && running;
                col(L.mute ? C.quiet : (hit ? C.ink : C.amber), hit ? 1 : a);
                var inset = hit ? 0 : 1;
                mgraphics.rectangle(x + 0.5 + inset, y + 0.5 + inset, cw - 1 - 2 * inset, rh - 1 - 2 * inset);
                mgraphics.fill();
                if (rat > 1 && cw > 6)
                    for (var t = 1; t < rat; t++)
                    {
                        col(C.bg, 0.8);
                        mgraphics.rectangle(x + t * (cw / rat), y + 1, 1, rh - 2);
                        mgraphics.fill();
                    }
                if (cond > 0 && cw > 12)
                {
                    col(C.bg);
                    mgraphics.set_font_size(7);
                    mgraphics.move_to(x + 2, y + rh - 2);
                    mgraphics.show_text(condNames[cond] || "");
                    mgraphics.set_font_size(8.5);
                }
            }
            else if (inLane && s === pos && running)
            {
                col(C.amber, 0.25);
                mgraphics.rectangle(x + 0.5, y + 0.5, cw - 1, rh - 1);
                mgraphics.fill();
            }
        }
    }
}

// ---- mouse ------------------------------------------------------------------------------------
function hitStep(x, y)
{
    fit();
    var w = size()[0], h = size()[1];
    if (mode === 0)
    {
        var n = Math.max(1, seqLen);
        var i = Math.floor((x - 4) / ((w - 8) / n));
        return i >= 0 && i < n ? { step: i } : null;
    }
    var first = page() * ROWS, rh = (h - 4) / ROWS;
    var r = Math.floor((y - 2) / rh);
    if (r < 0 || r >= ROWS) return null;
    var lane = first + r;
    if (x < NAMEW) return { lane: lane, name: true };
    var maxLen = 1;
    for (var l = first; l < first + ROWS; l++) maxLen = Math.max(maxLen, lanes[l].len || 16);
    var cw = (w - NAMEW - 6) / maxLen;
    var s = Math.floor((x - NAMEW - 2) / cw);
    if (s < 0 || s >= (lanes[lane].len || 16)) return null;
    return { lane: lane, step: s };
}

function onclick(x, y, but, cmd, shift, capslock, option, ctrl)
{
    var t = hitStep(x, y);
    drag = null;
    if (t === null) return;
    if (mode === 0)
    {
        if (shift || ctrl) outlet(0, "lock", t.step);
        else outlet(0, "mute", t.step);
        return;
    }
    if (t.name)
    {
        if (shift) outlet(0, "lane", "mute", t.lane);
        else outlet(0, "lane", "select", t.lane);
        selLane = t.lane;
        mgraphics.redraw();
        return;
    }
    if (shift)              outlet(0, "step", t.lane, t.step, "ratchet");
    else if (option)        outlet(0, "step", t.lane, t.step, "cond");
    else if (cmd || ctrl)   outlet(0, "step", t.lane, t.step, "prob");
    else
    {
        var v = lanes[t.lane].steps[t.step] || 0;
        drag = { lane: t.lane, step: t.step, y0: y, vel: (v & 1) ? ((v >> 1) & 127) : 100, moved: false };
    }
}

function ondrag(x, y, but, cmd, shift, capslock, option, ctrl)
{
    if (drag === null) return;
    if (but === 0)
    {
        // mouse up: a click without a drag toggles the step
        if (! drag.moved) outlet(0, "step", drag.lane, drag.step, "toggle");
        drag = null;
        return;
    }
    var dy = drag.y0 - y;
    if (Math.abs(dy) > 2 || drag.moved)
    {
        drag.moved = true;
        var v = Math.max(1, Math.min(127, Math.round(drag.vel + dy * 3)));
        outlet(0, "step", drag.lane, drag.step, "vel", v);
    }
}

function onresize() { mgraphics.redraw(); }
