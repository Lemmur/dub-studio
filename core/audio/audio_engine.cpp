// AudioEngine (Фаза 1, PLAN.md 6.1): RtAudio duplex 1-in/2-out, запись моно float32
// через SPSC ring, метроном, мониторинг, плейбек из append-only арены.
// ВАЖНО: rtCallback не содержит malloc/I/O/SQL/mutex — только атомики и memcpy-подобные циклы.
#include "dubstudio/audio_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace dubstudio {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kMinBpm = 30;
constexpr int kMaxBpm = 300;

std::string apiLabel(RtAudio::Api api) {
    switch (api) {
    case RtAudio::WINDOWS_ASIO: return "ASIO";
    case RtAudio::WINDOWS_WASAPI: return "WASAPI";
    case RtAudio::WINDOWS_DS: return "DirectSound";
    default: return "Unknown";
    }
}

// Затухающая синусоида-клик: экспоненциальный спад ~60 мс.
std::vector<float> makeClick(double freq, double sampleRate, double amplitude) {
    const std::uint64_t len = static_cast<std::uint64_t>(sampleRate * 0.08); // 80 мс
    std::vector<float> v(static_cast<std::size_t>(len));
    for (std::uint64_t i = 0; i < len; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double env = std::exp(-t * 55.0);
        v[static_cast<std::size_t>(i)] =
            static_cast<float>(amplitude * env * std::sin(2.0 * kPi * freq * t));
    }
    return v;
}

double toDb(double linear) {
    if (linear <= 1e-9) return -99.0;
    return 20.0 * std::log10(linear);
}

} // namespace

AudioEngine::AudioEngine() {
    rebuildClicks();
}

AudioEngine::~AudioEngine() { close(); }

std::vector<RtAudio::Api> AudioEngine::compiledApis() {
    std::vector<RtAudio::Api> apis;
    RtAudio::getCompiledApi(apis);
    return apis;
}

std::vector<AudioDevice> AudioEngine::listDevices() const {
    std::vector<AudioDevice> result;
    const auto apis = compiledApis();
    for (RtAudio::Api api : apis) {
        if (api != RtAudio::WINDOWS_ASIO && api != RtAudio::WINDOWS_WASAPI) continue;
        RtAudio rt(api);
        const auto ids = rt.getDeviceIds();
        const unsigned int defIn = rt.getDefaultInputDevice();
        for (unsigned int id : ids) {
            RtAudio::DeviceInfo info = rt.getDeviceInfo(id);
            if (info.ID != id) continue; // устройство пропало между вызовами
            AudioDevice d;
            d.api = api;
            d.apiName = apiLabel(api);
            d.deviceId = id;
            d.name = info.name;
            d.inputChannels = info.inputChannels;
            d.outputChannels = info.outputChannels;
            d.preferredSampleRate = info.preferredSampleRate ? info.preferredSampleRate : 48000;
            d.isDefaultInput = (id == defIn);
            result.push_back(std::move(d));
        }
    }
    return result;
}

void AudioEngine::open(const AudioDevice& device, unsigned int sampleRate,
                       unsigned int bufferFrames) {
    close(); // клики/арена перестраиваются только при остановленном потоке
    sampleRate_ = sampleRate;
    bufferFrames_ = bufferFrames;

    rt_ = std::make_unique<RtAudio>(device.api);

    RtAudio::StreamParameters inParams;
    inParams.deviceId = device.deviceId;
    inParams.nChannels = 1; // PLAN.md 6.1: вход 1 моно
    inParams.firstChannel = 0;

    RtAudio::StreamParameters outParams;
    outParams.deviceId = device.deviceId;
    outParams.nChannels = 2; // стерео-выход (метроном/мониторинг)
    outParams.firstChannel = 0;

    RtAudio::StreamOptions options;
    options.flags = RTAUDIO_MINIMIZE_LATENCY | RTAUDIO_SCHEDULE_REALTIME;
    options.numberOfBuffers = 2;

    unsigned int frames = bufferFrames;
    const RtAudioErrorType err =
        rt_->openStream(&outParams, &inParams, RTAUDIO_FLOAT32, sampleRate, &frames,
                        &AudioEngine::rtCallback, this, &options);
    if (err != RTAUDIO_NO_ERROR) {
        rt_.reset();
        throw std::runtime_error("Не удалось открыть поток " + device.apiName + " \"" +
                                 device.name + "\": код " + std::to_string(static_cast<int>(err)));
    }
    bufferFrames_ = frames;

    rebuildClicks();
    metroPos_.store(0, std::memory_order_relaxed);

    const RtAudioErrorType startErr = rt_->startStream();
    if (startErr != RTAUDIO_NO_ERROR) {
        rt_->closeStream();
        rt_.reset();
        throw std::runtime_error("Не удалось запустить поток " + device.apiName + ": код " +
                                 std::to_string(static_cast<int>(startErr)));
    }
}

