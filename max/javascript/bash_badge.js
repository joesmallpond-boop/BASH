// BASH! badge: the name, bouncing to the beat, flaring on every hit, glowing with the
// output level. Underneath: Live's key (or a status message for a few seconds).
// Messages in: frame ..., hits ..., keyname <text>, msg <text>, pv <id> <v>
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

var running = 0, beat = 0, peak = 0, keyText = "", msgText = "", msgUntil = 0, frameCount = 0;
var lastHits = [], flare = 0, sparks = [];
var letters = ["B", "A", "S", "H", "!"];

function now() { return new Date().getTime(); }

function anything()
{
    var a = arrayfromargs(arguments);
    switch (messagename)
    {
        case "frame":
            running = a[0]; beat = a[1]; peak = a[6];
            frameCount++;
            flare *= 0.82;
            for (var i = sparks.length - 1; i >= 0; i--)
            {
                var s = sparks[i];
                s.x += s.vx; s.y += s.vy; s.vy += 0.25; s.life -= 1;
                if (s.life <= 0) sparks.splice(i, 1);
            }
            mgraphics.redraw();
            break;
        case "hits":
            var any = false;
            for (var k = 0; k < a.length; k++)
            {
                if (lastHits.length === a.length && a[k] !== lastHits[k]) any = true;
            }
            lastHits = a;
            if (any)
            {
                flare = 1;
                if (sparks.length < 40)
                    for (var n = 0; n < 4; n++)
                        sparks.push({ x: 20 + Math.random() * 64, y: 26, vx: (Math.random() - 0.5) * 3, vy: -1 - Math.random() * 2.5, life: 10 + Math.random() * 8 });
            }
            break;
        case "keyname":
            keyText = a.join(" ");
            mgraphics.redraw();
            break;
        case "msg":
            msgText = a.join(" ");
            msgUntil = now() + 3500;
            mgraphics.redraw();
            break;
        default:
            break;
    }
}

function paint()
{
    refreshTheme();
    var w = mgraphics.size[0], h = mgraphics.size[1];
    var g = mgraphics;
    col(C.bg);
    g.rectangle_rounded(0, 0, w, h, 8, 8);
    g.fill();

    // glow from the output level
    var glow = Math.min(1, peak * 1.4);
    col(C.amber, 0.04 + 0.12 * glow + 0.1 * flare);
    g.rectangle_rounded(2, 2, w - 4, h - 20, 7, 7);
    g.fill();

    // the letters: each one hops on its own part of the beat
    g.select_font_face(FONT_BOLD);
    g.set_font_size(19);
    var phase = beat - Math.floor(beat);
    var x = 8;
    for (var i = 0; i < letters.length; i++)
    {
        var hop = 0;
        if (running)
        {
            var p = (phase * letters.length + letters.length - i) % letters.length;
            hop = p < 1 ? Math.sin(p * Math.PI) * 5 : 0;
        }
        var wobble = flare * Math.sin(frameCount * 0.9 + i) * 1.5;
        col(i === 4 ? C.amber : C.ink, i === 4 ? 1 : 0.92);
        g.move_to(x + wobble, 30 - hop);
        g.show_text(letters[i]);
        var m = g.text_measure(letters[i]);
        x += m[0] + 1;
    }

    // sparks from hits
    for (var s = 0; s < sparks.length; s++)
    {
        var sp = sparks[s];
        col(Math.random() < 0.5 ? C.amber : C.alt, sp.life / 18);
        g.rectangle(sp.x, sp.y, 2, 2);
        g.fill();
    }

    // running light
    // running: the Decompression Tapes mark, rising and glowing with the beat
    var lift = running ? Math.sin((beat - Math.floor(beat)) * Math.PI) * 1.5 : 0;
    drawMark(w - 10, 10 - lift, 9, 2, running ? C.amber : C.quiet, running ? 0.85 : 0.35);

    // key / status line
    var text = now() < msgUntil ? msgText : keyText;
    g.select_font_face(FONT);
    g.set_font_size(9.5);
    col(now() < msgUntil ? C.amber : C.quiet);
    var tw = g.text_measure(text)[0];
    var maxw = w - 8;
    if (tw > maxw)
    {
        // long messages scroll
        var off = (frameCount * 1.2) % (tw + 20);
        g.move_to(4 - off, h - 6);
        g.show_text(text + "     " + text);
    }
    else
    {
        g.move_to(4, h - 6);
        g.show_text(text);
    }
}

function onresize() { mgraphics.redraw(); }
