// BASH! slicer waveform. Top strip: TRANSIENTS (drag the % for sensitivity), GRID and
// RANDOM (drag the number for how many), CLEAR. Below: the sample with its slices; the
// play window is shaded, the playing slice flashes and a playhead runs through it.
//   click a slice: play it          drag a marker: move it        double-click: add a slice
//   right- / ctrl-click a marker: delete it              click with no sample: browse
// Messages in: wave, slices, frame, hits, pv (winstart / winsize)
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
var STRIP = 15;
var frames = 0, srcBpm = 0, ratio = 1, name = "drop a sample", bins = [];
var slices = [], winStart = 1, winSize = 4;
var playSlice = -1, playPos = -1, flash = 0, lastHits = [], frameCount = 0;
var sens = 50, count = 16;
var buttons = [], drag = null;


function anything()
{
    var a = arrayfromargs(arguments);
    switch (messagename)
    {
        case "wave":
            frames = a[0]; srcBpm = a[1]; ratio = a[2]; name = String(a[3]);
            bins = a.slice(4);
            mgraphics.redraw();
            break;
        case "slices":
            slices = a;
            mgraphics.redraw();
            break;
        case "pv":
            if (a[0] === "winstart") { winStart = a[1]; mgraphics.redraw(); }
            if (a[0] === "winsize") { winSize = a[1]; mgraphics.redraw(); }
            break;
        case "hits":
            var any = false;
            for (var k = 0; k < a.length; k++) if (lastHits.length === a.length && a[k] !== lastHits[k]) any = true;
            if (any) flash = 1;
            lastHits = a;
            break;
        case "frame":
            playSlice = a[4]; playPos = a[5];
            frameCount++;
            flash *= 0.8;
            mgraphics.redraw();
            break;
        default:
            break;
    }
}

function waveArea(w, h) { return { x: 2, y: STRIP + 3, w: w - 4, h: h - STRIP - 5 }; }

function paint()
{
    refreshTheme();
    var w = mgraphics.size[0], h = mgraphics.size[1];
    col(C.bg);
    mgraphics.rectangle_rounded(0, 0, w, h, 6, 6);
    mgraphics.fill();
    paintStrip(w);

    var A = waveArea(w, h);
    col(C.panel);
    mgraphics.rectangle_rounded(A.x, A.y, A.w, A.h, 4, 4);
    mgraphics.fill();
    mgraphics.select_font_face(FONT);

    if (frames <= 0)
    {
        col(C.quiet);
        mgraphics.set_font_size(11);
        var msg = name;
        var tw = mgraphics.text_measure(msg)[0];
        mgraphics.move_to(A.x + (A.w - tw) / 2, A.y + A.h / 2 + 4);
        mgraphics.show_text(msg);
        // a sleepy flat line that breathes
        col(C.amberDim, 0.6);
        var yy = A.y + A.h / 2 + 14 + Math.sin(frameCount * 0.08) * 2;
        mgraphics.rectangle(A.x + 20, yy, A.w - 40, 1);
        mgraphics.fill();
        return;
    }

    // the play window (slices winStart .. winStart + winSize - 1)
    var ws = winStart - 1, we = Math.min(slices.length, ws + winSize);
    if (slices.length > 0 && ws < slices.length)
    {
        var x1 = A.x + slices[ws] * A.w, x2 = A.x + (we < slices.length ? slices[we] : 1) * A.w;
        col(C.active, 0.7);
        mgraphics.rectangle(x1, A.y, x2 - x1, A.h);
        mgraphics.fill();
    }
    // the playing slice flashes
    if (playSlice >= 0 && playSlice < slices.length && flash > 0.05)
    {
        var p1 = A.x + slices[playSlice] * A.w, p2 = A.x + (playSlice + 1 < slices.length ? slices[playSlice + 1] : 1) * A.w;
        col(C.amber, 0.25 * flash);
        mgraphics.rectangle(p1, A.y, p2 - p1, A.h);
        mgraphics.fill();
    }
    // waveform
    var mid = A.y + A.h / 2;
    var n = bins.length;
    for (var i = 0; i < n; i++)
    {
        var bx = A.x + i * A.w / n;
        var bh = Math.max(0.5, bins[i] * (A.h / 2 - 2));
        var inPlay = playSlice >= 0 && playPos >= 0 && Math.abs(i / n - playPos) < 0.01;
        col(inPlay ? C.amber : C.ink, inPlay ? 1 : 0.55);   // Live-style grey waveform, the playing spot lit
        mgraphics.rectangle(bx, mid - bh, Math.max(1, A.w / n - 0.3), bh * 2);
        mgraphics.fill();
    }
    // slice markers
    mgraphics.set_font_size(7.5);
    for (var s = 0; s < slices.length; s++)
    {
        var mx = A.x + slices[s] * A.w;
        col(s === playSlice ? C.amber : C.ink, s === playSlice ? 1 : 0.55);
        mgraphics.rectangle(mx, A.y, 1, A.h);
        mgraphics.fill();
        if (slices.length <= 48)
        {
            mgraphics.move_to(mx + 2, A.y + 8);
            mgraphics.show_text(String(s + 1));
        }
    }
    // playhead
    if (playPos >= 0)
    {
        col(C.amber);
        mgraphics.rectangle(A.x + playPos * A.w - 1, A.y, 2, A.h);
        mgraphics.fill();
    }
    // name + tempo
    mgraphics.set_font_size(8.5);
    col(C.quiet);
    var info = name + "  " + (Math.round(srcBpm * 10) / 10) + " bpm" + (Math.abs(ratio - 1) > 0.001 ? "  warped" : "") + "  " + slices.length + " slices";
    var iw = mgraphics.text_measure(info)[0];
    mgraphics.move_to(A.x + A.w - iw - 4, A.y + A.h - 4);
    mgraphics.show_text(info);
}