void AudioEngine::close() {
    if (rt_) {
        if (rt_->isStreamRunning()) rt_->stopStream();
        if (rt_->isStreamOpen()) rt_->closeStream();
        rt_.reset();
    }
    recording_.store(false, std::memory_order_release);
    playActive_.store(0, std::memory_order_release);
}

void AudioEngine::setDirectMonitoring(bool hardware) {
    hwMonitor_.store(hardware, std::memory_order_relaxed);
}

void AudioEngine::setTempo(int bpm, int beatsPerBar) {
    bpm_.store(std::clamp(bpm, kMinBpm, kMaxBpm), std::memory_order_relaxed);
    beatsPerBar_.store(std::clamp(beatsPerBar, 1, 12), std::memory_order_relaxed);
    // Фаза метронома заново: период доли изменился.
    metroPos_.store(0, std::memory_order_relaxed);
}

void AudioEngine::rebuildClicks() {
    click_ = makeClick(1500.0, sampleRate_, 0.5);
    clickAccent_ = makeClick(2200.0, sampleRate_, 0.7);
}

void AudioEngine::mixClick(float* out, unsigned int nFrames) {
    // Период доли = sampleRate * 60 / bpm; акцент — первая доля такта.
    const std::uint64_t beatLen = static_cast<std::uint64_t>(sampleRate_) * 60ULL /
                                  static_cast<std::uint64_t>(bpm_.load(std::memory_order_relaxed));
    if (beatLen == 0) return;
    const std::uint64_t bpb = static_cast<std::uint64_t>(
        beatsPerBar_.load(std::memory_order_relaxed));
    std::uint64_t pos = metroPos_.load(std::memory_order_relaxed);
    const auto& normal = click_;
    const auto& accent = clickAccent_;
    for (unsigned int i = 0; i < nFrames; ++i) {
        const std::uint64_t beatIdx = pos / beatLen;
        const std::uint64_t inBeat = pos % beatLen;
        const bool isAccent = (beatIdx % bpb) == 0;
        const auto& wave = isAccent ? accent : normal;
        if (inBeat < wave.size()) {
            const float s = wave[static_cast<std::size_t>(inBeat)];
            out[2 * i] += s;
            out[2 * i + 1] += s;
        }
        ++pos;
    }
    metroPos_.store(pos, std::memory_order_relaxed);
}

