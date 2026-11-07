// bash~ — BASH! for Max for Live. One channel: a sequencer (generative acid or step)
// driving a synth, slicer or drum kit. All the work happens in bash::Instrument (core);
// this file only connects it to Max: audio out, messages in and out, Live's transport.
//
// Outlets (left to right): signal L, signal R, signal LFO 1 map, signal LFO 2 map (each in its
// mapped parameter's units, for live.remote~), messages (ui / state / display data / msg).
// Every message into the left inlet goes to Instrument::command (see core/src/Instrument.cpp).

#include "ext.h"
#include "ext_obex.h"
#include "ext_itm.h"
#include "ext_path.h"
#include "z_dsp.h"

#include "bash/Instrument.h"

#include <atomic>
#include <cmath>
#include <new>
#include <string>
#include <vector>

namespace
{
    t_class* bashClass = nullptr;
    constexpr double frameMs = 40.0;   // displays run at 25 fps

    struct Bash
    {
        t_pxobject ob;
        void* msgOut;
        void* clock;
        void* midiClock;
        void* qelem;
        bash::Instrument* inst;
        std::vector<float>* bufL;
        std::vector<float>* bufR;
        double sr;
        // transport: a continuous position, resynced to Max's (Live's) when they drift apart
        double ppq;
        bool   wasPlaying;
        std::atomic<bool>* alive;
    };

