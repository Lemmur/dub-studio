// Тесты Фазы 1 (аудио): ring buffer, метроном, микс processBlock, WAV roundtrip,
// пики волноформы, скомпилированные API. Готовность фазы (ручной тест на
// железе): запись 10 сек без xrun — здесь проверяем логику без устройства.
#include "catch_amalgamated.hpp"

#include "dubstudio/audio_engine.h"
#include "dubstudio/clip_store.h"
#include "dubstudio/ring_buffer.h"
#include "dubstudio/wav_writer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using Catch::Approx; // Catch2 v3: Approx больше не в глобальном namespace
using Catch::Approx; // Catch2 v3: Approx больше не в глобальном namespace

using dubstudio::AudioEngine;
using dubstudio::Clip;
using dubstudio::ClipStore;
using dubstudio::RingBuffer;
using dubstudio::WavWriter;

// --- RingBuffer ---------------------------------------------------------------

TEST_CASE("RingBuffer: порядок и полнота при seq push/pop", "[audio]") {
    RingBuffer rb(64);
    std::vector<float> in(10);
    for (int i = 0; i < 10; ++i) in[static_cast<std::size_t>(i)] = static_cast<float>(i);

    CHECK(rb.push(in.data(), 10) == 10);
    std::vector<float> out(10, -1.0f);
    CHECK(rb.pop(out.data(), 10) == 10);
    for (int i = 0; i < 10; ++i) {
        CHECK(out[static_cast<std::size_t>(i)] == static_cast<float>(i));
    }
    CHECK(rb.framesAvailable() == 0);
}

TEST_CASE("RingBuffer: wraparound через границу буфера", "[audio]") {
    RingBuffer rb(8); // ёмкость станет 8
    std::vector<float> out(4);
    std::vector<float> in = {1, 2, 3, 4, 5, 6};

    CHECK(rb.push(in.data(), 6) == 6);
    CHECK(rb.pop(out.data(), 4) == 4); // читаем 1..4, в буфере 5,6
    // writeIndex около границы: следующий push завернётся через начало.
    CHECK(rb.push(in.data(), 6) == 6);
    std::vector<float> out2(8, 0);
    CHECK(rb.pop(out2.data(), 8) == 8);
    // Итог: хвост 5,6 + новые 1,2,3,4,5,6.
    const float exp2[] = {5, 6, 1, 2, 3, 4, 5, 6};
    for (int i = 0; i < 8; ++i) {
        CHECK(out2[static_cast<std::size_t>(i)] == exp2[i]);
    }
}

TEST_CASE("RingBuffer: переполнение не блокирует и не портит данные", "[audio]") {
    RingBuffer rb(8);
    std::vector<float> in(16, 0.5f);
    CHECK(rb.push(in.data(), 8) == 8);
    CHECK(rb.push(in.data(), 8) == 0); // полно
    std::vector<float> out(8, -1);
    CHECK(rb.pop(out.data(), 8) == 8);
    for (float v : out) CHECK(v == 0.5f);
}

TEST_CASE("RingBuffer: SPSC из двух потоков", "[audio]") {
    RingBuffer rb(1024);
    constexpr int kTotal = 100000;
    std::vector<float> produced;
    produced.reserve(kTotal);

    std::thread producer([&] {
        for (int i = 0; i < kTotal; i += 64) {
            const int n = std::min(64, kTotal - i);
            std::vector<float> chunk(static_cast<std::size_t>(n));
            for (int j = 0; j < n; ++j) chunk[static_cast<std::size_t>(j)] = static_cast<float>(i + j);
            std::uint64_t written = 0;
            while (written < static_cast<std::uint64_t>(n)) {
                written += rb.push(chunk.data() + written, static_cast<std::uint64_t>(n) - written);
            }
        }
    });
    std::vector<float> consumed;
    consumed.reserve(kTotal);
    std::thread consumer([&] {
        float buf[64];
        while (consumed.size() < kTotal) {
            const auto n = rb.pop(buf, 64);
            consumed.insert(consumed.end(), buf, buf + n);
        }
    });
    producer.join();
    consumer.join();
    REQUIRE(consumed.size() == kTotal);
    for (int i = 0; i < kTotal; i += 997) {
        CHECK(consumed[static_cast<std::size_t>(i)] == static_cast<float>(i));
    }
}

// --- Метрики -------------------------------------------------------------------

TEST_CASE("rmsDb/peakDb: синус -6 dBFS", "[audio]") {
    std::vector<float> sine(48000);
    for (std::size_t i = 0; i < sine.size(); ++i) {
        sine[i] = 0.5f * std::sin(2.0 * 3.14159265 * 440.0 * i / 48000.0);
    }
    CHECK(dubstudio::peakDb(sine) == Approx(20.0 * std::log10(0.5)).epsilon(0.001));
    CHECK(dubstudio::rmsDb(sine) == Approx(20.0 * std::log10(0.5 / std::sqrt(2.0))).epsilon(0.01));
}

