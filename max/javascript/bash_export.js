// BASH! LOOP OUT: puts the loop bash~ just exported into Live.
//   clip <beats> <pitch start dur vel>...   -> a MIDI clip in the first empty slot of this track
//   exported wav <path>                     -> a new audio track next to this one, with the WAV in its first slot
// (Max for Live can't drag files out of a device, so the loop lands in a clip you can drag anywhere;
//  the files are also in User Library > Kalabash > Captures in Live's browser.)
inlets = 1;
outlets = 1;

var lastClip = null;

function say(text) { outlet(0, "say", text); }

function num(v) { return Number(v instanceof Array ? v[0] : v); }

// the track this device is on (it may sit inside a rack)
function thisTrack()
{
    var t = new LiveAPI("this_device");
    for (var i = 0; i < 8 && t && t.id != 0; i++)
    {
        t = new LiveAPI(t.unquotedpath + " canonical_parent");
        if (t.type === "Track") return t;
    }
    return null;
}

function clip()
{
    lastClip = arrayfromargs(arguments);
}

function exported(kind, path)
{
    try
    {
        if (kind === "midi") midiClip();
        else if (kind === "wav") audioClip(path);
    }
    catch (e) { say("Saved to Captures (couldn't make the clip: " + e + ")"); }
}

function midiClip()
{
    if (!lastClip) return;
    var a = lastClip;
    lastClip = null;
    var track = thisTrack();
    if (!track) { say("Saved to User Library > Kalabash > Captures"); return; }
    if (num(track.get("has_midi_input")) !== 1) { say("Saved to User Library > Kalabash > Captures"); return; }
    var n = track.getcount("clip_slots");
    var slot = null;
    for (var i = 0; i < n; i++)
    {
        var s = new LiveAPI(track.unquotedpath + " clip_slots " + i);
        if (num(s.get("has_clip")) === 0) { slot = s; break; }
    }
    if (!slot)
    {
        new LiveAPI("live_set").call("create_scene", -1);
        slot = new LiveAPI(track.unquotedpath + " clip_slots " + n);
    }
    slot.call("create_clip", a[0]);
    var c = new LiveAPI(slot.unquotedpath + " clip");
    c.set("name", "BASH! loop");
    var notes = [];
    for (var k = 1; k + 3 < a.length; k += 4)
        notes.push({ pitch: a[k], start_time: a[k + 1], duration: a[k + 2], velocity: a[k + 3], mute: 0 });
    if (notes.length)
    {
        try { c.call("add_new_notes", { notes: notes }); }
        catch (e)
        {
            // Live 11.0 and earlier
            c.call("set_notes");
            c.call("notes", notes.length);
            for (var j = 0; j < notes.length; j++)
                c.call("note", notes[j].pitch, notes[j].start_time.toFixed(4), notes[j].duration.toFixed(4), notes[j].velocity, 0);
            c.call("done");
        }
    }
    try { new LiveAPI("live_set view").set("highlighted_clip_slot", "id", slot.id); } catch (e2) {}
    say("MIDI loop in a clip on this track (drag it anywhere)");
}

function audioClip(path)
{
    var live = new LiveAPI("live_set");
    var track = thisTrack();
    var index = -1;
    if (track)
    {
        var count = live.getcount("tracks");
        for (var i = 0; i < count; i++)
            if (new LiveAPI("live_set tracks " + i).id == track.id) { index = i + 1; break; }
    }
    live.call("create_audio_track", index);
    var at = index < 0 ? live.getcount("tracks") - 1 : index;
    var t = new LiveAPI("live_set tracks " + at);
    t.set("name", "BASH! loop");
    var slot = new LiveAPI("live_set tracks " + at + " clip_slots 0");
    slot.call("create_audio_clip", path);
    if (num(slot.get("has_clip")) === 1)
    {
        try { new LiveAPI("live_set view").set("highlighted_clip_slot", "id", slot.id); } catch (e2) {}
        say("WAV loop on a new audio track (drag it anywhere)");
    }
    else say("WAV saved to User Library > Kalabash > Captures (drag it in from the browser)");
}

function anything() {}
