#pragma once
// BASH! core — reading audio files (WAV, AIFF, MP3, FLAC) into SampleData, and writing WAVs.

#include "bash/Slices.h"

#include <memory>
#include <string>

namespace bash
{
    /** Decode up to maxSeconds of a file (stereo kept, more channels folded to 2). nullptr + error on failure. */
    std::shared_ptr<SampleData> decodeAudioFile (const std::string& path, double maxSeconds, std::string& error);
    /** Write a 24-bit stereo WAV. */
    bool writeWavFile (const std::string& path, const SampleData&);
}
