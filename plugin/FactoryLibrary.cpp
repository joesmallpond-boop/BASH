#include "FactoryLibrary.h"

FactoryLibrary::FactoryLibrary() : juce::Thread ("BASH library")
{
    startThread (juce::Thread::Priority::low);
}

FactoryLibrary::~FactoryLibrary()
{
    stopThread (15000);
}

juce::File FactoryLibrary::root()
{
   #if JUCE_IOS
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("BASH Library");
   #else
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("BASH Library");
   #endif
}

juce::File FactoryLibrary::fileFor (const bash::library::OneShot& s) const
{
    return root().getChildFile (juce::String (bash::library::oneShotPath (s)) + ".wav");
}

juce::File FactoryLibrary::fileFor (const bash::library::Loop& l) const
{
    return root().getChildFile (juce::String (bash::library::loopPath (l)) + ".wav");
}

juce::File FactoryLibrary::oneShotFile (const std::string& categoryAndName) const
{
    return root().getChildFile ("Drums/" + juce::String (categoryAndName) + ".wav");
}

bool FactoryLibrary::writeWav (const juce::File& file, const bash::SampleData& d)
{
    file.getParentDirectory().createDirectory();
    // write next to it, then move into place: another instance never sees half a file
    const auto tmp = file.getSiblingFile (file.getFileName() + ".part");
    tmp.deleteFile();
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out (tmp.createOutputStream());
        if (out == nullptr) return false;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (out.get(), d.sampleRate, 2, 24, {}, 0));
        if (w == nullptr) return false;
        out.release();   // the writer owns the stream now
        const float* chans[] = { d.left.data(), d.right.data() };
        if (! w->writeFromFloatArrays (chans, 2, (int) d.numFrames())) return false;
    }
    return tmp.moveFileTo (file);
}

void FactoryLibrary::run()
{
    const auto dir = root();
    const auto stamp = dir.getChildFile (".bash-library-version");
    const juce::String want (bash::library::version);
    const bool current = stamp.existsAsFile() && stamp.loadFileAsString().trim() == want;

    const auto& shots = bash::library::oneShots();
    const auto& loops = bash::library::loops();
    const float total = (float) (shots.size() + loops.size());
    int done = 0, failed = 0;

    auto make = [&] (const juce::File& f, auto&& render)
    {
        if (threadShouldExit()) return;
        if (! (current && f.existsAsFile()))   // re-render when the library version changed, or a file is missing
        {
            auto d = render();
            if (d == nullptr || ! writeWav (f, *d)) ++failed;
        }
        prog.store ((float) ++done / total);
    };
    for (const auto& s : shots) make (fileFor (s), s.render);
    for (const auto& l : loops) make (fileFor (l), l.render);
    if (threadShouldExit()) return;

    if (failed == 0)
    {
        stamp.replaceWithText (want);
        isReady.store (true);
    }
    else
    {
        const juce::ScopedLock sl (lock);
        err = "Couldn't write the sound library to " + dir.getFullPathName();
    }
}