// --- processBlock (микс без устройства) ------------------------------------------

TEST_CASE("processBlock: мониторинг копирует вход с гейном", "[audio]") {
    AudioEngine e;
    e.setSoftwareMonitoring(true);
    e.setDirectMonitoring(false);
    e.setMonitoringGain(0.5f);

    std::vector<float> in(64, 0.8f);
    std::vector<float> out(128, -1.0f);
    e.processBlock(out.data(), in.data(), 64);
    for (int i = 0; i < 64; ++i) {
        CHECK(out[static_cast<std::size_t>(2 * i)] == Approx(0.4f));
        CHECK(out[static_cast<std::size_t>(2 * i + 1)] == Approx(0.4f));
    }
    CHECK(e.inputPeak() == Approx(0.8f));
}

TEST_CASE("processBlock: Direct Monitoring гасит софт-копию", "[audio]") {
    AudioEngine e;
    e.setSoftwareMonitoring(true);
    e.setDirectMonitoring(true); // аппаратный: софт-копия выключена

    std::vector<float> in(16, 0.8f);
    std::vector<float> out(32, -1.0f);
    e.processBlock(out.data(), in.data(), 16);
    for (float v : out) CHECK(v == 0.0f);
}

TEST_CASE("processBlock: запись наполняет кольцо, stopRecord возвращает сэмплы", "[audio]") {
    AudioEngine e;
    e.startRecord();
    REQUIRE(e.isRecording());

    for (int block = 0; block < 10; ++block) {
        std::vector<float> in(128);
        for (int i = 0; i < 128; ++i) {
            in[static_cast<std::size_t>(i)] = static_cast<float>(block * 128 + i);
        }
        std::vector<float> out(256);
        e.processBlock(out.data(), in.data(), 128);
    }
    CHECK(e.recordedFrames() == 1280);

    std::vector<float> live;
    e.drainRecorded(live);
    auto res = e.stopRecord();
    // Всё, что записано, должно быть получено (live + хвост).
    CHECK(live.size() + res.samples.size() == 1280);
    CHECK(res.sampleRate == 48000);
    CHECK_FALSE(e.isRecording());
}

TEST_CASE("processBlock: плейбек доигрывает до конца и глохнет", "[audio]") {
    AudioEngine e;
    std::vector<float> take(256, 0.25f);
    e.play(take.data(), take.size());
    REQUIRE(e.isPlaying());

    std::vector<float> out(512);
    std::vector<float> silence(128, 0.0f);
    e.processBlock(out.data(), silence.data(), 128);
    CHECK(e.isPlaying());
    CHECK(e.playheadFrames() == 128);
    e.processBlock(out.data(), silence.data(), 128);
    CHECK_FALSE(e.isPlaying()); // 256 кадров отыграны

    // Выход содержал сэмплы плейбека.
    CHECK(out[0] == Approx(0.25f));
    CHECK(out[510] == Approx(0.0f)); // второй блок — уже тишина
}

TEST_CASE("processBlock: метроном микширует клик", "[audio]") {
    AudioEngine e;
    e.setMetronomeEnabled(true);
    e.setTempo(120, 4); // акцент в самом начале

    std::vector<float> in(64, 0.0f);
    std::vector<float> out(128, 0.0f);
    e.processBlock(out.data(), in.data(), 64);
    float peak = 0.0f;
    for (float v : out) peak = std::max(peak, std::fabs(v));
    CHECK(peak > 0.3f); // клик слышимый

    // Дефолт (выкл) — тишина.
    AudioEngine e2;
    e2.setMetronomeEnabled(false);
    std::vector<float> out2(128, -1.0f);
    e2.processBlock(out2.data(), in.data(), 64);
    for (float v : out2) CHECK(v == 0.0f);
}

// --- WAV roundtrip ---------------------------------------------------------------

TEST_CASE("WavWriter: PCM 24-bit roundtrip", "[audio]") {
    const fs::path dir = fs::temp_directory_path() / "dubstudio_wav_test";
    fs::create_directories(dir);
    const std::string path = (dir / "take.wav").string();

    std::vector<float> sine(4800);
    for (std::size_t i = 0; i < sine.size(); ++i) {
        sine[i] = 0.9f * std::sin(2.0f * 3.14159265f * 220.0f * i / 48000.0f);
    }
    const auto written = WavWriter::writePcm24(path, sine, 48000);
    CHECK(written == 4800);

    std::vector<float> back;
    std::uint32_t sr = 0;
    REQUIRE(WavWriter::readMono(path, back, sr));
    CHECK(sr == 48000);
    REQUIRE(back.size() == 4800);
    // 24-bit квантование: погрешность <= ~1 LSB = 1/8388608 (абсолютный допуск).
    for (std::size_t i = 0; i < back.size(); i += 97) {
        CHECK(back[i] == Approx(sine[i]).margin(2e-7));
    }
    fs::remove_all(dir);
}

