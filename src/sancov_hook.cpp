#include <atomic>
#include <cstdint>

alignas(64) std::atomic<uint8_t> g_sancov_map[65536];

extern "C" void __sanitizer_cov_trace_pc() {
    uintptr_t pc = (uintptr_t)__builtin_return_address(0);
    uint32_t slot = pc & 0xFFFF;
    g_sancov_map[slot].fetch_add(1, std::memory_order_relaxed);
}
