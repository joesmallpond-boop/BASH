// BASH! LFO scope: the shape, a dot riding it, and a fading trail of where it's been.
// jsarguments: 1 or 2 (which LFO; 3 = Life). Messages in: frame, pv
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
var which = jsarguments.length > 1 ? jsarguments[1] : 1;
var shape = which === 2 ? 5 : 0, value = 0, trail = [];
var shapeNames = ["SINE", "TRI", "SAW", "SQUARE", "S&H", "SMOOTH"];


function anything()
{
    var a = arrayfromargs(arguments);
    if (messagename === "pv" && a[0] === "lfo" + which + "shape") { shape = a[1]; mgraphics.redraw(); }
    if (messagename === "frame")
    {
        value = which === 1 ? a[7] : which === 2 ? a[8] : a[9];
        trail.push(value);
        if (trail.length > 60) trail.shift();
        mgraphics.redraw();
    }
}

function shapeAt(p)
{
    switch (shape)
    {
        case 0: return Math.sin(p * Math.PI * 2);
        case 1: return p < 0.5 ? p * 4 - 1 : 3 - p * 4;
        case 2: return p * 2 - 1;
        case 3: return p < 0.5 ? 1 : -1;
        case 4: return [0.6, -0.3, 0.9, -0.8, 0.1, -0.5, 0.4, -0.9][Math.floor(p * 8) % 8];
        default: return Math.sin(p * Math.PI * 2) * 0.6 + Math.sin(p * Math.PI * 6) * 0.3;
    }
}

function paint()
{
    refreshTheme();
    var w = mgraphics.size[0], h = mgraphics.size[1];
    col(C.bg);
    mgraphics.rectangle_rounded(0, 0, w, h, 5, 5);
    mgraphics.fill();
    var mid = h / 2, amp = h / 2 - 5;
    col(C.line);
    mgraphics.rectangle(3, mid, w - 6, 1);
    mgraphics.fill();
    if (which !== 3)
    {
        col(C.amberDim);
        mgraphics.set_line_width(1);
        for (var i = 0; i <= 40; i++)
        {
            var x = 3 + (w - 6) * i / 40, y = mid - shapeAt(i / 40) * amp;
            if (i === 0) mgraphics.move_to(x, y); else mgraphics.line_to(x, y);
        }
        mgraphics.stroke();
    }
    // trail scrolling in from the right
    for (var t = 0; t < trail.length; t++)
    {
        var tx = w - 4 - (trail.length - 1 - t) * ((w - 8) / 60);
        col(C.amber, 0.15 + 0.6 * t / trail.length);
        mgraphics.rectangle(tx, mid - trail[t] * amp - 1, 2, 2);
        mgraphics.fill();
    }
    col(C.amber);
    mgraphics.arc(w - 5, mid - value * amp, 3, 0, Math.PI * 2);
    mgraphics.fill();
    col(C.quiet);
    mgraphics.select_font_face(FONT);
    mgraphics.set_font_size(7.5);
    mgraphics.move_to(4, 9);
    mgraphics.show_text(which === 3 ? "LIFE" : "LFO " + which);
}

function onresize() { mgraphics.redraw(); }
