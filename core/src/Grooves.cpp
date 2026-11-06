// BASH! step-sequencer grooves. Written for the 16-pad kit layout:
//  0 kick   1 snare   2 clap   3 closed hat   4 open hat   5-7 perc
//  8 tom low   9 tom high   10-11 perc   12 cymbal   13 perc   14 kick 2 / sub   15 FX
// Built from the rhythmic conventions of each scene, not copied from any record.
#include "bash/Library.h"

#include <algorithm>

namespace bash::grooves
{
    namespace
    {
        Groove G (const char* style, const char* name, double bpm, float swing, std::initializer_list<std::pair<int, const char*>> lanes, int rate = 1)
        {
            Groove g { style, name, bpm, swing, rate, {} };
            for (auto [lane, s] : lanes) g.lanes[(size_t) lane] = s;
            return g;
        }
        constexpr int K = 0, S = 1, C = 2, CH = 3, OH = 4, P1 = 5, P2 = 6, P3 = 7, TL = 8, TH = 9, P4 = 10, P5 = 11, CY = 12, P6 = 13, K2 = 14, FX = 15;

        std::vector<Groove> build()
        {
            return {
                // ---- UK Garage / 2-step: skippy kicks, snare on 2 & 4, swung off-beat hats
                G ("UK Garage", "2-Step Classic", 132, 0.28f, { { K, "x.........x....." }, { S, "....x.......x..." }, { C, "....g.......x..." },
                    { CH, "..x...x...x...x." }, { OH, "..............o." }, { P1, "...g..g.....g..g" }, { P2, "x.x.x.x.x.x.x.x." } }),
                G ("UK Garage", "2-Step Skippy", 134, 0.3f, { { K, "x......x..x....." }, { S, "....x.......x..." }, { CH, "..x.x.x...x.xx.x" },
                    { P1, "..g...g..g...g.." }, { P2, "xgxgxgxgxgxgxgxg" }, { CY, "x..............................." } }),
                G ("UK Garage", "Bumpy 2-Step", 130, 0.25f, { { K, "x..x......x....." }, { S, "....x.......x..." }, { C, ".......g....x..." },
                    { CH, "..xx..x...xx..x." }, { OH, "..............x." }, { P1, ".g.....g.g......" } }),
                G ("UK Garage", "Garage Rims", 134, 0.3f, { { K, "x.....x...x....." }, { S, "....x.......x..." }, { P1, ".g..g..g.g..g..g" },
                    { CH, "..x...x...x.x.x." }, { P2, "x.x.x.x.x.x.x.x." } }),
                G ("UK Garage", "Speed Garage 4x4", 130, 0.15f, { { K, "x...x...x...x..." }, { C, "....x.......x..." }, { OH, "..x...x...x...x." },
                    { CH, "x.x.x.x.x.x.x.x." }, { P1, "...g......g....." }, { K2, "x.......x......." } }),
                G ("UK Garage", "Dark Garage", 136, 0.22f, { { K, "x.........x..x.." }, { S, "....x.......x..." }, { CH, "..x...x...x...x." },
                    { TL, "......x........x" }, { P3, "...........o...." }, { FX, "f..............." } }),
                G ("UK Garage", "Garage Fills", 132, 0.28f, { { K, "x.........x....." }, { S, "....x.......x.rR" }, { CH, "..x...x...x...x." },
                    { TL, "............fff." }, { TH, "...........f..f." }, { C, "....n.......n..." } }),

                // ---- UK Funky: 4x4 kick under tom/conga syncopation
                G ("UK Funky", "Funky Percussion", 130, 0.18f, { { K, "x...x...x...x..." }, { C, "....x.......x..." }, { TL, "...x......x....." },
                    { TH, ".....x..x.....x." }, { P1, "..x..x...x..x..." }, { P2, "xgxgxgxgxgxgxgxg" }, { CH, ".x.x.x.x.x.x.x.x" } }),
                G ("UK Funky", "Funky Tribal", 128, 0.2f, { { K, "x..x..x...x..x.." }, { C, "....x.......x..." }, { TL, "x.....x...x....." },
                    { TH, "...x.....x...x.." }, { P1, "..x.x..x..x.x..x" }, { P4, "x..x..x..x..x..x..x..x.." } }),
                G ("UK Funky", "Funky Bounce", 130, 0.22f, { { K, "x...x...x...x..." }, { S, ".......x....x..." }, { CH, "..x...x...x...x." },
                    { P1, ".x..x..x..x..x.." }, { P2, "x.x.x.x.x.x.x.x." }, { P5, "...g.......g...." } }),

                // ---- Dubstep / halfstep: snare on beat 3, space, rolls
                G ("Dubstep / Halfstep", "Halfstep Classic", 140, 0.05f, { { K, "x..........x...." }, { S, "........x......." }, { CH, "..x...x...x...x." },
                    { OH, "......x........." }, { P1, "...........g...." } }),
                G ("Dubstep / Halfstep", "Deep Halfstep", 140, 0.04f, { { K, "x.................x............." }, { S, "........x...............x......." },
                    { CH, "....x.......x.......x.......x..." }, { P1, ".......g.............g.........." }, { K2, "x..............................." } }),
                G ("Dubstep / Halfstep", "Halfstep Skank", 140, 0.08f, { { K, "x.....x...x....." }, { S, "........x......." }, { CH, "..x...x...x...x." },
                    { P1, ".....g.......g.." }, { OH, "..............o." } }),
                G ("Dubstep / Halfstep", "Halfstep Rolls", 140, 0.04f, { { K, "x..........x...." }, { S, "........x......." }, { CH, "x.x.r.x.x.t.x.R." },
                    { OH, "......x........." }, { FX, "...............f" } }),
                G ("Dubstep / Halfstep", "Tribal Halfstep", 140, 0.1f, { { K, "x.........x....." }, { S, "........x......." }, { TL, "...x.....x...x.." },
                    { TH, ".x....x.......x." }, { P2, "x.x.x.x.x.x.x.x." }, { P4, "x..x..x..x.." } }),
                G ("Dubstep / Halfstep", "Halfstep Syncopated", 142, 0.06f, { { K, "x......x..x....." }, { S, "........x......." }, { CH, "..x...x...x.x.x." },
                    { C, "...............f" }, { P1, "....g......g...." } }),

                // ---- Drum & bass: two-step kick/snare, ghost notes, rolling hats
                G ("Drum & Bass", "Two-Step Roller", 174, 0.0f, { { K, "x.........x....." }, { S, "....x.......x..." }, { CH, "x.x.x.x.x.x.x.x." },
                    { P2, "xgxgxgxgxgxgxgxg" }, { CY, "..x...x...x...x." } }),
                G ("Drum & Bass", "Roller Ghosts", 174, 0.04f, { { K, "x.........x....." }, { S, "....x..g....x.g." }, { P1, "..g....g...g...." },
                    { CH, "x.x.x.x.x.x.x.x." }, { P2, "xgxgxgxgxgxgxgxg" } }),
                G ("Drum & Bass", "Steppers", 172, 0.0f, { { K, "x.......x.x....." }, { S, "....x.......x..." }, { CH, "..x...x...x...x." },
                    { CY, "x...x...x...x..." }, { P1, "...........g...." } }),
                G ("Drum & Bass", "Liquid Shuffle", 174, 0.12f, { { K, "x.........x....." }, { S, "....x.......x..." }, { CH, ".x.x.x.x.x.x.x.x" },
                    { P2, "xgxgxgxgxgxgxgxg" }, { CY, "x...x...x...x..." }, { OH, "..............o." } }),
                G ("Drum & Bass", "Neuro Stomp", 174, 0.0f, { { K, "x.........x..x.." }, { S, "....X.......X..." }, { CH, "..x...x...x...x." },
                    { C, "....x.......x..." }, { FX, "f..............." }, { K2, "x..............." } }),
                G ("Drum & Bass", "Minimal DnB", 170, 0.06f, { { K, "x.......x.x....." }, { S, "....x.....g.x..." }, { P1, "..g...g...g....." },
                    { P4, "x....x....x....x....x...." }, { CH, "..x.......x....." } }),

                // ---- Jungle: chopped-break ghost notes, busy snares, ride
                G ("Jungle", "Break Chop", 170, 0.0f, { { K, "x.x.......xx...." }, { S, "....X..g.g..X..g" }, { CY, "x.x.x.x.x.x.x.x." },
                    { CH, ".g.g.g.g.g.g.g.g" } }),
                G ("Jungle", "Jungle Roll", 170, 0.0f, { { K, "x.........x....." }, { S, "....x..g.x..x.gg" }, { CH, "xgxgxgxgxgxgxgxg" },
                    { CY, "x...x...x...x..." } }),
                G ("Jungle", "Ragga Jungle", 168, 0.05f, { { K, "x..x......x....." }, { S, "....x.......x..." }, { P1, "..g..g..g..g..g." },
                    { TL, "...........x..x." }, { FX, "x..............................." } }),
                G ("Jungle", "Jungle Breakdown", 170, 0.0f, { { K, "x.x.......x....." }, { S, "....x..g.g..x.ff" }, { CY, "x.x.x.x.x.x.x.x." },
                    { TL, "............ff.." }, { TH, "..............ff" } }),

                // ---- Footwork / juke: 160, triplet-ish kick stabs
                G ("Footwork", "Footwork Stabs", 160, 0.0f, { { K, "x..x..x...x..x.." }, { C, "....x.......x..." }, { CH, "x.x.x.x.x.x.x.x." },
                    { TL, "..x..x..x..x..x." } }),
                G ("Footwork", "Juke Stutter", 160, 0.0f, { { K, "x..x..x.x..x..x." }, { C, ".........x......" }, { CH, "x.x.r.x.x.x.t.x." },
                    { P1, "..x.....x.....x." } }),
                G ("Footwork", "Footwork Triplets", 160, 0.0f, { { K, "x..x..x...x..x..x..x..x." }, { C, "......x.....x....." }, { CH, "x.xx.xx.x" } }, 4),

                // ---- Broken beat / bruk
                G ("Broken Beat", "Bruk", 125, 0.12f, { { K, "x.....x..x......" }, { S, "....x......x..x." }, { CH, "..x...x...x...x." },
                    { P1, ".g..g...g..g...." } }),
                G ("Broken Beat", "Bruk Two", 124, 0.15f, { { K, "x..x....x.x....." }, { S, "....x.......x.g." }, { CH, "x.x.x.xx.x.x.x.x" },
                    { P1, "..g......g......" }, { OH, "......o.......o." } }),
                G ("Broken Beat", "West London Shuffle", 122, 0.2f, { { K, "x.....x...x..x.." }, { S, "....x......x...." }, { CH, "..x.x.x...x.x.x." },
                    { P2, "x.x.x.x.x.x.x.x." }, { P1, ".g.....g........" } }),

                // ---- UK bass / bleep
                G ("UK Bass / Bleep", "Bleep Stepper", 130, 0.0f, { { K, "x...x...x...x..." }, { C, "....x.......x..." }, { P1, "..x..x...x..x.x." },
                    { P2, "......x.......x." }, { CH, "..x...x...x...x." } }),
                G ("UK Bass / Bleep", "UK Bass Swing", 130, 0.18f, { { K, "x..x....x..x...." }, { S, "....x.......x..." }, { CH, "..x.x.x...x.x.x." },
                    { P4, "...x......x....." }, { P1, ".........x.....x" } }),
                G ("UK Bass / Bleep", "Bassline Bounce", 132, 0.12f, { { K, "x...x...x...x..." }, { C, "....x.......x..." }, { OH, "..x...x...x...x." },
                    { CH, "x.xxx.xxx.xxx.xx" }, { P1, "...x.....x...x.." } }),

                // ---- Techno / warehouse: 4x4, rumble, metallic polymeter
                G ("Techno / Warehouse", "Warehouse Rumble", 134, 0.0f, { { K, "x...x...x...x..." }, { K2, "..g...g...g...g." }, { CH, "..x...x...x...x." },
                    { P1, "x..x..x..x.." }, { C, "....x.......x..." } }),
                G ("Techno / Warehouse", "Broken Techno", 132, 0.04f, { { K, "x..x....x.x....." }, { S, "....x......x...." }, { P1, "..x..x...x..x..." },
                    { P2, "x.." }, { CH, "x.x.x.x.x.x.x.x." } }),
                G ("Techno / Warehouse", "Industrial Pulse", 136, 0.0f, { { K, "x...x...x...x..." }, { P1, "r..x.r..x..r.x.." }, { P2, "....x.......x..." },
                    { OH, "..x...x...x...x." }, { P6, "x....x....x....x...." } }),
                G ("Techno / Warehouse", "Tool Techno", 130, 0.0f, { { K, "x...x...x...x..." }, { CH, "xgXgxgXgxgXgxgXg" }, { P1, "x...." },
                    { C, "........x......." }, { P4, "x......" } }),

                // ---- Electro
                G ("Electro", "Electro Classic", 128, 0.05f, { { K, "x......x..x....." }, { S, "....x.......x..." }, { CH, "xxxxxxxxxxxxxxxx" },
                    { TH, ".........x.x...." } }),
                G ("Electro", "Broken Electro", 126, 0.05f, { { K, "x..x......x..x.." }, { S, "....x..x....x..." }, { P1, "......x.......x." },
                    { CH, "x.x.x.x.x.x.x.x." }, { TL, "..............ff" } }),
                G ("Electro", "Electro Bounce", 130, 0.08f, { { K, "x.....x..x......" }, { C, "....x.......x..." }, { CH, "x.xxx.xxx.xxx.xx" },
                    { P2, "...x.......x...." }, { P1, "..........x....x" } }),

                // ---- IDM / glitch: ratchets, chance, polymeter
                G ("IDM / Glitch", "Glitch Ratchets", 160, 0.0f, { { K, "x...x..x..x....." }, { S, "....x...t...x..R" }, { CH, "rxrxtxrxrxtxrxRx" },
                    { P1, "o.o.o.o.o.o.o.o." }, { P2, "x....." } }),
                G ("IDM / Glitch", "Drill Chaos", 170, 0.0f, { { K, "x.x..x.xx..x.x.." }, { S, ".x..x.t..x.xR.x." }, { CH, "RRxxttxxRRxxttxx" },
                    { P1, "o..o..o..o..o..o" }, { P6, "..x.o..x.o.." } }),
                G ("IDM / Glitch", "Microbeat", 120, 0.06f, { { K, "x.......x..x...." }, { P1, "x...." }, { P2, "x......" }, { P3, "x.........." },
                    { CH, "oooooooooooooooo" }, { S, "....x.........x." } }),
                G ("IDM / Glitch", "Skittering", 140, 0.0f, { { K, "x.......x.x....." }, { S, "....x.......x..." }, { CH, "xotoxoRoxotoxoro" },
                    { P1, "..o...o...o...o." } }),

                // ---- Beat tape / wonky: heavy swing, drunk timing
                G ("Beat Tape / Wonky", "Wonky Swing", 88, 0.45f, { { K, "x.....>..x......" }, { S, "....x......<x..." }, { CH, "x.x.x.x.x.x.x.x." },
                    { P2, "..x...x...x...x." } }),
                G ("Beat Tape / Wonky", "Drunk Drums", 92, 0.3f, { { K, "x....>...x.<...." }, { S, "....>.......x..<" }, { CH, "x.>.x.<.x.>.x.<." },
                    { C, "............x..." } }),
                G ("Beat Tape / Wonky", "Lo-Fi Head Nod", 86, 0.38f, { { K, "x.......x.x....." }, { S, "....x.......x..." }, { CH, "x.xgx.xgx.xgx.xg" },
                    { P2, "..x...x...x...x." } }),
                G ("Beat Tape / Wonky", "Off-Kilter", 95, 0.25f, { { K, "x..x...x..x....." }, { S, ".....x.......x.." }, { CH, "x.x.x>x.x.x.x<x." },
                    { P1, "...g......g....." }, { FX, "...............f" } }),
            };
        }

