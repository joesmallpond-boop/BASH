// BASH! synth keys: three octaves that light up as the synth plays, with each note
// blowing a bubble that floats off the top (accents a bit bigger). The key's notes glow.
// Messages in: frame (note counter + last note), pv (root / scale / octave)
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
var lastCount = -1, glow = [], bubbles = [], octave = 2, root = 0, lastNote = -1;
for (var i = 0; i < 128; i++) glow.push(0);
var BLACK = [false, true, false, true, false, false, true, false, true, false, true, false];

function lowNote() { return 12 * (octave + 1); }

function anything()
{
    var a = arrayfromargs(arguments);
    if (messagename === "pv")
    {
        if (a[0] === "octave") octave = a[1];
        if (a[0] === "root") root = a[1];
        mgraphics.redraw();
        return;
    }
    if (messagename !== "frame") return;
    var count = a[10], note = a[11];
    if (lastCount >= 0 && count !== lastCount && note >= 0)
    {
        glow[note] = 1;
        lastNote = note;
        var p = keyPos(note);
        if (p !== null && bubbles.length < 30)
            bubbles.push({ x: p.x + p.w / 2, y: p.y, r: 2 + Math.random() * 2.5, vx: (Math.random() - 0.5) * 0.6, life: 1 });
    }
    lastCount = count;
    for (var n = 0; n < 128; n++) glow[n] *= 0.86;
    for (var b = bubbles.length - 1; b >= 0; b--)
    {
        var bb = bubbles[b];
        bb.y -= 1.6; bb.x += bb.vx + Math.sin(bb.y * 0.2) * 0.3; bb.life -= 0.035; bb.r += 0.05;
        if (bb.life <= 0) bubbles.splice(b, 1);
    }
    mgraphics.redraw();
}

function keyPos(note)
{
    var w = mgraphics.size[0], h = mgraphics.size[1];
    var lo = lowNote(), whites = 21;
    var rel = note - lo;
    if (rel < 0 || rel >= 36) return null;
    var oct = Math.floor(rel / 12), pc = rel % 12;
    var whiteIndex = [0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6][pc] + oct * 7;
    var ww = (w - 4) / whites, kh = h * 0.55;
    if (BLACK[pc]) return { x: 2 + (whiteIndex + 1) * ww - ww * 0.3, y: h - kh, w: ww * 0.6, h: kh * 0.6, black: true };
    return { x: 2 + whiteIndex * ww, y: h - kh, w: ww - 1, h: kh, black: false };
}

function paint()
{
    refreshTheme();
    var w = mgraphics.size[0], h = mgraphics.size[1];
    col(C.bg);
    mgraphics.rectangle_rounded(0, 0, w, h, 6, 6);
    mgraphics.fill();
    var lo = lowNote();
    for (var pass = 0; pass < 2; pass++)
        for (var n = lo; n < lo + 36; n++)
        {
            var p = keyPos(n);
            if (p === null || (pass === 0) === p.black) continue;
            var inKey = ((n - root) % 12 + 12) % 12 === 0;
            col(p.black ? C.bg : C.panel);
            mgraphics.rectangle_rounded(p.x, p.y, p.w, p.h, 2, 2);
            mgraphics.fill();
            if (inKey) { col(C.amberDim, 0.5); mgraphics.rectangle(p.x + 1, p.y + p.h - 4, p.w - 2, 2); mgraphics.fill(); }
            if (glow[n] > 0.03) { col(C.amber, glow[n]); mgraphics.rectangle_rounded(p.x, p.y, p.w, p.h, 2, 2); mgraphics.fill(); }
        }
    for (var b = 0; b < bubbles.length; b++)
    {
        var bb = bubbles[b];
        col(C.amber, bb.life * 0.8);
        mgraphics.set_line_width(1);
        mgraphics.arc(bb.x, bb.y, bb.r, 0, Math.PI * 2);
        mgraphics.stroke();
    }
    col(C.quiet);
    mgraphics.select_font_face(FONT);
    mgraphics.set_font_size(7.5);
    mgraphics.move_to(4, 9);
    mgraphics.show_text("NOTES" + (lastNote >= 0 ? "  " + ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"][lastNote % 12] + (Math.floor(lastNote / 12) - 1) : ""));
}

function onresize() { mgraphics.redraw(); }
