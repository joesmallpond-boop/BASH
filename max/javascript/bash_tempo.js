// BASH! sample tempo: the loaded sample's tempo (found from its name or by listening).
// Drag the number to correct it (shift: fine), click / 2 or x 2 to halve or double it.
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
var bpm = 0, ratio = 1, drag = null;


function anything()
{
    var a = arrayfromargs(arguments);
    if (messagename === "wave") { bpm = a[1]; ratio = a[2]; mgraphics.redraw(); }
}

function paint()
{
    refreshTheme();
    var w = mgraphics.size[0], h = mgraphics.size[1];
    col(C.bg);
    mgraphics.rectangle_rounded(0, 0, w, h, 5, 5);
    mgraphics.fill();
    mgraphics.select_font_face(FONT);
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
