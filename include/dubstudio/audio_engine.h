// AudioEngine — RtAudio + ASIO (приоритет) / WASAPI (fallback), PLAN.md раздел 6.1.
// Железное правило: в callback только ring-buffer и атомики (без malloc/I/O/SQL/mutex).
// Весь путь обработки вынесен в processBlock() — тестируется без звуковой карты.
#pragma once

#include "ring_buffer.h"

#include <RtAudio.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dubstudio {

struct AudioDevice {
    RtAudio::Api api = RtAudio::UNSPECIFIED;
    std::string apiName;      // "ASIO" / "WASAPI"
    unsigned int deviceId = 0; // id внутри API (getDeviceIds())
    std::string name;
    unsigned int inputChannels = 0;
    unsigned int outputChannels = 0;
    unsigned int preferredSampleRate = 48000;
    bool isDefaultInput = false;
};

// Результат финализации записи.
struct TakeResult {
    std::vector<float> samples;   // моно float32
    std::uint64_t sampleRate = 0;
    std::uint64_t frames = 0;
    double rmsDb = -99.0;
    double peakDb = -99.0;
    std::uint64_t xruns = 0;      // xrun-счётчик на момент стопа
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // --- Устройства ---------------------------------------------------------
    // Все устройства всех скомпилированных API, ASIO первыми (PLAN.md 6.1).
    // ВАЖНО: ASIO-драйвер однопользовательский — пока поток этого API открыт
    // у нас, повторная инициализация драйвера в новом экземпляре RtAudio
    // завершается ошибкой. Поэтому для открытого API возвращается
    // запомненное устройство (currentDevice_), остальные API пробятся честно.
    std::vector<AudioDevice> listDevices() const;
    // Дуплекс 1-in / 2-out на выбранном устройстве, частота проекта, буфер.
    // При ошибке — std::runtime_error с текстом.
    void open(const AudioDevice& device, unsigned int sampleRate, unsigned int bufferFrames);
    void close();
    bool isOpen() const { return rt_ && rt_->isStreamOpen(); }
    unsigned int sampleRate() const { return sampleRate_; }
    unsigned int bufferFrames() const { return bufferFrames_; }
    const AudioDevice& currentDevice() const { return currentDevice_; }

    // --- Метроном -----------------------------------------------------------
    void setMetronomeEnabled(bool on) { metroOn_.store(on, std::memory_order_relaxed); }
    bool metronomeEnabled() const { return metroOn_.load(std::memory_order_relaxed); }
    // Клики предвычислены на sample rate открытого потока; bpm меняет только период.
    void setTempo(int bpm, int beatsPerBar);
    int bpm() const { return bpm_.load(std::memory_order_relaxed); }
    int beatsPerBar() const { return beatsPerBar_.load(std::memory_order_relaxed); }

    // --- Мониторинг (PLAN.md 6.1: Direct Monitoring OFF по умолчанию) -------
    // hardware=true -> сигнал мониторит само устройство, софт-копия выключена.
    void setDirectMonitoring(bool hardware);
    void setSoftwareMonitoring(bool on) { swMonitor_.store(on, std::memory_order_relaxed); }
    bool softwareMonitoring() const { return swMonitor_.load(std::memory_order_relaxed); }
    void setMonitoringGain(float linear) { monitorGain_.store(linear, std::memory_order_relaxed); }

    // --- Плейбек --------------------------------------------------------------
    // Арена append-only внутри одного клипа; перед новым клипом старая
    // очищается после барьера awaitIdleBlock() — callback её больше не читает.
    void play(const float* samples, std::uint64_t frames);
    void stopPlayback();
    bool isPlaying() const { return playActive_.load(std::memory_order_acquire) != 0; }
    std::uint64_t playheadFrames() const { return playhead_.load(std::memory_order_acquire); }

    // --- Запись моно ----------------------------------------------------------
    bool isRecording() const { return recording_.load(std::memory_order_acquire); }
    void startRecord();      // сброс кольца + recording=true
    TakeResult stopRecord(); // финальный дрейн, метрики

    // Дрейн кольца на UI-потоке (живая волноформа); возвращает число кадров.
    std::uint64_t drainRecorded(std::vector<float>& out);

    // --- Метрики (UI опрашивает) ----------------------------------------------
    std::uint64_t xrunCount() const { return xruns_.load(std::memory_order_relaxed); }
    float inputPeak() const { return inPeak_.load(std::memory_order_relaxed); }
    std::uint64_t recordedFrames() const { return recFrames_.load(std::memory_order_acquire); }

    // --- Тесты: путь обработки без звуковой карты -----------------------------
    // out стерео (2*nFrames), in моно (nFrames). Тот же код, что в rtCallback.
    void processBlock(float* out, const float* in, unsigned int nFrames);

    static std::vector<RtAudio::Api> compiledApis();

private:
    static int rtCallback(void* out, void* in, unsigned int nFrames, double streamTime,
                          RtAudioStreamStatus status, void* userData);

    void rebuildClicks(); // только при остановленном потоке (open/ctor)
    void mixClick(float* out, unsigned int nFrames);
    // Барьер: дождаться завершения аудио-блока, начавшегося до текущего момента
    // (после него callback гарантированно увидел последние атомарные сторы).
    void awaitIdleBlock();

    std::unique_ptr<RtAudio> rt_; // RtAudio некопируем, владение через unique_ptr
    AudioDevice currentDevice_;   // устройство открытого потока (для listDevices)
    unsigned int sampleRate_ = 48000;
    unsigned int bufferFrames_ = 256;

    // --- Состояние, доступное из callback (только атомики/предвычисленное) ---
    RingBuffer recRing_{48000 * 8, 1}; // 8 сек запаса, аллоцируется в ctor
    std::atomic<bool> recording_{false};
    std::atomic<std::uint64_t> recFrames_{0};
    std::atomic<std::uint64_t> xruns_{0};
    std::atomic<float> inPeak_{0.0f};
    std::atomic<std::uint64_t> blockSeq_{0}; // инкремент в конце каждого блока

    std::atomic<bool> metroOn_{false};
    std::atomic<int> bpm_{100};
    std::atomic<int> beatsPerBar_{4};
    std::vector<float> click_;         // клик (перестраивается только на UI без потока)
    std::vector<float> clickAccent_;   // акцент первой доли
    std::atomic<std::uint64_t> metroPos_{0}; // сэмпл от старта метронома

    std::atomic<bool> swMonitor_{false};
    std::atomic<bool> hwMonitor_{false}; // Direct Monitoring: софт-копию не делать
    std::atomic<float> monitorGain_{0.8f};

    // Плейбек: арена не двигает старые данные; очистка только за барьером.
    std::vector<float> arena_;
    std::atomic<std::uint64_t> playOffset_{0}; // оффсет активного клипа в арене
    std::atomic<std::uint64_t> playLen_{0};    // кадры активного клипа
    std::atomic<std::uint64_t> playhead_{0};   // позиция внутри клипа
    std::atomic<int> playActive_{0};
};

// Утилиты метрик тейка (PLAN.md 6.3 — сырые RMS/Peak).
double rmsDb(const std::vector<float>& samples);
double peakDb(const std::vector<float>& samples);

} // namespace dubstudio
