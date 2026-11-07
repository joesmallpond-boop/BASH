// Prints BASH!'s parameter table as JSON, for max/make_device.py (so the device's
// live.* objects always match the instrument's parameters).
#include "bash/Instrument.h"
#include <cstdio>
#include <string>

static std::string q (const std::string& s)
{
    std::string o = "\"";
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
    return o + "\"";
}

int main()
{
    const char* kinds[] = { "float", "int", "enum", "toggle" };
    std::printf ("[\n");
    const auto& specs = bash::paramSpecs();
    for (size_t i = 0; i < specs.size(); ++i)
    {
        const auto& s = specs[i];
        std::printf ("  {\"id\": %s, \"name\": %s, \"short\": %s, \"kind\": \"%s\", \"min\": %g, \"max\": %g, \"def\": %g, \"exp\": %g, \"unit\": %s, \"group\": %d, \"choices\": [",
                     q (s.id).c_str(), q (s.name).c_str(), q (s.shortName).c_str(), kinds[(int) s.kind], s.min, s.max, s.def, s.exponent, q (s.unit).c_str(), s.group);
        for (size_t c = 0; c < s.choices.size(); ++c) std::printf ("%s%s", c ? ", " : "", q (s.choices[c]).c_str());
        std::printf ("]}%s\n", i + 1 < specs.size() ? "," : "");
    }
    std::printf ("]\n");
    return 0;
}
