// BASH! drum pads, 4 x 4 (pad 1 = C1, bottom left, like Push / MPC).
// Every hit throws a ripple out of its pad; FM pads are outlined, sample pads filled.
//   click: play + select     shift-click: select quietly     alt-click: clear the pad
// Messages in: padnames, padkinds, padsel, hits, frame, pv
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
var names = [], kinds = [], sel = 0, lastHits = [], glow = [], ripples = [];
for (var i = 0; i < 16; i++) { names.push("-"); kinds.push(0); glow.push(0); }

function col(c, a) { mgraphics.set_source_rgba(c[0], c[1], c[2], a === undefined ? c[3] : a); }

function padRect(i, w, h)
{
    var g = 2, cw = (w - g * 5) / 4, ch = (h - g * 5) / 4;
    var colI = i % 4, rowI = 3 - Math.floor(i / 4);
    return { x: g + colI * (cw + g), y: g + rowI * (ch + g), w: cw, h: ch };
}

function anything()
{
    var a = arrayfromargs(arguments);
    switch (messagename)
    {
        case "padnames": names = a; mgraphics.redraw(); break;
        case "padkinds": kinds = a; mgraphics.redraw(); break;
        case "padsel": sel = a[0]; mgraphics.redraw(); break;
        case "hits":
            var w = mgraphics.size[0], h = mgraphics.size[1];
            for (var k = 0; k < a.length && k < 16; k++)
                if (lastHits.length === a.length && a[k] !== lastHits[k])
                {
                    glow[k] = 1;
                    var r = padRect(k, w, h);
                    if (ripples.length < 24) ripples.push({ x: r.x + r.w / 2, y: r.y + r.h / 2, r: 4, life: 1 });
                }
            lastHits = a;
            break;
        case "frame":
            for (var g = 0; g < 16; g++) glow[g] *= 0.8;
            for (var p = ripples.length - 1; p >= 0; p--)
            {
                ripples[p].r += 3.2;
                ripples[p].life -= 0.09;
                if (ripples[p].life <= 0) ripples.splice(p, 1);
            }
            mgraphics.redraw();
            break;
        default:
            break;
    }
}

function paint()
{
    var w = mgraphics.size[0], h = mgraphics.size[1];
    col(C.bg);
    mgraphics.rectangle_rounded(0, 0, w, h, 6, 6);
    mgraphics.fill();
    mgraphics.select_font_face("Arial");
    for (var i = 0; i < 16; i++)
    {
        var r = padRect(i, w, h);
        var kind = kinds[i] || 0;
        col(kind === 1 ? C.raised : C.panel);
        mgraphics.rectangle_rounded(r.x, r.y, r.w, r.h, 4, 4);
        mgraphics.fill();
        if (glow[i] > 0.03)
        {
            col(C.amber, glow[i] * 0.85);
            mgraphics.rectangle_rounded(r.x, r.y, r.w, r.h, 4, 4);
            mgraphics.fill();
        }
        if (kind === 2)
        {
            col(C.amberDim);
            mgraphics.set_line_width(1);
            mgraphics.rectangle_rounded(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 4, 4);
            mgraphics.stroke();
        }
        if (i === sel)
        {
            col(C.amber);
            mgraphics.set_line_width(1.5);
            mgraphics.rectangle_rounded(r.x + 0.75, r.y + 0.75, r.w - 1.5, r.h - 1.5, 4, 4);
            mgraphics.stroke();
        }
        col(glow[i] > 0.4 ? C.bg : (kind === 0 ? C.quiet : C.ink));
        mgraphics.set_font_size(7.5);
        var nm = String(names[i] || "-");
        while (nm.length > 1 && mgraphics.text_measure(nm)[0] > r.w - 3) nm = nm.substr(0, nm.length - 1);
        mgraphics.move_to(r.x + 2, r.y + r.h - 3);
        mgraphics.show_text(nm);
        col(C.quiet, 0.8);
        mgraphics.move_to(r.x + 2, r.y + 9);
        mgraphics.show_text(String(i + 1));
    }
    for (var p = 0; p < ripples.length; p++)
    {
        var rp = ripples[p];
        col(C.amber, rp.life * 0.7);
        mgraphics.set_line_width(1.2);
        mgraphics.arc(rp.x, rp.y, rp.r, 0, Math.PI * 2);
        mgraphics.stroke();
    }
}

function onclick(x, y, but, cmd, shift, capslock, option, ctrl)
{
    var w = mgraphics.size[0], h = mgraphics.size[1];
    for (var i = 0; i < 16; i++)
    {
        var r = padRect(i, w, h);
        if (x < r.x || x >= r.x + r.w || y < r.y || y >= r.y + r.h) continue;
        sel = i;
        if (option) outlet(0, "pad", "clear", i);
        else if (shift) outlet(0, "pad", "select", i);
        else outlet(0, "pad", "play", i);
        mgraphics.redraw();
        return;
    }
}

function onresize() { mgraphics.redraw(); }
