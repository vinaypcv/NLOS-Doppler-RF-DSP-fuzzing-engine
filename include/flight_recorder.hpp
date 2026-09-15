#pragma once
#include <cstdint>
#include <atomic>
#include <array>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

namespace CoreSystem {

struct alignas(32) TelemetryEvent {
    uint64_t timestamp_ns;
    uint32_t thread_id;
    uint32_t event_code;
    uint64_t payload;
};

template <size_t CapacityPerThread = 8192, size_t MaxThreads = 16>
class StripedMmapFlightRecorder {
    static_assert((CapacityPerThread & (CapacityPerThread - 1)) == 0, "Capacity must be a power of 2.");

    struct alignas(64) ThreadChannel {
        alignas(64) std::atomic<size_t> write_index{0};
        TelemetryEvent events[CapacityPerThread];
    };

public:
    StripedMmapFlightRecorder(const char* filename = "flight_log.bin") noexcept {
        size_t total_bytes = sizeof(ThreadChannel) * MaxThreads;
        fd = ::open(filename, O_RDWR | O_CREAT | O_TRUNC, 0666);
        if (fd != -1) {
            if (::ftruncate(fd, total_bytes) == 0) {
                channels = static_cast<ThreadChannel*>(
                    ::mmap(nullptr, total_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)
                );
                if (channels != MAP_FAILED) {
                    ::madvise(channels, total_bytes, MADV_WILLNEED);
                }
            } else {
                ::close(fd);
                fd = -1;
            }
        }
    }

    ~StripedMmapFlightRecorder() noexcept {
        if (channels && channels != MAP_FAILED) {
            ::msync(channels, sizeof(ThreadChannel) * MaxThreads, MS_SYNC);
            ::munmap(channels, sizeof(ThreadChannel) * MaxThreads);
        }
        if (fd != -1) {
            ::close(fd);
        }
    }

    inline void Record(uint32_t thread_id, uint32_t code, uint64_t payload) noexcept {
        if (!channels || channels == MAP_FAILED) return;

        size_t slot = thread_id % MaxThreads;
        ThreadChannel& ch = channels[slot];

        struct timespec ts;
        ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        uint64_t now_ns = (static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL) + ts.tv_nsec;

        size_t idx = ch.write_index.fetch_add(1, std::memory_order_relaxed) & (CapacityPerThread - 1);
        ch.events[idx] = TelemetryEvent{
            .timestamp_ns = now_ns,
            .thread_id = thread_id,
            .event_code = code,
            .payload = payload
        };
    }

    size_t GetTotalLoggedEvents() const noexcept {
        if (!channels || channels == MAP_FAILED) return 0;
        size_t total = 0;
        for (size_t i = 0; i < MaxThreads; ++i) {
            total += channels[i].write_index.load(std::memory_order_relaxed);
        }
        return total;
    }

private:
    ThreadChannel* channels{nullptr};
    int fd{-1};
};

} // namespace CoreSystem