function paintStrip(w)
{
    var defs = [
        { id: "transients", label: "TRANSIENTS", w: 70 },
        { id: "sens", label: sens + "%", w: 34, drag: true },
        { id: "grid", label: "GRID", w: 38 },
        { id: "random", label: "RANDOM", w: 50 },
        { id: "count", label: String(count), w: 30, drag: true },
        { id: "clear", label: "CLEAR", w: 42 },
        { id: "browse", label: "LOAD", w: 36 }
    ];
    buttons = [];
    var x = 2;
    mgraphics.select_font_face(FONT);
    mgraphics.set_font_size(8.5);
    for (var i = 0; i < defs.length; i++)
    {
        var d = defs[i];
        var b = { id: d.id, x: x, y: 1, w: d.w, h: STRIP, drag: d.drag };
        buttons.push(b);
        col(d.drag ? C.raised : C.panel);
        mgraphics.rectangle_rounded(b.x, b.y, b.w, b.h, 3, 3);
        mgraphics.fill();
        col(d.drag ? C.amber : C.ink);
        var tw = mgraphics.text_measure(d.label)[0];
        mgraphics.move_to(b.x + (b.w - tw) / 2, b.y + 11);
        mgraphics.show_text(d.label);
        x += d.w + 2;
    }
}

function markerNear(x, A)
{
    var best = -1, bd = 5;
    for (var s = 1; s < slices.length; s++)
    {
        var d = Math.abs(A.x + slices[s] * A.w - x);
        if (d < bd) { bd = d; best = s; }
    }
    return best;
}

function sliceAt(frac)
{
    var i = 0;
    for (var s = 0; s < slices.length; s++) if (slices[s] <= frac) i = s;
    return i;
}

function onclick(x, y, but, cmd, shift, capslock, option, ctrl)
{
    var w = mgraphics.size[0], h = mgraphics.size[1];
    drag = null;
    if (y <= STRIP + 1)
    {
        for (var i = 0; i < buttons.length; i++)
        {
            var b = buttons[i];
            if (x < b.x || x >= b.x + b.w) continue;
            if (b.drag) { drag = { id: b.id, y0: y, start: b.id === "sens" ? sens : count }; return; }
            if (b.id === "transients") outlet(0, "slices", "transients", sens / 100);
            else if (b.id === "grid") outlet(0, "slices", "grid", count);
            else if (b.id === "random") outlet(0, "slices", "random", count);
            else if (b.id === "clear") outlet(0, "slices", "clear");
            else if (b.id === "browse") outlet(0, "browse");
            return;
        }
        return;
    }
    if (frames <= 0) { outlet(0, "browse"); return; }
    var A = waveArea(w, h);
    var m = markerNear(x, A);
    if (m > 0 && (ctrl || cmd)) { outlet(0, "slice", "delete", m); return; }
    if (m > 0) { drag = { id: "marker", index: m }; return; }
    var frac = Math.max(0, Math.min(1, (x - A.x) / A.w));
    outlet(0, "slice", "play", sliceAt(frac));
}

function ondblclick(x, y)
{
    var w = mgraphics.size[0], h = mgraphics.size[1];
    if (y <= STRIP + 1 || frames <= 0) return;
    var A = waveArea(w, h);
    if (markerNear(x, A) > 0) return;
    outlet(0, "slice", "add", Math.max(0, Math.min(1, (x - A.x) / A.w)));
}

function ondrag(x, y, but)
{
    if (drag === null) return;
    if (but === 0) { drag = null; return; }
    var w = mgraphics.size[0], h = mgraphics.size[1];
    if (drag.id === "marker")
    {
        var A = waveArea(w, h);
        var frac = Math.max(0, Math.min(1, (x - A.x) / A.w));
        slices[drag.index] = frac;   // feels immediate; the real value comes back from bash~
        outlet(0, "slice", "move", drag.index, frac);
        mgraphics.redraw();
        return;
    }
    var dy = drag.y0 - y;
    if (drag.id === "sens") sens = Math.max(0, Math.min(100, Math.round(drag.start + dy)));
    if (drag.id === "count") count = Math.max(2, Math.min(256, Math.round(drag.start + dy * 0.5)));
    mgraphics.redraw();
}

function onresize() { mgraphics.redraw(); }
