// Фаза 2: реализация DSP-операций редактирования (PLAN.md 6.4).
#include "dubstudio/audio_ops.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace dubstudio {
namespace {

float peakAbs(const std::vector<float>& samples, std::size_t from, std::size_t to) {
    float peak = 0.0f;
    for (std::size_t i = from; i < to && i < samples.size(); ++i) {
        const float v = std::fabs(samples[i]);
        if (v > peak) peak = v;
    }
    return peak;
}

} // namespace

double normalizePeakToDb(std::vector<float>& samples, double targetDbFs) {
    const float peak = peakAbs(samples, 0, samples.size());
    if (peak <= 0.0f) return 0.0;
    const float target = static_cast<float>(std::pow(10.0, targetDbFs / 20.0));
    const float gain = target / peak;
    for (auto& s : samples) {
        s = std::clamp(s * gain, -1.0f, 1.0f);
    }
    return 20.0 * std::log10(static_cast<double>(gain));
}

void silenceRange(std::vector<float>& samples, std::size_t from, std::size_t to) {
    if (from >= samples.size()) return;
    to = std::min(to, samples.size());
    for (std::size_t i = from; i < to; ++i) samples[i] = 0.0f;
}

void reverseSamples(std::vector<float>& samples, std::size_t from, std::size_t to) {
    if (from >= samples.size()) return;
    to = std::min(to, samples.size());
    std::reverse(samples.begin() + static_cast<std::ptrdiff_t>(from),
                 samples.begin() + static_cast<std::ptrdiff_t>(to));
}

std::uint64_t snapToZeroCrossing(const std::vector<float>& samples, std::uint64_t pos) {
    if (samples.empty()) return pos;
    if (pos >= samples.size()) return samples.size();
    // Ищем влево и вправо ближайшую смену знака (или точный ноль).
    std::int64_t left = static_cast<std::int64_t>(pos);
    while (left > 0) {
        if (samples[static_cast<std::size_t>(left)] == 0.0f) return static_cast<std::uint64_t>(left);
        const float a = samples[static_cast<std::size_t>(left - 1)];
        const float b = samples[static_cast<std::size_t>(left)];
        if ((a < 0.0f && b >= 0.0f) || (a >= 0.0f && b < 0.0f))
            return static_cast<std::uint64_t>(left);
        --left;
    }
    std::size_t right = static_cast<std::size_t>(pos);
    while (right + 1 < samples.size()) {
        const float a = samples[right];
        const float b = samples[right + 1];
        if ((a < 0.0f && b >= 0.0f) || (a >= 0.0f && b < 0.0f)) return right + 1;
        ++right;
    }
    return pos; // пересечений нет (например, постоянный сигнал)
}

SoundRange findSoundRange(const std::vector<float>& samples, double thresholdDbFs,
                          std::uint64_t windowSamples) {
    SoundRange r;
    if (samples.empty()) return r;
    if (windowSamples == 0) windowSamples = 1;
    const float threshold =
        static_cast<float>(std::pow(10.0, thresholdDbFs / 20.0) / std::sqrt(2.0));

    const std::size_t n = samples.size();
    const std::size_t win = static_cast<std::size_t>(
        std::min<std::uint64_t>(windowSamples, n));
    const std::size_t blocks = (n + win - 1) / win;

    std::uint64_t first = n, last = 0;
    for (std::size_t b = 0; b < blocks; ++b) {
        const std::size_t from = b * win;
        const std::size_t to = std::min(n, from + win);
        double sum = 0.0;
        for (std::size_t i = from; i < to; ++i) sum += static_cast<double>(samples[i]) * samples[i];
        const double rms = std::sqrt(sum / static_cast<double>(to - from));
        if (rms > threshold) {
            if (from < first) first = from;
            last = to;
        }
    }
    if (first >= last) return r; // тишина целиком
    r.first = first;
    r.last = last;
    r.empty = false;
    return r;
}