        Step stepFor (char ch)
        {
            Step s;
            s.on = ch != '.';
            switch (ch)
            {
                case 'X': s.vel = 124; break;
                case 'g': s.vel = 42; break;
                case 'o': s.vel = 90; s.prob = 50; break;
                case 'r': s.ratchet = 2; break;
                case 't': s.ratchet = 3; break;
                case 'R': s.ratchet = 4; s.shape = RatchetShape::Up; break;
                case 'f': s.cond = TrigCond::Fill; break;
                case 'n': s.cond = TrigCond::NotFill; break;
                case '2': s.cond = TrigCond::R2of2; break;
                case '<': s.nudge = -18; break;
                case '>': s.nudge = 22; break;
                default: break;
            }
            return s;
        }
    }

    const std::vector<Groove>& all()
    {
        static const std::vector<Groove> list = build();
        return list;
    }

    void apply (const Groove& g, StepPattern& p)
    {
        for (int l = 0; l < stepLanes; ++l)
        {
            auto& lane = p.lanes[(size_t) l];
            for (auto& s : lane.steps) s = Step{};
            const auto& str = g.lanes[(size_t) l];
            if (str.empty()) { lane.length = 16; continue; }
            lane.length = (uint8_t) std::clamp ((int) str.size(), 1, maxSteps);
            lane.mute = false;
            lane.speed = defaultLaneSpeed;
            for (int i = 0; i < lane.length; ++i) lane.steps[(size_t) i] = stepFor (str[(size_t) i]);
        }
    }
}
