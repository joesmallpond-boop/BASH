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

var C = {
    bg: [0.114, 0.114, 0.114, 1], panel: [0.145, 0.145, 0.145, 1], raised: [0.18, 0.18, 0.18, 1],
    line: [0.227, 0.227, 0.227, 1], ink: [0.914, 0.902, 0.875, 1], quiet: [0.545, 0.541, 0.525, 1],
    amber: [1.0, 0.722, 0.0, 1], amberDim: [0.541, 0.396, 0.031, 1], active: [0.29, 0.224, 0.063, 1]
};
var condNames = ["", "F", "!F", "1st", "!1", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4"];
var NAMEW = 58, ROWS = 8;

var mode = 0, engine = 0;
var seqLen = 16, seqSteps = [];          // [active, value, accent, slide, locked]
var lanes = [];                          // per lane: {name, len, mute, steps[64]}
var lanePos = [], lastHits = [], laneFlash = [], selLane = 0;
var playIndex = -1, running = 0, lastPlay = -1, pop = 0;
var drag = null;

for (var i = 0; i < 16; i++) { lanes.push({ name: "", len: 16, mute: 0, steps: [] }); lanePos.push(-1); laneFlash.push(0); }

function col(c, a) { mgraphics.set_source_rgba(c[0], c[1], c[2], a === undefined ? c[3] : a); }
function size() { return mgraphics.size; }
function page() { return selLane >= ROWS ? 1 : 0; }

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
    var w = size()[0], h = size()[1];
    col(C.bg);
    mgraphics.rectangle_rounded(0, 0, w, h, 6, 6);
    mgraphics.fill();
    if (mode === 0) paintGenerative(w, h); else paintSteps(w, h);
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
    mgraphics.select_font_face("Arial");
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
        mgraphics.show_text(nm.length > 11 ? nm.substr(0, 11) : nm);
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
    // page marker
    col(C.quiet);
    mgraphics.set_font_size(7);
    mgraphics.move_to(NAMEW - 18, h - 1);
}

// ---- mouse ------------------------------------------------------------------------------------
function hitStep(x, y)
{
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