void renderClipInto(const Clip& clip, std::vector<float>& out) {
    if (clip.samples.empty()) return;
    const std::uint64_t need = clip.startSample + clip.samples.size();
    if (out.size() < need) out.resize(static_cast<std::size_t>(need), 0.0f);

    const float gain = static_cast<float>(std::pow(10.0, clip.gainDb / 20.0));
    const std::size_t n = clip.samples.size();
    const float fi = clip.fadeInSamples > 0 ? static_cast<float>(clip.fadeInSamples) : 1.0f;
    const float fo = clip.fadeOutSamples > 0 ? static_cast<float>(clip.fadeOutSamples) : 1.0f;
    for (std::size_t i = 0; i < n; ++i) {
        float g = gain;
        if (clip.fadeInSamples > 0 && i < clip.fadeInSamples) g *= static_cast<float>(i + 1) / fi;
        if (clip.fadeOutSamples > 0 && i + clip.fadeOutSamples >= n)
            g *= static_cast<float>(n - i) / fo;
        out[static_cast<std::size_t>(clip.startSample) + i] += clip.samples[i] * g;
    }
}

std::vector<float> renderMix(const std::vector<Clip>& clips) {
    std::vector<float> out;
    for (const auto& c : clips) renderClipInto(c, out);
    return out;
}

MergedClip crossfadeMerge(const Clip& a, const Clip& b) {
    MergedClip m;
    if (a.samples.empty() && b.samples.empty()) return m;

    const std::uint64_t aStart = a.samples.empty() ? b.startSample : a.startSample;
    const std::uint64_t bStart = b.samples.empty() ? a.startSample : b.startSample;
    const std::uint64_t aEnd = aStart + a.samples.size();
    const std::uint64_t bEnd = bStart + b.samples.size();
    const std::uint64_t start = std::min(aStart, bStart);
    const std::uint64_t end = std::max(aEnd, bEnd);

    // Зона перекрытия (для кроссфейда нужно перекрытие по времени).
    const std::uint64_t o0 = std::max(aStart, bStart);
    const std::uint64_t o1 = std::min(aEnd, bEnd);
    m.overlapSamples = o1 > o0 ? o1 - o0 : 0;
    m.startSample = start;

    m.samples.assign(static_cast<std::size_t>(end - start), 0.0f);
    const float ga = static_cast<float>(std::pow(10.0, a.gainDb / 20.0));
    const float gb = static_cast<float>(std::pow(10.0, b.gainDb / 20.0));

    for (std::uint64_t t = start; t < end; ++t) {
        float va = 0.0f;
        bool hasA = false, hasB = false;
        if (t >= aStart && t < aEnd) {
            va = a.samples[static_cast<std::size_t>(t - aStart)] * ga;
            hasA = true;
        }
        float vb = 0.0f;
        if (t >= bStart && t < bEnd) {
            vb = b.samples[static_cast<std::size_t>(t - bStart)] * gb;
            hasB = true;
        }
        float v;
        if (hasA && hasB && m.overlapSamples > 0) {
            // Equal-power: сумма мощностей постоянна на всём перекрытии.
            const double p = static_cast<double>(t - o0) / static_cast<double>(m.overlapSamples);
            const double ca = std::cos(p * 3.14159265358979323846 / 2.0);
            const double cb = std::sin(p * 3.14159265358979323846 / 2.0);
            v = static_cast<float>(va * ca + vb * cb);
        } else if (hasA) {
            v = va;
        } else {
            v = vb;
        }
        m.samples[static_cast<std::size_t>(t - start)] = v;
    }
    return m;
}

