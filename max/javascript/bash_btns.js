// BASH! button bar. jsarguments: "LABEL|message words" per button.
// A click sends the message; the button flashes. "DICE" draws a die that tumbles.
inlets = 1;
outlets = 1;
mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

var C = {
    bg: [0.114, 0.114, 0.114, 1], panel: [0.145, 0.145, 0.145, 1], ink: [0.914, 0.902, 0.875, 1],
    amber: [1.0, 0.722, 0.0, 1], active: [0.29, 0.224, 0.063, 1]
};
var defs = [];
for (var i = 1; i < jsarguments.length; i++)
{
    var parts = String(jsarguments[i]).split("|");
    defs.push({ label: parts[0], msg: (parts[1] || parts[0].toLowerCase()).split(" "), flash: 0, spin: 0 });
}

function col(c, a) { mgraphics.set_source_rgba(c[0], c[1], c[2], a === undefined ? c[3] : a); }

function anything()
{
    if (messagename !== "frame") return;
    var busy = false;
    for (var i = 0; i < defs.length; i++)
    {
        defs[i].flash *= 0.8; defs[i].spin *= 0.86;
        busy = busy || defs[i].flash > 0.02 || defs[i].spin > 0.02;
    }
    if (busy) mgraphics.redraw();
}

function paint()
{
    var w = mgraphics.size[0], h = mgraphics.size[1];
    var n = Math.max(1, defs.length), bw = (w - 2 * (n - 1)) / n;
    mgraphics.select_font_face("Arial Bold");
    for (var i = 0; i < defs.length; i++)
    {
        var d = defs[i], x = i * (bw + 2);
        col(C.panel);
        mgraphics.rectangle_rounded(x, 0, bw, h, 4, 4);
        mgraphics.fill();
        if (d.flash > 0.02) { col(C.amber, d.flash * 0.7); mgraphics.rectangle_rounded(x, 0, bw, h, 4, 4); mgraphics.fill(); }
        var label = d.label;
        var tx = x + 4;
        if (label === "DICE")
        {
            var s = Math.min(h - 6, 12);
            mgraphics.save();
            mgraphics.translate(x + 4 + s / 2, h / 2);
            mgraphics.rotate(d.spin * Math.PI * 2.5);
            col(C.amber);
            mgraphics.rectangle_rounded(-s / 2, -s / 2, s, s, 2.5, 2.5);
            mgraphics.fill();
            col(C.bg);
            var p = s / 4;
            var dots = [[-p, -p], [p, p], [0, 0]];
            for (var k = 0; k < dots.length; k++) { mgraphics.arc(dots[k][0], dots[k][1], 1.2, 0, Math.PI * 2); mgraphics.fill(); }
            mgraphics.restore();
            tx = x + s + 8;
        }
        col(C.ink);
        mgraphics.set_font_size(9);
        var tw = mgraphics.text_measure(label)[0];
        mgraphics.move_to(label === "DICE" ? tx : x + (bw - tw) / 2, h / 2 + 3.5);
        mgraphics.show_text(label);
    }
}

function onclick(x, y)
{
    var w = mgraphics.size[0];
    var n = Math.max(1, defs.length), bw = (w - 2 * (n - 1)) / n;
    var i = Math.floor(x / (bw + 2));
    if (i < 0 || i >= defs.length) return;
    defs[i].flash = 1; defs[i].spin = 1;
    outlet(0, defs[i].msg);
    mgraphics.redraw();
}

function onresize() { mgraphics.redraw(); }
