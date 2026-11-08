// BASH! GRAB CLIP: loads the audio clip selected in Live (the one shown in Clip View) into the
// slicer ("grab slice") or the selected drum pad ("grab pad"). Live can't drag clips into a
// Max for Live device, so: click the clip, then GRAB CLIP. Warped loops bring their tempo too.
inlets = 1;
outlets = 1;

function say(text) { outlet(0, "say", text); }

function first(v) { return v instanceof Array ? v[0] : v; }

function text(v)
{
    if (v instanceof Array) return v.join(" ");
    return String(v);
}

function grab(target)
{
    var clip = new LiveAPI("live_set view detail_clip");
    if (!clip || Number(clip.id) === 0) { say("Click an audio clip in Live first, then GRAB CLIP"); return; }
    if (Number(first(clip.get("is_audio_clip"))) !== 1) { say("That's a MIDI clip: click an audio clip, then GRAB CLIP"); return; }
    var path = text(clip.get("file_path"));
    if (!path || path === "0") { say("That clip has no audio file on disk yet: save the set, then try again"); return; }

    if (target === "pad")
    {
        outlet(0, "pad", "load", -1, path);
        return;
    }
    outlet(0, "load", path);

    // a warped clip: its tempo, from its length in beats and its file's length in seconds
    try
    {
        if (Number(first(clip.get("warping"))) === 1)
        {
            var beats = Number(first(clip.get("end_marker"))) - Number(first(clip.get("start_marker")));
            var frames = Number(first(clip.get("sample_length")));
            var rate = Number(first(clip.get("sample_rate")));
            if (beats > 0 && frames > 0 && rate > 0)
            {
                var bpm = beats / (frames / rate) * 60.0;
                while (bpm > 0 && bpm < 60) bpm *= 2;
                while (bpm > 240) bpm /= 2;
                if (bpm >= 60 && bpm <= 240) outlet(0, "srcbpm", Math.round(bpm * 100) / 100);
            }
        }
    }
    catch (e) {}
}

function anything() {}