void AudioEngine::awaitIdleBlock() {
    // Ждём завершения аудио-блока, начавшегося ДО этого вызова: такой блок мог
    // ещё читать арену/кольцо по старым атомикам. После инкремента blockSeq_
    // все последующие блоки видят последние сторы (release/acquire).
    if (!rt_ || !rt_->isStreamOpen()) return;
    const std::uint64_t seq0 = blockSeq_.load(std::memory_order_acquire);
    for (int i = 0; i < 500; ++i) { // ~50 мс максимум, блок ~5 мс
        if (blockSeq_.load(std::memory_order_acquire) != seq0) return;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

void AudioEngine::play(const float* samples, std::uint64_t frames) {
    if (frames == 0) return;
    stopPlayback();
    awaitIdleBlock();
    // Арена очищается только здесь (UI-поток, callback её больше не читает),
    // capacity сохраняется — после первого тейка realloc не будет.
    arena_.clear();
    playOffset_.store(0, std::memory_order_relaxed);
    arena_.insert(arena_.end(), samples, samples + frames);
    playhead_.store(0, std::memory_order_relaxed);
    playLen_.store(frames, std::memory_order_release);
    playActive_.store(1, std::memory_order_release);
}

void AudioEngine::stopPlayback() {
    playActive_.store(0, std::memory_order_release);
    playLen_.store(0, std::memory_order_release);
    playhead_.store(0, std::memory_order_relaxed);
}

std::uint64_t AudioEngine::drainRecorded(std::vector<float>& out) {
    if (!recording_.load(std::memory_order_acquire)) return 0;
    const std::uint64_t avail = recRing_.framesAvailable();
    if (avail == 0) return 0;
    const std::size_t oldSize = out.size();
    out.resize(oldSize + static_cast<std::size_t>(avail));
    const std::uint64_t n = recRing_.pop(out.data() + oldSize, avail);
    if (n < avail) out.resize(oldSize + static_cast<std::size_t>(n));
    return n;
}

void AudioEngine::startRecord() {
    if (recording_.exchange(false, std::memory_order_acq_rel)) {
        // перезапись на лету: сначала дождаться блока по старому флагу
        awaitIdleBlock();
    }
    stopPlayback();
    awaitIdleBlock(); // блок с прошлым playActive мог читать арену
    recRing_.reset();
    recFrames_.store(0, std::memory_order_relaxed);
    metroPos_.store(0, std::memory_order_relaxed);
    recording_.store(true, std::memory_order_release);
}

TakeResult AudioEngine::stopRecord() {
    TakeResult r;
    r.sampleRate = sampleRate_;
    recording_.store(false, std::memory_order_release);
    awaitIdleBlock(); // долить хвост от блока, начавшегося до стопа
    const std::uint64_t avail = recRing_.framesAvailable();
    if (avail > 0) {
        r.samples.resize(static_cast<std::size_t>(avail));
        recRing_.pop(r.samples.data(), avail);
    }
    r.frames = r.samples.size();
    r.rmsDb = rmsDb(r.samples);
    r.peakDb = peakDb(r.samples);
    r.xruns = xruns_.load(std::memory_order_relaxed);
    return r;
}

void AudioEngine::processBlock(float* out, const float* in, unsigned int nFrames) {
    // 1) Выход в ноль (микс аддитивный поверх).
    std::memset(out, 0, sizeof(float) * 2 * static_cast<std::size_t>(nFrames));

    if (in) {
        // 2) Вход: peak-детектор (метр).
        float peak = 0.0f;
        for (unsigned int i = 0; i < nFrames; ++i) {
            const float a = std::fabs(in[i]);
            if (a > peak) peak = a;
        }
        inPeak_.store(peak, std::memory_order_relaxed);

        // 3) Мониторинг: копия вход -> выход с гейном (если не Direct Monitoring).
        if (swMonitor_.load(std::memory_order_relaxed) &&
            !hwMonitor_.load(std::memory_order_relaxed)) {
            const float g = monitorGain_.load(std::memory_order_relaxed);
            for (unsigned int i = 0; i < nFrames; ++i) {
                const float s = in[i] * g;
                out[2 * i] += s;
                out[2 * i + 1] += s;
            }
        }

        // 4) Запись: push в SPSC-кольцо; не влезло -> xrun (никаких блокировок).
        if (recording_.load(std::memory_order_acquire)) {
            const std::uint64_t pushed = recRing_.push(in, nFrames);
            if (pushed < nFrames) {
                xruns_.fetch_add(1, std::memory_order_relaxed);
            } else {
                recFrames_.fetch_add(nFrames, std::memory_order_relaxed);
            }
        }
    }

    // 5) Метроном.
    if (metroOn_.load(std::memory_order_relaxed)) {
        mixClick(out, nFrames);
    }

    // 6) Плейбек из арены (данные не двигаются, callback читает по индексам).
    if (playActive_.load(std::memory_order_acquire) != 0) {
        const std::uint64_t len = playLen_.load(std::memory_order_acquire);
        const std::uint64_t off = playOffset_.load(std::memory_order_relaxed);
        std::uint64_t ph = playhead_.load(std::memory_order_relaxed);
        const std::uint64_t canPlay =
            (ph + nFrames <= len) ? nFrames : (len > ph ? len - ph : 0);
        for (std::uint64_t i = 0; i < canPlay; ++i) {
            const float s = arena_[static_cast<std::size_t>(off + ph + i)];
            out[2 * i] += s;
            out[2 * i + 1] += s;
        }
        ph += canPlay;
        playhead_.store(ph, std::memory_order_release);
        if (ph >= len) playActive_.store(0, std::memory_order_release);
    }

    // 7) Мягкий клиппер выхода (страховка ушей/ЦАП).
    for (unsigned int i = 0; i < 2 * nFrames; ++i) {
        float s = out[i];
        if (s > 1.0f) s = 1.0f;
        else if (s < -1.0f) s = -1.0f;
        out[i] = s;
    }

    // 8) Барьер для UI (awaitIdleBlock): конец блока.
    blockSeq_.fetch_add(1, std::memory_order_release);
}

int AudioEngine::rtCallback(void* out, void* in, unsigned int nFrames, double /*streamTime*/,
                            RtAudioStreamStatus status, void* userData) {
    auto* self = static_cast<AudioEngine*>(userData);
    if (status) self->xruns_.fetch_add(1, std::memory_order_relaxed);
    self->processBlock(static_cast<float*>(out), static_cast<const float*>(in), nFrames);
    return 0;
}

double rmsDb(const std::vector<float>& samples) {
    if (samples.empty()) return -99.0;
    double sum = 0.0;
    for (float s : samples) sum += static_cast<double>(s) * static_cast<double>(s);
    return toDb(std::sqrt(sum / static_cast<double>(samples.size())));
}

double peakDb(const std::vector<float>& samples) {
    if (samples.empty()) return -99.0;
    float peak = 0.0f;
    for (float s : samples) peak = std::max(peak, std::fabs(s));
    return toDb(peak);
}

} // namespace dubstudio
