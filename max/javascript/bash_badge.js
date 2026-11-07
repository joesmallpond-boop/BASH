// BASH! badge: the name, bouncing to the beat, flaring on every hit, glowing with the
// output level. Underneath: Live's key (or a status message for a few seconds).
// Messages in: frame ..., hits ..., keyname <text>, msg <text>, pv <id> <v>
inlets = 1;
outlets = 1;
mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

var C = {
    bg: [0.114, 0.114, 0.114, 1], panel: [0.145, 0.145, 0.145, 1], line: [0.227, 0.227, 0.227, 1],
    ink: [0.914, 0.902, 0.875, 1], quiet: [0.545, 0.541, 0.525, 1],
    amber: [1.0, 0.722, 0.0, 1], amberDim: [0.541, 0.396, 0.031, 1], active: [0.29, 0.224, 0.063, 1]
};

var running = 0, beat = 0, peak = 0, keyText = "", msgText = "", msgUntil = 0, frameCount = 0;
var lastHits = [], flare = 0, sparks = [];
var letters = ["B", "A", "S", "H", "!"];

function now() { return new Date().getTime(); }
function col(c, alpha) { mgraphics.set_source_rgba(c[0], c[1], c[2], alpha === undefined ? c[3] : alpha); }

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
    var w = mgraphics.size[0], h = mgraphics.size[1];
    var g = mgraphics;
    col(C.bg);
    g.rectangle_rounded(0, 0, w, h, 8, 8);
    g.fill();

    // glow from the output level
    var glow = Math.min(1, peak * 1.4);
    g.set_source_rgba(1.0, 0.722, 0.0, 0.10 + 0.35 * glow + 0.25 * flare);
    g.rectangle_rounded(2, 2, w - 4, h - 20, 7, 7);
    g.fill();

    // the letters: each one hops on its own part of the beat
    g.select_font_face("Arial Black");
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
        col(i === 4 ? C.amber : C.ink);
        g.move_to(x + wobble, 30 - hop);
        g.show_text(letters[i]);
        var m = g.text_measure(letters[i]);
        x += m[0] + 1;
    }

    // sparks from hits
    for (var s = 0; s < sparks.length; s++)
    {
        var sp = sparks[s];
        g.set_source_rgba(1.0, 0.75 + 0.25 * Math.random(), 0.2, sp.life / 18);
        g.rectangle(sp.x, sp.y, 2, 2);
        g.fill();
    }

    // running light
    col(running ? C.amber : C.line);
    g.arc(w - 10, 10, 3.5, 0, Math.PI * 2);
    g.fill();

    // key / status line
    var text = now() < msgUntil ? msgText : keyText;
    g.select_font_face("Arial");
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
