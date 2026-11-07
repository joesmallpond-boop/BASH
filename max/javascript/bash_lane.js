// BASH! step lane strip: the selected lane's settings and its die.
//   < >  previous / next lane      ALGO: click to cycle (shift: back)
//   LEN / PULSES / DENS / ROT / EVOLVE: drag up or down      SPEED: click to cycle
//   MUTE, ROLL (new algorithm settings + pattern), GEN (re-run the algorithm), CLEAR
// Messages in: lane, lanesel, menu lanealgos / lanespeeds, hits
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
var algoNames = ["Euclid", "Random", "Groove", "Rolls", "Mutate", "Humanize"];
var speedNames = ["x1/4", "x1/2", "x2/3", "x1", "x3/2", "x2", "x4"];
var lanes = [];
var sel = 0, flash = 0, lastHits = [], rollSpin = 0;
var fields = [], drag = null;
for (var i = 0; i < 16; i++) lanes.push({ name: "", len: 16, speed: 3, mute: 0, algo: 0, density: 0.5, pulses: 4, rotate: 0, evolve: 0 });

function col(c, a) { mgraphics.set_source_rgba(c[0], c[1], c[2], a === undefined ? c[3] : a); }

function anything()
{
    var a = arrayfromargs(arguments);
    switch (messagename)
    {
        case "lane":
            var l = a[0];
            if (l < 0 || l > 15) break;
            lanes[l] = { name: String(a[1]), len: a[2], speed: a[3], mute: a[4], algo: a[5], density: a[6], pulses: a[7], rotate: a[8], evolve: a[9] };
            if (l === sel) mgraphics.redraw();
            break;
        case "lanesel":
            sel = a[0];
            mgraphics.redraw();
            break;
        case "menu":
            if (a[0] === "lanealgos") algoNames = a.slice(1);
            if (a[0] === "lanespeeds") speedNames = a.slice(1);
            break;
        case "hits":
            if (lastHits.length === a.length && a[sel] !== lastHits[sel]) flash = 1;
            lastHits = a;
            break;
        case "frame":
            if (flash > 0.02 || rollSpin > 0.02) { flash *= 0.8; rollSpin *= 0.85; mgraphics.redraw(); }
            break;
        default:
            break;
    }
}

function layout(w, h)
{
    var L = lanes[sel];
    var row1 = 2, row2 = h / 2 + 1, rh = h / 2 - 3;
    var euclid = L.algo === 0;
    fields = [
        { id: "prev",  x: 2,   y: row1, w: 14,  h: rh, label: "<" },
        { id: "name",  x: 17,  y: row1, w: 80,  h: rh },
        { id: "next",  x: 98,  y: row1, w: 14,  h: rh, label: ">" },
        { id: "algo",  x: 116, y: row1, w: 70,  h: rh, title: "ALGO", text: algoNames[L.algo] || "?" },
        { id: "speed", x: 188, y: row1, w: 48,  h: rh, title: "SPEED", text: speedNames[L.speed] || "x1" },
        { id: "mute",  x: 238, y: row1, w: 38,  h: rh, label: "MUTE", on: L.mute },
        { id: "roll",  x: 278, y: row1, w: w - 280, h: rh, label: "ROLL" },
        { id: "len",   x: 2,   y: row2, w: 46,  h: rh, title: "LEN", text: String(L.len), drag: [1, 64, 0.25] },
        { id: euclid ? "pulses" : "density", x: 50, y: row2, w: 54, h: rh, title: euclid ? "PULSES" : "DENS",
          text: euclid ? String(L.pulses) : Math.round(L.density * 100) + "%", drag: euclid ? [0, 64, 0.25] : [0, 1, 0.01] },
        { id: "rotate", x: 106, y: row2, w: 44, h: rh, title: "ROT", text: String(L.rotate), drag: [0, 63, 0.25] },
        { id: "evolve", x: 152, y: row2, w: 54, h: rh, title: "EVOLVE", text: Math.round(L.evolve * 100) + "%", drag: [0, 1, 0.01] },
        { id: "gen",   x: 208, y: row2, w: 34,  h: rh, label: "GEN" },
        { id: "clear", x: 244, y: row2, w: 32,  h: rh, label: "CLR" },
        { id: "dice",  x: 278, y: row2, w: w - 280, h: rh, label: "" }
    ];
}

