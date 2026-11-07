#include "bash/AudioFile.h"

#define DR_WAV_IMPLEMENTATION
#include "../third_party/dr_libs/dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#include "../third_party/dr_libs/dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#include "../third_party/dr_libs/dr_flac.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace bash
{
    namespace
    {
        std::string lowerExt (const std::string& path)
        {
            const auto dot = path.find_last_of ('.');
            std::string e = dot == std::string::npos ? std::string() : path.substr (dot + 1);
            std::transform (e.begin(), e.end(), e.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
            return e;
        }

        std::shared_ptr<SampleData> fromInterleaved (const float* data, uint64_t frames, unsigned channels, double rate, const std::string& path)
        {
            auto s = std::make_shared<SampleData>();
            s->sampleRate = rate;
            s->path = path;
            s->left.resize ((size_t) frames);
            s->right.resize ((size_t) frames);
            for (uint64_t i = 0; i < frames; ++i)
            {
                const float* f = data + i * channels;
                if (channels == 1) { s->left[(size_t) i] = s->right[(size_t) i] = f[0]; }
                else { s->left[(size_t) i] = f[0]; s->right[(size_t) i] = f[1]; }
            }
            return s;
        }
    }

    std::shared_ptr<SampleData> decodeAudioFile (const std::string& path, double maxSeconds, std::string& error)
    {
        const auto ext = lowerExt (path);
        std::vector<float> buf;
        unsigned channels = 0;
        double rate = 0.0;
        uint64_t frames = 0;

        if (ext == "mp3")
        {
            drmp3 mp3;
            if (! drmp3_init_file (&mp3, path.c_str(), nullptr)) { error = "Couldn't read " + path; return nullptr; }
            channels = mp3.channels; rate = mp3.sampleRate;
            const uint64_t want = (uint64_t) (maxSeconds * rate);
            buf.resize ((size_t) (want * channels));
            frames = drmp3_read_pcm_frames_f32 (&mp3, want, buf.data());
            drmp3_uninit (&mp3);
        }
        else if (ext == "flac")
        {
            drflac* fl = drflac_open_file (path.c_str(), nullptr);
            if (fl == nullptr) { error = "Couldn't read " + path; return nullptr; }
            channels = fl->channels; rate = fl->sampleRate;
            const uint64_t want = std::min<uint64_t> (fl->totalPCMFrameCount, (uint64_t) (maxSeconds * rate));
            buf.resize ((size_t) (want * channels));
            frames = drflac_read_pcm_frames_f32 (fl, want, buf.data());
            drflac_close (fl);
        }
        else
        {
            drwav wav;   // WAV, RF64, W64, AIFF
            if (! drwav_init_file (&wav, path.c_str(), nullptr)) { error = "Couldn't read " + path; return nullptr; }
            channels = wav.channels; rate = wav.sampleRate;
            const uint64_t want = std::min<uint64_t> (wav.totalPCMFrameCount, (uint64_t) (maxSeconds * rate));
            buf.resize ((size_t) (want * channels));
            frames = drwav_read_pcm_frames_f32 (&wav, want, buf.data());
            drwav_uninit (&wav);
        }
        if (frames == 0 || channels == 0 || rate <= 0.0) { error = path + " is empty"; return nullptr; }
        return fromInterleaved (buf.data(), frames, channels, rate, path);
    }

    bool writeWavFile (const std::string& path, const SampleData& s)
    {
        drwav_data_format fmt {};
        fmt.container = drwav_container_riff;
        fmt.format = DR_WAVE_FORMAT_PCM;
        fmt.channels = 2;
        fmt.sampleRate = (drwav_uint32) s.sampleRate;
        fmt.bitsPerSample = 24;
        drwav wav;
        if (! drwav_init_file_write (&wav, path.c_str(), &fmt, nullptr)) return false;
        std::vector<uint8_t> bytes ((size_t) s.numFrames() * 6);
        for (uint32_t i = 0; i < s.numFrames(); ++i)
            for (int c = 0; c < 2; ++c)
            {
                const float v = std::clamp (c == 0 ? s.left[i] : s.right[i], -1.0f, 1.0f);
                const int32_t q = (int32_t) (v * 8388607.0f);
                uint8_t* b = bytes.data() + (size_t) i * 6 + (size_t) c * 3;
                b[0] = (uint8_t) (q & 0xff); b[1] = (uint8_t) ((q >> 8) & 0xff); b[2] = (uint8_t) ((q >> 16) & 0xff);
            }
        const auto written = drwav_write_pcm_frames (&wav, s.numFrames(), bytes.data());
        drwav_uninit (&wav);
        return written == s.numFrames();
    }
}
