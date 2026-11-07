// BASH! sample tempo: the loaded sample's tempo (found from its name or by listening).
// Drag the number to correct it (shift: fine), click / 2 or x 2 to halve or double it.
inlets = 1;
outlets = 1;
mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

var C = {
    bg: [0.114, 0.114, 0.114, 1], panel: [0.145, 0.145, 0.145, 1], ink: [0.914, 0.902, 0.875, 1],
    quiet: [0.545, 0.541, 0.525, 1], amber: [1.0, 0.722, 0.0, 1]
};
var bpm = 0, ratio = 1, drag = null;

function col(c, a) { mgraphics.set_source_rgba(c[0], c[1], c[2], a === undefined ? c[3] : a); }

function anything()
{
    var a = arrayfromargs(arguments);
    if (messagename === "wave") { bpm = a[1]; ratio = a[2]; mgraphics.redraw(); }
}

function paint()
{
    var w = mgraphics.size[0], h = mgraphics.size[1];
    col(C.bg);
    mgraphics.rectangle_rounded(0, 0, w, h, 5, 5);
    mgraphics.fill();
    mgraphics.select_font_face("Arial");
    col(C.quiet);
    mgraphics.set_font_size(7.5);
    mgraphics.move_to(4, 9);
    mgraphics.show_text("SAMPLE BPM");
    col(bpm > 0 ? C.amber : C.quiet);
    mgraphics.set_font_size(15);
    mgraphics.move_to(4, 27);
    mgraphics.show_text(bpm > 0 ? (Math.round(bpm * 10) / 10).toFixed(1) : "--");
    var bw = (w - 10) / 2, by = h - 16;
    for (var i = 0; i < 2; i++)
    {
        col(C.panel);
        mgraphics.rectangle_rounded(4 + i * (bw + 2), by, bw, 13, 3, 3);
        mgraphics.fill();
        col(C.ink);
        mgraphics.set_font_size(8.5);
        mgraphics.move_to(4 + i * (bw + 2) + bw / 2 - 7, by + 9.5);
        mgraphics.show_text(i === 0 ? "/ 2" : "x 2");
    }
}

function onclick(x, y, but, cmd, shift)
{
    var w = mgraphics.size[0], h = mgraphics.size[1];
    drag = null;
    if (bpm <= 0) return;
    if (y >= h - 16) { outlet(0, "srcbpm", x < w / 2 ? "half" : "double"); return; }
    drag = { y0: y, start: bpm, fine: shift };
}

function ondrag(x, y, but, cmd, shift)
{
    if (drag === null || but === 0) { drag = null; return; }
    var v = drag.start + (drag.y0 - y) * (shift ? 0.05 : 0.5);
    bpm = Math.max(40, Math.min(250, Math.round(v * 10) / 10));
    outlet(0, "srcbpm", bpm);
    mgraphics.redraw();
}

function onresize() { mgraphics.redraw(); }