function paint()
{
    var w = mgraphics.size[0], h = mgraphics.size[1];
    layout(w, h);
    col(C.bg);
    mgraphics.rectangle_rounded(0, 0, w, h, 6, 6);
    mgraphics.fill();
    mgraphics.select_font_face("Arial");
    var L = lanes[sel];
    for (var i = 0; i < fields.length; i++)
    {
        var f = fields[i];
        if (f.id === "dice") { drawDie(f); continue; }
        col(f.on ? C.active : C.panel);
        mgraphics.rectangle_rounded(f.x, f.y, f.w, f.h, 4, 4);
        mgraphics.fill();
        if (f.id === "name")
        {
            if (flash > 0.05) { col(C.amber, flash * 0.5); mgraphics.rectangle_rounded(f.x, f.y, f.w, f.h, 4, 4); mgraphics.fill(); }
            col(C.amber);
            mgraphics.set_font_size(10.5);
            mgraphics.move_to(f.x + 5, f.y + f.h / 2 + 4);
            var nm = (sel + 1) + " " + L.name;
            while (nm.length > 1 && mgraphics.text_measure(nm)[0] > f.w - 8) nm = nm.substr(0, nm.length - 1);
            mgraphics.show_text(nm);
            continue;
        }
        if (f.title)
        {
            col(C.quiet);
            mgraphics.set_font_size(7.5);
            mgraphics.move_to(f.x + 4, f.y + 9);
            mgraphics.show_text(f.title);
            col(f.drag ? C.amber : C.ink);
            mgraphics.set_font_size(10);
            mgraphics.move_to(f.x + 4, f.y + f.h - 3);
            mgraphics.show_text(f.text);
        }
        else
        {
            col(f.on ? C.amber : C.ink);
            mgraphics.set_font_size(9.5);
            var tw = mgraphics.text_measure(f.label)[0];
            mgraphics.move_to(f.x + (f.w - tw) / 2, f.y + f.h / 2 + 3.5);
            mgraphics.show_text(f.label);
        }
    }
}

function drawDie(f)
{
    // the lane die: a little cube that tumbles when rolled
    col(C.panel);
    mgraphics.rectangle_rounded(f.x, f.y, f.w, f.h, 4, 4);
    mgraphics.fill();
    var s = Math.min(f.h - 6, 16), cx = f.x + f.w / 2, cy = f.y + f.h / 2;
    mgraphics.save();
    mgraphics.translate(cx, cy);
    mgraphics.rotate(rollSpin * Math.PI * 2);
    col(C.amber);
    mgraphics.rectangle_rounded(-s / 2, -s / 2, s, s, 3, 3);
    mgraphics.fill();
    col(C.bg);
    var p = s / 4;
    var dots = [[-p, -p], [p, p], [p, -p], [-p, p], [0, 0]];
    for (var d = 0; d < dots.length; d++) { mgraphics.arc(dots[d][0], dots[d][1], 1.4, 0, Math.PI * 2); mgraphics.fill(); }
    mgraphics.restore();
}

function fieldAt(x, y)
{
    for (var i = 0; i < fields.length; i++)
    {
        var f = fields[i];
        if (x >= f.x && x < f.x + f.w && y >= f.y && y < f.y + f.h) return f;
    }
    return null;
}

function onclick(x, y, but, cmd, shift, capslock, option, ctrl)
{
    var f = fieldAt(x, y);
    drag = null;
    if (f === null) return;
    var L = lanes[sel];
    switch (f.id)
    {
        case "prev": sel = (sel + 15) % 16; outlet(0, "lane", "select", sel); break;
        case "next": sel = (sel + 1) % 16; outlet(0, "lane", "select", sel); break;
        case "algo": outlet(0, "lane", "algo", sel, (L.algo + (shift ? algoNames.length - 1 : 1)) % algoNames.length); break;
        case "speed": outlet(0, "lane", "speed", sel, (L.speed + (shift ? speedNames.length - 1 : 1)) % speedNames.length); break;
        case "mute": outlet(0, "lane", "mute", sel); break;
        case "roll": case "dice": rollSpin = 1; outlet(0, "lane", "roll", sel); break;
        case "gen": outlet(0, "lane", "gen", sel); break;
        case "clear": outlet(0, "lane", "clear", sel); break;
        default:
            if (f.drag)
            {
                var start = f.id === "len" ? L.len : f.id === "pulses" ? L.pulses : f.id === "density" ? L.density
                          : f.id === "rotate" ? L.rotate : L.evolve;
                drag = { id: f.id, y0: y, start: start, range: f.drag };
            }
            break;
    }
    mgraphics.redraw();
}

function ondrag(x, y, but)
{
    if (drag === null || but === 0) { drag = null; return; }
    var r = drag.range;
    var v = drag.start + (drag.y0 - y) * r[2];
    v = Math.max(r[0], Math.min(r[1], v));
    if (r[2] >= 0.25) v = Math.round(v);
    outlet(0, "lane", drag.id === "len" ? "length" : drag.id, sel, v);
}

function onresize() { mgraphics.redraw(); }
