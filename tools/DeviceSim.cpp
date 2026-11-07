// Runs BASH!'s instrument the way the Max for Live device does and prints every message
// it sends to the device (one JSON array per line), for max/preview.js to replay into the
// display scripts. Scenario: synth | slice | drum | step | mod
#include "bash/Instrument.h"
#include <cstdio>
#include <string>
#include <vector>

static std::string q (const std::string& s)
{
    std::string o = "\"";
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; if ((unsigned char) c >= 32) o += c; }
    return o + "\"";
}

int main (int argc, char** argv)
{
    const std::string scenario = argc > 1 ? argv[1] : "synth";
    bash::Instrument in;
    std::vector<std::string> out;
    in.setEmitter ([&] (const std::string& sel, const bash::Atoms& a)
    {
        std::string line = "[" + q (sel);
        for (auto& x : a)
        {
            line += ",";
            if (const double* d = std::get_if<double> (&x)) { char b[64]; std::snprintf (b, sizeof b, "%.9g", *d); line += b; }
            else line += q (std::get<std::string> (x));
        }
        out.push_back (line + "]");
    });
    in.prepare (48000.0, 128);
    auto p = [&] (const char* id, double v) { in.command ("param", { std::string (id), v }); };
    in.command ("refresh", {});
    in.command ("key", { 9.0, (double) ((1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10)) });   // A minor

    if (scenario == "synth") { p ("engine", 0); p ("density", 0.75); p ("steps", 16); }
    if (scenario == "slice") { p ("engine", 1); in.command ("loop", { 2.0 }); p ("slicemode", 2); p ("winsize", 6); }
    if (scenario == "drum")  { p ("engine", 2); in.command ("kit", { std::string ("pick"), 1.0 }); in.command ("pad", { std::string ("select"), 2.0 }); p ("mode", 0); p ("slicemode", 3); }
    if (scenario == "step")  { p ("engine", 2); in.command ("kit", { std::string ("pick"), 0.0 }); in.command ("groove", { 5.0 });
                               in.command ("lane", { std::string ("select"), 3.0 }); in.command ("step", { 1.0, 6.0, std::string ("ratchet") }); }
    if (scenario == "mod")   { p ("engine", 0); p ("m1src", 1); p ("m1dst", 6); p ("m1amt", 0.6); p ("m2src", 3); p ("m2dst", 1); p ("m2amt", -0.3); p ("lfo2sync", 1); }

    bash::Transport tp; tp.hostAvailable = true; tp.hostPlaying = true; tp.hostBpm = 172.0;
    std::vector<float> l (128), r (128);
    const int blocksPerFrame = (int) (48000.0 * 0.04 / 128);
    for (int frame = 0; frame < 60; ++frame)
    {
        for (int b = 0; b < blocksPerFrame; ++b) { in.process (l.data(), r.data(), 128, tp); tp.hostPpq += 128.0 * tp.hostBpm / 60.0 / 48000.0; }
        in.tick();
    }
    for (auto& s : out) std::printf ("%s\n", s.c_str());
    return 0;
}