std::vector<float> timeStretchWsola(const std::vector<float>& src, double ratio,
                                    std::uint32_t sampleRate) {
    if (src.empty()) return {};
    ratio = std::clamp(ratio, 0.25, 4.0);
    if (std::fabs(ratio - 1.0) < 1e-9) return src;

    const std::size_t target = static_cast<std::size_t>(
        std::llround(static_cast<double>(src.size()) * ratio));
    if (target == 0) return {};

    // Параметры: окно ~43 мс (но не больше 1/2 короткого клипа), overlap 50%.
    std::size_t win = static_cast<std::size_t>(std::max<std::uint64_t>(256, sampleRate / 23));
    win = std::min(win, std::max<std::size_t>(64, src.size() / 4));
    win = std::min(win, src.size());
    if (win < 16) return src; // слишком короткий материал — без растяжения
    const std::size_t Hs = win / 2;            // синтез-хоп (50% overlap)
    const std::int64_t search = static_cast<std::int64_t>(win / 4);
    const std::int64_t Ha = std::max<std::int64_t>(
        1, static_cast<std::int64_t>(std::llround(static_cast<double>(Hs) / ratio)));

    // Окно Ханна.
    std::vector<float> w(win);
    for (std::size_t k = 0; k < win; ++k) {
        w[k] = static_cast<float>(0.5 * (1.0 - std::cos(2.0 * 3.14159265358979323846 * k /
                                                        static_cast<double>(win - 1))));
    }

    std::vector<float> out(target, 0.0f);
    std::vector<float> wsum(target, 0.0f);

    // Нормированная кросс-корреляция сегмента src с уже собранным хвостом out.
    auto bestOffset = [&](std::size_t analysis, std::size_t synth) -> std::size_t {
        std::int64_t bestD = 0;
        double bestScore = -2.0;
        const std::size_t overlap = win - Hs; // зона соприкосновения с прошлым фреймом
        for (std::int64_t d = -search; d <= search; ++d) {
            const std::int64_t apos = static_cast<std::int64_t>(analysis) + d;
            if (apos < 0 || apos + static_cast<std::int64_t>(win) >
                                static_cast<std::int64_t>(src.size()))
                continue;
            double dot = 0.0, ea = 0.0, eb = 0.0;
            for (std::size_t k = 0; k < overlap; ++k) {
                const double x = out[synth + k];
                const double y = src[static_cast<std::size_t>(apos) + k];
                dot += x * y;
                ea += x * x;
                eb += y * y;
            }
            const double denom = std::sqrt(ea * eb);
            const double score = denom > 1e-12 ? dot / denom : 0.0;
            if (score > bestScore) {
                bestScore = score;
                bestD = d;
            }
        }
        return static_cast<std::size_t>(static_cast<std::int64_t>(analysis) + bestD);
    };

    // Первый фрейм — от начала источника.
    std::size_t analysis = 0;
    std::size_t synth = 0;
    for (std::size_t k = 0; k < win; ++k) {
        out[k] += src[k] * w[k];
        wsum[k] += w[k];
    }
    while (true) {
        synth += Hs;
        if (synth + win > target) break;
        analysis = static_cast<std::size_t>(
            std::min<std::int64_t>(static_cast<std::int64_t>(analysis) + Ha,
                                   static_cast<std::int64_t>(src.size()) -
                                       static_cast<std::int64_t>(win)));
        // На исчерпании источника поиск бессмысленен (d зажат в 0).
        const std::size_t apos =
            analysis + static_cast<std::size_t>(search) * 2 + win < src.size()
                ? bestOffset(analysis, synth)
                : analysis;
        for (std::size_t k = 0; k < win; ++k) {
            out[synth + k] += src[apos + k] * w[k];
            wsum[synth + k] += w[k];
        }
        if (apos + win >= src.size()) {
            // Источник исчерпан: домешиваем последний фрейм повторно без поиска.
            synth += Hs;
            if (synth + win > target) break;
            for (std::size_t k = 0; k < win; ++k) {
                out[synth + k] += src[apos + k] * w[k];
                wsum[synth + k] += w[k];
            }
        }
    }

    // Нормировка суммы окон.
    for (std::size_t i = 0; i < target; ++i) {
        if (wsum[i] > 1e-4f) out[i] /= wsum[i];
    }
    return out;
}

} // namespace dubstudio