// --- Пики волноформы ----------------------------------------------------------------

TEST_CASE("ClipStore: блочные пики корректны", "[audio]") {
    ClipStore store;
    Clip c;
    c.samples.resize(1024);
    c.samples[10] = 0.5f;
    c.samples[300] = -0.75f;
    c.samples[1000] = 0.25f;
    const std::size_t idx = store.addTake(std::move(c));
    const auto& clip = store.takes()[idx];

    REQUIRE(clip.peaks.mins.size() == 4); // 1024/256
    CHECK(clip.peaks.maxs[0] == 0.5f);
    CHECK(clip.peaks.mins[1] == -0.75f);
    CHECK(clip.peaks.maxs[3] == 0.25f);
}

TEST_CASE("ClipStore: пики диапазона на грубом и тонком zoom", "[audio]") {
    ClipStore store;
    Clip c;
    c.samples.resize(4096);
    for (std::size_t i = 0; i < c.samples.size(); ++i) {
        c.samples[i] = (i % 300 < 150) ? 0.3f : -0.3f; // меандр, период не кратен блоку пиков
    }
    const std::size_t idx = store.addTake(std::move(c));
    const auto& clip = store.takes()[idx];

    std::vector<float> mins, maxs;
    // Грубый zoom: 4096 сэмплов в 32 колонки (128 сэмпл/колонка < 256 блока -> тонкий путь... возьмём 16 колонок).
    ClipStore::peaksForRange(clip, 0, 4096, 16, mins, maxs);
    CHECK(mins.size() == 16);
    for (std::size_t i = 0; i < 16; ++i) {
        CHECK(mins[i] == Approx(-0.3f));
        CHECK(maxs[i] == Approx(0.3f));
    }

    // Тонкий zoom: половина сэмплов в 2048 колонок (1 сэмпл/колонку).
    ClipStore::peaksForRange(clip, 0, 2048, 2048, mins, maxs);
    CHECK(mins.size() == 2048);
    // Первый сэмпл меандра положительный: min = max = сэмплу.
    CHECK(maxs[0] == Approx(0.3f));
    CHECK(mins[0] == Approx(0.3f));
    CHECK(mins[0] == Approx(maxs[0]));
}

TEST_CASE("ClipStore: за концом клипа колонки нулевые (нет растягивания)", "[audio]") {
    ClipStore store;
    Clip c;
    c.samples.assign(1024, 0.5f); // короткий клип на длинном окне
    const std::size_t idx = store.addTake(std::move(c));
    const auto& clip = store.takes()[idx];

    std::vector<float> mins, maxs;
    // Окно в 4 раза длиннее клипа, грубый zoom (1024 сэмпла/колонку).
    ClipStore::peaksForRange(clip, 0, 16384, 16, mins, maxs);
    REQUIRE(mins.size() == 16);
    CHECK(maxs[0] == Approx(0.5f)); // первая колонка — данные
    for (int c = 1; c < 16; ++c) {
        CHECK(mins[static_cast<std::size_t>(c)] == 0.0f);
        CHECK(maxs[static_cast<std::size_t>(c)] == 0.0f); // дальше — тишина
    }

    // Грубый zoom с окном 8192: 1024 сэмпла/колонку -> клип = ровно 1 колонка.
    ClipStore::peaksForRange(clip, 0, 8192, 8, mins, maxs);
    REQUIRE(mins.size() == 8);
    CHECK(maxs[0] == Approx(0.5f));
    for (int c = 1; c < 8; ++c) {
        CHECK(maxs[static_cast<std::size_t>(c)] == 0.0f);
    }
}

TEST_CASE("ClipStore: автоцвет различается", "[audio]") {
    CHECK(ClipStore::autoColor(0) != ClipStore::autoColor(1));
    CHECK(ClipStore::autoColor(0) == ClipStore::autoColor(10)); // цикл из 10
}

// --- Скомпилированные API --------------------------------------------------------------

TEST_CASE("RtAudio: ASIO и WASAPI вкомпилированы", "[audio]") {
    const auto apis = AudioEngine::compiledApis();
    const bool hasAsio = std::find(apis.begin(), apis.end(), RtAudio::WINDOWS_ASIO) != apis.end();
    const bool hasWasapi =
        std::find(apis.begin(), apis.end(), RtAudio::WINDOWS_WASAPI) != apis.end();
    CHECK(hasAsio);
    CHECK(hasWasapi);
}
