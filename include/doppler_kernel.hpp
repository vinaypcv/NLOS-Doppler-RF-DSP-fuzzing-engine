#pragma once
#include <cstdint>
#include <array>
#include <immintrin.h>

namespace SignalDataPlane {

struct alignas(32) IQBatch32 {
    std::array<int16_t, 32> i_samples;
    std::array<int16_t, 32> q_samples;
};

struct CrashVectorArtifact {
    uint64_t timestamp_ns;
    uint32_t thread_id;
    uint32_t event_code;
    uint64_t payload;
    uint64_t rng_seed;
    IQBatch32 input_frame;
    IQBatch32 mutated_frame;
};

class DopplerMathKernel {
public:
    static inline uint64_t FastXorshift64(uint64_t& state) noexcept {
        uint64_t x = state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        return state = x;
    }

    static inline void ApplyDopplerShiftAndMutateAVX2(
        const IQBatch32& input, 
        IQBatch32& output, 
        uint64_t& rng_state,
        uint32_t& out_event_code,
        uint64_t& out_payload
    ) noexcept {
        uint64_t rand_val = FastXorshift64(rng_state);
        uint8_t mutation_type = (rand_val >> 56) % 8;
        out_event_code = 100 + (mutation_type * 10) + (rand_val & 0x07);

        int16_t phase_shift = static_cast<int16_t>(rand_val & 0x7FFF);
        int16_t scale_factor = 32767;

        switch (mutation_type) {
            case 0: break;
            case 1: scale_factor = static_cast<int16_t>(16384 + (rand_val & 0x3FFF)); break;
            case 2: scale_factor = static_cast<int16_t>(rand_val & 0x0FFF); break;
            case 3: phase_shift = static_cast<int16_t>(rand_val ^ 0x5A5A); break;
            case 4: scale_factor = 32767; break;
            case 5: scale_factor = static_cast<int16_t>(rand_val & 0x7FFF); break;
            case 6: phase_shift = static_cast<int16_t>(rand_val << 2); break;
            case 7: scale_factor = static_cast<int16_t>((rand_val & 0x1FFF) * -1); break;
        }

        out_payload = (static_cast<uint64_t>(mutation_type) << 32) | static_cast<uint32_t>(phase_shift);

        int16_t cos_val = static_cast<int16_t>((scale_factor * ((phase_shift & 1) ? 0 : 32767)) >> 15);
        int16_t sin_val = static_cast<int16_t>((scale_factor * ((phase_shift & 1) ? 32767 : 0)) >> 15);

        __m256i vec_cos = _mm256_set1_epi16(cos_val);
        __m256i vec_sin = _mm256_set1_epi16(sin_val);

        for (size_t chunk = 0; chunk < 32; chunk += 16) {
            __m256i vec_i = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(input.i_samples.data() + chunk));
            __m256i vec_q = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(input.q_samples.data() + chunk));

            __m256i i_cos = _mm256_mulhi_epi16(vec_i, vec_cos);
            __m256i q_sin = _mm256_mulhi_epi16(vec_q, vec_sin);
            __m256i i_sin = _mm256_mulhi_epi16(vec_i, vec_sin);
            __m256i q_cos = _mm256_mulhi_epi16(vec_q, vec_cos);

            __m256i out_i = _mm256_sub_epi16(i_cos, q_sin);
            __m256i out_q = _mm256_add_epi16(i_sin, q_cos);

            _mm256_stream_si256(reinterpret_cast<__m256i*>(output.i_samples.data() + chunk), out_i);
            _mm256_stream_si256(reinterpret_cast<__m256i*>(output.q_samples.data() + chunk), out_q);
        }
    }
};

} // namespace SignalDataPlane
