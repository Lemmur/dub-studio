// WAV writer/reader для тейков: моно PCM 24-bit (дефолт проекта 48000/24,
// AGENTS.md правило 9). Пишет через dr_wav (MIT).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dubstudio {

class WavWriter {
public:
    // Пишет samples (моно float32, [-1..1]) в path как PCM 24-bit.
    // Возвращает число записанных кадров. Кидаёт std::runtime_error при ошибке.
    static std::uint64_t writePcm24(const std::string& path,
                                    const std::vector<float>& samples,
                                    std::uint32_t sampleRate);

    // Читает WAV (любой поддерживаемый dr_wav) в моно float32 + sample rate.
    // Используется для валидации roundtrip и загрузки тейков.
    static bool readMono(const std::string& path, std::vector<float>& samples,
                         std::uint32_t& sampleRate);
};

} // namespace dubstudio
