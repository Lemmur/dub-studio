// ClipStore: пики волноформы и автоцвет тейков (Фаза 1).
#include "dubstudio/clip_store.h"

#include <algorithm>
#include <cmath>

namespace dubstudio {
namespace {
constexpr std::uint64_t kPeakBlock = 256;
}

void ClipStore::rebuildPeaks(Clip& clip) {
    clip.peaks.blockSize = kPeakBlock;
    const std::size_t blocks = (clip.samples.size() + kPeakBlock - 1) / kPeakBlock;
    clip.peaks.mins.assign(blocks, 0.0f);
    clip.peaks.maxs.assign(blocks, 0.0f);
    for (std::size_t b = 0; b < blocks; ++b) {
        const std::size_t from = b * static_cast<std::size_t>(kPeakBlock);
        float lo = clip.samples[from], hi = clip.samples[from]; // от первого сэмпла
        const std::size_t to = std::min(clip.samples.size(), from + static_cast<std::size_t>(kPeakBlock));
        for (std::size_t i = from; i < to; ++i) {
            const float s = clip.samples[i];
            if (s < lo) lo = s;
            if (s > hi) hi = s;
        }
        clip.peaks.mins[b] = lo;
        clip.peaks.maxs[b] = hi;
    }
}

std::size_t ClipStore::addTake(Clip clip) {
    rebuildPeaks(clip);
    takes_.push_back(std::move(clip));
    return takes_.size() - 1;
}

void ClipStore::peaksForRange(const Clip& clip, std::uint64_t fromSample, std::uint64_t toSample,
                              int columns, std::vector<float>& mins, std::vector<float>& maxs) {
    mins.assign(static_cast<std::size_t>(columns), 0.0f);
    maxs.assign(static_cast<std::size_t>(columns), 0.0f);
    if (columns <= 0 || toSample <= fromSample) return;
    const std::uint64_t total = clip.samples.size();
    if (fromSample >= total) return; // окно целиком правее конца клипа
    // toSample НЕ зажимаем к длине клипа: колонки правее конца остаются
    // нулями, волна занимает ровно свою длительность, а не растягивается
    // на всю ширину окна (фикс «волны на всю дорожку» при живой записи).

    // Грубый zoom (>= blockSize сэмплов на колонку): по блочным пикам.
    // Колонки за концом клипа остаются нулями — волна не «растягивается»
    // на всё окно, а занимает ровно свою длительность.
    const double perColumn = static_cast<double>(toSample - fromSample) / columns;
    if (perColumn >= static_cast<double>(kPeakBlock)) {
        const std::uint64_t nBlocks = clip.peaks.mins.size();
        for (int c = 0; c < columns; ++c) {
            const std::uint64_t b0 = (fromSample + static_cast<std::uint64_t>(c * perColumn)) / kPeakBlock;
            if (b0 >= nBlocks) continue; // дальше конца клипа — тишина
            const std::uint64_t b1 =
                std::min<std::uint64_t>(nBlocks,
                    std::max<std::uint64_t>(b0 + 1,
                        (fromSample + static_cast<std::uint64_t>((c + 1) * perColumn) + kPeakBlock - 1) / kPeakBlock));
            float lo = clip.peaks.mins[static_cast<std::size_t>(b0)];
            float hi = clip.peaks.maxs[static_cast<std::size_t>(b0)];
            for (std::uint64_t b = b0 + 1; b < b1; ++b) {
                lo = std::min(lo, clip.peaks.mins[static_cast<std::size_t>(b)]);
                hi = std::max(hi, clip.peaks.maxs[static_cast<std::size_t>(b)]);
            }
            mins[static_cast<std::size_t>(c)] = lo;
            maxs[static_cast<std::size_t>(c)] = hi;
        }
        return;
    }

    // Тонкий zoom: прямые сэмплы ( Audacity-style до отдельных сэмплов).
    for (int c = 0; c < columns; ++c) {
        const std::uint64_t s0 = fromSample + static_cast<std::uint64_t>(c * perColumn);
        std::uint64_t s1 = fromSample + static_cast<std::uint64_t>((c + 1) * perColumn);
        if (c == columns - 1) s1 = toSample; // последняя колонка до конца
        if (s0 >= total) continue;
        float lo = clip.samples[static_cast<std::size_t>(s0)];
        float hi = clip.samples[static_cast<std::size_t>(s0)];
        for (std::uint64_t s = s0 + 1; s < s1 && s < total; ++s) {
            const float v = clip.samples[static_cast<std::size_t>(s)];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        mins[static_cast<std::size_t>(c)] = lo;
        maxs[static_cast<std::size_t>(c)] = hi;
    }
}

std::uint32_t ClipStore::autoColor(int takeIndex) {
    // Палитра различимых оттенков на тёмном фоне (по циклу).
    static const std::uint32_t kPalette[] = {
        0x4f9dff, 0xff9d5c, 0x7ddf7d, 0xdf7dc8, 0xd0d05c,
        0x8c7ddf, 0x5cd0d0, 0xdf8c8c, 0x9dffdf, 0xc8b48c,
    };
    constexpr int kN = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));
    const int idx = ((takeIndex % kN) + kN) % kN;
    return kPalette[idx];
}

} // namespace dubstudio
