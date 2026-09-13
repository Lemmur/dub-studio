// WAV 24-bit writer через dr_wav (single-header, third_party/dr_wav).
#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>

#include "dubstudio/wav_writer.h"

#include <cstring>
#include <stdexcept>

namespace dubstudio {

std::uint64_t WavWriter::writePcm24(const std::string& path, const std::vector<float>& samples,
                                    std::uint32_t sampleRate) {
    drwav_data_format fmt{};
    fmt.container = drwav_container_riff;
    fmt.format = DR_WAVE_FORMAT_PCM;
    fmt.channels = 1;
    fmt.sampleRate = sampleRate;
    fmt.bitsPerSample = 24;

    drwav wav;
    if (!drwav_init_file_write(&wav, path.c_str(), &fmt, nullptr)) {
        throw std::runtime_error("Не удалось создать WAV: " + path);
    }

    // float -> s24 (3 байта LE на сэмпл).
    std::vector<unsigned char> bytes;
    const constexpr std::size_t kChunk = 4096;
    bytes.resize(kChunk * 3);
    std::uint64_t written = 0;
    while (written < samples.size()) {
        const std::size_t n =
            static_cast<std::size_t>(std::min<std::uint64_t>(kChunk, samples.size() - written));
        for (std::size_t i = 0; i < n; ++i) {
            double s = samples[written + i];
            if (s > 1.0) s = 1.0;
            if (s < -1.0) s = -1.0;
            // Округление к ближайшему, клиппинг на границах int24.
            long v = lround(s * 8388607.0);
            if (v > 8388607) v = 8388607;
            if (v < -8388608) v = -8388608;
            const auto u = static_cast<std::uint32_t>(static_cast<std::int32_t>(v));
            bytes[i * 3 + 0] = static_cast<unsigned char>(u & 0xFF);
            bytes[i * 3 + 1] = static_cast<unsigned char>((u >> 8) & 0xFF);
            bytes[i * 3 + 2] = static_cast<unsigned char>((u >> 16) & 0xFF);
        }
        written += drwav_write_pcm_frames(&wav, n, bytes.data());
    }
    drwav_uninit(&wav);
    return written;
}

bool WavWriter::readMono(const std::string& path, std::vector<float>& samples,
                         std::uint32_t& sampleRate) {
    drwav wav;
    if (!drwav_init_file(&wav, path.c_str(), nullptr)) return false;
    sampleRate = wav.sampleRate;
    const std::uint64_t frames = wav.totalPCMFrameCount;
    samples.resize(static_cast<std::size_t>(frames));
    if (frames > 0) {
        drwav_read_pcm_frames_f32(&wav, frames, samples.data());
    }
    drwav_uninit(&wav);
    return true;
}

} // namespace dubstudio
