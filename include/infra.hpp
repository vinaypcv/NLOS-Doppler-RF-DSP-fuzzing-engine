#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <pthread.h>

namespace CoreSystem {
constexpr size_t CACHE_LINE_SIZE = 64;
constexpr size_t BUS_CAPACITY = 2048;

inline void EnforceThreadCoreAffinity(int core_id) noexcept {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_t current_thread = pthread_self();
    (void)pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

template <typename T, size_t Capacity>
class LockFreeBus {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2.");
public:
    LockFreeBus() noexcept : head(0), tail(0) {}

    bool Push(const T& item) noexcept {
        const size_t current_tail = tail.load(std::memory_order_relaxed);
        const size_t current_head = head.load(std::memory_order_acquire);
        
        if ((current_tail - current_head) >= Capacity) {
            return false;
        }
        buffer[current_tail & (Capacity - 1)] = item;
        tail.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    bool Pop(T& item) noexcept {
        const size_t current_head = head.load(std::memory_order_relaxed);
        const size_t current_tail = tail.load(std::memory_order_acquire);
        
        if (current_head == current_tail) {
            return false;
        }
        item = buffer[current_head & (Capacity - 1)];
        head.store(current_head + 1, std::memory_order_release);
        return true;
    }
private:
    std::array<T, Capacity> buffer;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail;
};
} // namespace CoreSystem
