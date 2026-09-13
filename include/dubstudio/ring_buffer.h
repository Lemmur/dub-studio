// SPSC lock-free ring buffer (один писатель = аудио-callback, один читатель = UI).
// AGENTS.md правило 3: в ASIO callback только ring-buffer, без malloc/mutex.
// Ёмкость — степень двойки, индексы монотонные uint64 (wraparound не страшен).
#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dubstudio {

class RingBuffer {
public:
    // capacityFrames округляется вверх к степени двойки; samplesPerFrame = 1 (моно).
    explicit RingBuffer(std::uint64_t capacityFrames, int samplesPerFrame = 1)
        : samplesPerFrame_(samplesPerFrame) {
        std::uint64_t cap = 8;
        while (cap < capacityFrames) cap <<= 1;
        mask_ = cap - 1;
        data_.assign(static_cast<std::size_t>(cap) * samplesPerFrame, 0.0f);
    }

    // Callback (писатель). Возвращает число реально записанных кадров;
    // при нехватке места пишет сколько влезает и НЕ блокируется.
    std::uint64_t push(const float* src, std::uint64_t frames) noexcept {
        const std::uint64_t w = writeIndex_.load(std::memory_order_relaxed);
        const std::uint64_t r = readIndex_.load(std::memory_order_acquire);
        const std::uint64_t used = w - r;
        const std::uint64_t cap = mask_ + 1;
        if (used >= cap) return 0;
        const std::uint64_t freeFrames = cap - used;
        const std::uint64_t n = frames < freeFrames ? frames : freeFrames;
        const std::uint64_t start = (w & mask_) * static_cast<std::uint64_t>(samplesPerFrame_);
        const std::uint64_t contiguous = (n < cap - (w & mask_)) ? n : cap - (w & mask_);
        const std::uint64_t sBytes = static_cast<std::uint64_t>(samplesPerFrame_);
        std::copy(src, src + contiguous * sBytes, data_.begin() + static_cast<std::ptrdiff_t>(start));
        if (n > contiguous) {
            std::copy(src + contiguous * sBytes, src + n * sBytes, data_.begin());
        }
        writeIndex_.store(w + n, std::memory_order_release);
        return n;
    }

    // UI (читатель). Возвращает число прочитанных кадров.
    std::uint64_t pop(float* dst, std::uint64_t frames) noexcept {
        const std::uint64_t r = readIndex_.load(std::memory_order_relaxed);
        const std::uint64_t w = writeIndex_.load(std::memory_order_acquire);
        const std::uint64_t available = w - r;
        const std::uint64_t n = frames < available ? frames : available;
        if (n == 0) return 0;
        const std::uint64_t cap = mask_ + 1;
        const std::uint64_t start = (r & mask_) * static_cast<std::uint64_t>(samplesPerFrame_);
        const std::uint64_t contiguous = (n < cap - (r & mask_)) ? n : cap - (r & mask_);
        const std::uint64_t sBytes = static_cast<std::uint64_t>(samplesPerFrame_);
        std::copy(data_.begin() + static_cast<std::ptrdiff_t>(start),
                  data_.begin() + static_cast<std::ptrdiff_t>(start + contiguous * sBytes), dst);
        if (n > contiguous) {
            std::copy(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>((n - contiguous) * sBytes),
                      dst + contiguous * sBytes);
        }
        readIndex_.store(r + n, std::memory_order_release);
        return n;
    }

    // Сброс (только когда писатель неактивен: recording=false на UI-потоке).
    void reset() noexcept {
        writeIndex_.store(0, std::memory_order_relaxed);
        readIndex_.store(0, std::memory_order_relaxed);
    }

    std::uint64_t framesAvailable() const noexcept {
        return writeIndex_.load(std::memory_order_acquire) - readIndex_.load(std::memory_order_acquire);
    }
    std::uint64_t capacity() const noexcept { return mask_ + 1; }

private:
    std::vector<float> data_;
    std::uint64_t mask_ = 0;
    int samplesPerFrame_ = 1;
    std::atomic<std::uint64_t> writeIndex_{0}; // пишет только callback
    std::atomic<std::uint64_t> readIndex_{0};  // пишет только UI
};

} // namespace dubstudio
