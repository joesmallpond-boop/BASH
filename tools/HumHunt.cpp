// BASH! hum hunt: roll the dice many times, play, stop, and report anything that
// keeps sounding (drones / hums) per track output and on the main bus, with L vs R.
#include "../plugin/PluginProcessor.h"

#include <cmath>
#include <cstdio>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const int rolls = argc > 1 ? std::atoi (argv[1]) : 40;
    const int seqOnly = argc > 2 ? std::atoi (argv[2]) : 0;
    const double sr = 48000.0; const int bs = 256;
    int bad = 0;

    for (int roll = 0; roll < rolls; ++roll)
    {
        BashProcessor p;
        auto layout = p.getBusesLayout();
        for (auto& b : layout.outputBuses) b = juce::AudioChannelSet::stereo();
        p.setBusesLayout (layout);
        const int chans = p.getTotalNumOutputChannels();
        p.setPlayConfigDetails (0, chans, sr, bs);
        p.prepareToPlay (sr, bs);
        p.apvts.getParameter ("sync")->setValueNotifyingHost (0.0f);
        for (int t = 1; t <= 8; ++t) p.apvts.getParameter ("t" + juce::String (t) + "_on")->setValueNotifyingHost (1.0f);
        if (seqOnly) { for (int t = 0; t < 8; ++t) p.rollDice (t); }
        else
        {
            // what the old "everything" die did: randomise every sound, FX, mixer and sequencer knob
            juce::Random rnd (1000 + roll);
            static const juce::StringArray keep { "sync", "play", "master", "limiter", "tempo", "root", "scale" };
            for (auto* prm : p.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                {
                    const auto id = rp->getParameterID();
                    const auto s = id.fromFirstOccurrenceOf ("_", false, false);
                    if (keep.contains (id) || s == "on" || s == "engine" || s == "mute" || s == "solo" || s == "level") continue;
                    rp->setValueNotifyingHost (rnd.nextFloat());
                }
        }
        p.apvts.getParameter ("play")->setValueNotifyingHost (1.0f);

        juce::AudioBuffer<float> buf (chans, bs);
        juce::MidiBuffer midi;
        auto run = [&] (double seconds, std::vector<double>* energy)
        {
            const int blocks = (int) (seconds * sr / bs);
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear(); midi.clear();
                p.processBlock (buf, midi);
                if (energy)
                    for (int c = 0; c < chans; ++c)
                    {
                        const float* x = buf.getReadPointer (c);
                        for (int i = 0; i < bs; ++i) (*energy)[(size_t) c] += (double) x[i] * x[i];
                    }
            }
        };
        run (6.0, nullptr);
        p.apvts.getParameter ("play")->setValueNotifyingHost (0.0f);
        run (12.0, nullptr);                         // let honest tails die
        std::vector<double> e ((size_t) chans, 0.0);
        run (3.0, &e);                               // anything left now is a drone
        const double n = 3.0 * sr;
        if (std::getenv ("BISECT"))
        {
            auto mainDb = [&]
            {
                std::vector<double> e2 ((size_t) chans, 0.0);
                run (1.0, &e2);
                return std::pair { 10 * std::log10 (e2[0] / sr + 1e-20), 10 * std::log10 (e2[1] / sr + 1e-20) };
            };
            auto [l0, r0] = mainDb();
            std::printf ("roll %d main L %.1f R %.1f\n", roll, l0, r0);
            for (auto id : { "retatype", "retbtype" })
            {
                p.apvts.getParameter (id)->setValueNotifyingHost (0.0f);
                run (0.2, nullptr);
                auto [l1, r1] = mainDb();
                std::printf ("   after %s off: L %.1f R %.1f\n", id, l1, r1);
            }
            continue;
        }
        for (int c = 0; c + 1 < chans; c += 2)
        {
            const double l = 10 * std::log10 (e[(size_t) c] / n + 1e-20), r = 10 * std::log10 (e[(size_t) c + 1] / n + 1e-20);
            if (l > -70 || r > -70)
            {
                ++bad;
                const int bus = c / 2;
                std::printf ("roll %d  %s  L %.1f dB  R %.1f dB\n", roll, bus == 0 ? "MAIN(returns/master)" : ("track " + std::to_string (bus)).c_str(), l, r);
                const int t = bus;
                auto pv = [&] (const juce::String& id) { auto* q = p.apvts.getParameter (id); return q ? q->getCurrentValueAsText() : juce::String ("?"); };
                if (bus > 0)
                {
                    const juce::String pre = "t" + juce::String (t) + "_";
                    std::printf ("    engine %s  fx1 %s a %s b %s g %s | fx2 %s a %s b %s g %s\n",
                        pv (pre + "engine").toRawUTF8(), pv (pre + "fx1type").toRawUTF8(), pv (pre + "fx1a").toRawUTF8(), pv (pre + "fx1b").toRawUTF8(), pv (pre + "fx1gnarl").toRawUTF8(),
                        pv (pre + "fx2type").toRawUTF8(), pv (pre + "fx2a").toRawUTF8(), pv (pre + "fx2b").toRawUTF8(), pv (pre + "fx2gnarl").toRawUTF8());
                }
                else
                {
                    for (auto id : { "reta", "retb" })
                    {
                        const juce::String s (id);
                        std::printf ("    %s %s a %s b %s g %s\n", id, pv (s + "type").toRawUTF8(), pv (s + "a").toRawUTF8(), pv (s + "b").toRawUTF8(), pv (s + "gnarl").toRawUTF8());
                    }
                }
            }
        }
    }
    std::printf ("%d droning outputs in %d rolls\n", bad, rolls);
    return 0;
}