    // ---- messages out -------------------------------------------------------------------
    void emitTo (Bash* x, const std::string& sel, const bash::Atoms& args)
    {
        std::vector<t_atom> av (args.size());
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (const double* d = std::get_if<double> (&args[i]))
            {
                if (std::floor (*d) == *d && std::abs (*d) < 2147483647.0) atom_setlong (&av[i], (t_atom_long) *d);
                else atom_setfloat (&av[i], *d);
            }
            else atom_setsym (&av[i], gensym (std::get<std::string> (args[i]).c_str()));
        }
        outlet_anything (x->msgOut, gensym (sel.c_str()), (short) av.size(), av.empty() ? nullptr : av.data());
    }

    // ---- the display timer: clock (any thread) -> qelem (main thread) -> tick ------------------
    void tickMain (Bash* x)
    {
        x->inst->tick();
    }

    void clockFired (Bash* x)
    {
        qelem_set (x->qelem);
        clock_fdelay (x->clock, frameMs);
    }

    // BASH! Sequencer: send the sequencer's notes on as "midi <note> <velocity>" (scheduler thread)
    void midiFired (Bash* x)
    {
        bash::Engine::MidiNote notes[128];
        const int n = x->inst->takeMidi (notes, 128);
        for (int i = 0; i < n; ++i)
        {
            t_atom av[2];
            atom_setlong (av, notes[i].note);
            atom_setlong (av + 1, notes[i].velocity);
            outlet_anything (x->msgOut, gensym ("midi"), 2, av);
        }
        clock_fdelay (x->midiClock, 1.0);
    }

    std::string conformPath (const std::string& maxPath)
    {
        char out[MAX_PATH_CHARS];
        if (path_nameconform (maxPath.c_str(), out, PATH_STYLE_NATIVE, PATH_TYPE_BOOT) == 0) return out;
        return maxPath;
    }

    // ---- messages in ----------------------------------------------------------------------------
    void anything (Bash* x, t_symbol* s, long ac, t_atom* av)
    {
        bash::Atoms args;
        args.reserve ((size_t) ac);
        for (long i = 0; i < ac; ++i)
        {
            switch (atom_gettype (av + i))
            {
                case A_LONG:  args.push_back ((double) atom_getlong (av + i)); break;
                case A_FLOAT: args.push_back ((double) atom_getfloat (av + i)); break;
                case A_SYM:   args.push_back (std::string (atom_getsym (av + i)->s_name)); break;
                default: break;
            }
        }
        std::string sel = s->s_name;

        // file paths from live.drop / opendialog arrive in Max's style: make them native
        auto joinFrom = [&] (size_t from)
        {
            std::string p;
            for (size_t k = from; k < args.size(); ++k) { if (k > from) p += ' '; p += bash::atomStr (args[k]); }
            return p;
        };
        if (sel == "load" && ! args.empty())
            args = { conformPath (joinFrom (0)) };
        else if (sel == "pad" && args.size() >= 3 && bash::atomStr (args[0]) == "load")
            args = { args[0], args[1], conformPath (joinFrom (2)) };

        x->inst->command (sel, args);
    }

    void listIn (Bash* x, t_symbol*, long ac, t_atom* av)
    {
        // a bare list: treat as "param <id> <value>" if it starts with a word, else ignore
        if (ac >= 2 && atom_gettype (av) == A_SYM) anything (x, gensym ("param"), ac, av);
    }

    // ---- audio ------------------------------------------------------------------------------------
    void perform64 (Bash* x, t_object*, double**, long, double** outs, long numouts, long n, long, void*)
    {
        if (numouts < 4) return;
        if ((long) x->bufL->size() < n) { for (int o = 0; o < 4; ++o) for (long i = 0; i < n; ++i) outs[o][i] = 0.0; return; }

        // Live's transport (Max's global ITM follows Live inside Max for Live)
        bash::Transport tp;
        if (void* itmPtr = itm_getglobal())
        {
            auto* itm = (t_itm*) itmPtr;
            const double tempo = itm_gettempo (itm);
            const bool playing = itm_getstate (itm) != 0;
            const double hostPpq = itm_getticks (itm) / 480.0;
            if (tempo > 0.0)
            {
                tp.hostAvailable = true;
                tp.hostBpm = tempo;
                tp.hostPlaying = playing;
                // ticks move in scheduler steps, not per sample: keep a smooth position and only
                // jump when the transport really moved (start, loop, locate)
                if (! playing) x->ppq = hostPpq;
                else if (! x->wasPlaying || std::abs (hostPpq - x->ppq) > 0.1) x->ppq = hostPpq;
                tp.hostPpq = x->ppq;
                if (playing) x->ppq += (double) n * tempo / 60.0 / x->sr;
                x->wasPlaying = playing;
            }
        }

        float* l = x->bufL->data();
        float* r = x->bufR->data();
        x->inst->process (l, r, (int) n, tp);
        const double m1 = x->inst->mapOutput (0), m2 = x->inst->mapOutput (1);
        for (long i = 0; i < n; ++i) { outs[0][i] = l[i]; outs[1][i] = r[i]; outs[2][i] = m1; outs[3][i] = m2; }
    }

    void dsp64 (Bash* x, t_object* dsp, short*, double samplerate, long maxvectorsize, long)
    {
        x->sr = samplerate > 0.0 ? samplerate : 44100.0;
        const long size = std::max<long> (maxvectorsize, 4096);
        x->bufL->assign ((size_t) size, 0.0f);
        x->bufR->assign ((size_t) size, 0.0f);
        x->inst->prepare (x->sr, (int) maxvectorsize);
        object_method (dsp, gensym ("dsp_add64"), x, (method) perform64, 0, nullptr);
    }

    void assist (Bash*, void*, long m, long a, char* s)
    {
        if (m == ASSIST_INLET) snprintf_zero (s, 256, "Messages: param, note, key, load, pad, step, lane, dice ...");
        else if (a == 0) snprintf_zero (s, 256, "(signal) Left");
        else if (a == 1) snprintf_zero (s, 256, "(signal) Right");
        else if (a == 2) snprintf_zero (s, 256, "(signal) LFO 1 for its mapped parameter");
        else if (a == 3) snprintf_zero (s, 256, "(signal) LFO 2 for its mapped parameter");
        else snprintf_zero (s, 256, "ui / state / display data");
    }

    void* newBash (t_symbol*, long, t_atom*)
    {
        auto* x = (Bash*) object_alloc (bashClass);
        if (x == nullptr) return nullptr;
        dsp_setup ((t_pxobject*) x, 0);
        x->msgOut = outlet_new (x, nullptr);          // rightmost
        outlet_new (x, "signal");                      // LFO 2 map
        outlet_new (x, "signal");                      // LFO 1 map
        outlet_new (x, "signal");                      // R
        outlet_new (x, "signal");                      // L (leftmost)
        x->inst = new bash::Instrument();
        x->bufL = new std::vector<float> (4096, 0.0f);
        x->bufR = new std::vector<float> (4096, 0.0f);
        x->sr = 44100.0;
        x->ppq = 0.0;
        x->wasPlaying = false;
        x->alive = new std::atomic<bool> (true);
        x->inst->setEmitter ([x] (const std::string& sel, const bash::Atoms& args) { emitTo (x, sel, args); });
        x->qelem = qelem_new (x, (method) tickMain);
        x->clock = clock_new (x, (method) clockFired);
        clock_fdelay (x->clock, frameMs);
        x->midiClock = clock_new (x, (method) midiFired);
        clock_fdelay (x->midiClock, 1.0);
        return x;
    }

    void freeBash (Bash* x)
    {
        dsp_free ((t_pxobject*) x);
        clock_unset (x->clock);
        object_free (x->clock);
        clock_unset (x->midiClock);
        object_free (x->midiClock);
        qelem_free (x->qelem);
        x->inst->setEmitter (nullptr);
        delete x->inst;
        delete x->bufL;
        delete x->bufR;
        delete x->alive;
    }
}

extern "C" C74_EXPORT void ext_main (void*)
{
    t_class* c = class_new ("bash~", (method) newBash, (method) freeBash, (long) sizeof (Bash), 0L, A_GIMME, 0);
    class_addmethod (c, (method) dsp64, "dsp64", A_CANT, 0);
    class_addmethod (c, (method) anything, "anything", A_GIMME, 0);
    class_addmethod (c, (method) listIn, "list", A_GIMME, 0);
    class_addmethod (c, (method) assist, "assist", A_CANT, 0);
    class_dspinit (c);
    class_register (CLASS_BOX, c);
    bashClass = c;
}
