#pragma once
// The factory library on disk: rendered once (in the background) into
// Music/BASH Library, shared by every BASH! instance in the process.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "bash/Library.h"

#include <atomic>

class FactoryLibrary : private juce::Thread
{
public:
    FactoryLibrary();
    ~FactoryLibrary() override;

    /** Music/BASH Library (Documents/BASH Library on iPad). */
    static juce::File root();
    bool ready() const noexcept       { return isReady.load(); }
    float progress() const noexcept   { return prog.load(); }
    juce::String error() const        { const juce::ScopedLock sl (lock); return err; }

    juce::File fileFor (const bash::library::OneShot&) const;
    juce::File fileFor (const bash::library::Loop&) const;
    /** "Kicks/Sub Kick F" -> its file (it may not exist yet). */
    juce::File oneShotFile (const std::string& categoryAndName) const;

private:
    void run() override;
    static bool writeWav (const juce::File&, const bash::SampleData&);

    std::atomic<bool> isReady { false };
    std::atomic<float> prog { 0.0f };
    juce::CriticalSection lock;
    juce::String err;
};
