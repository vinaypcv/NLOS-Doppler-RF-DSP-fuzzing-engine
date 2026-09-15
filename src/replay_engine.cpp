#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

struct IQSample { int16_t i; int16_t q; };
struct RFFrameHeader { uint32_t sync_word; uint16_t payload_len; uint16_t crc16; };

struct IQFrame32 {
    RFFrameHeader header;
    IQSample samples[32];
};

struct StateSnapshotFrame {
    double initial_agc_gain;
    IQFrame32 frame;
};

class GoldenReferenceModel {
public:
    static double ComputeEnergy(const IQFrame32& frame) {
        double total_energy = 0.0;
        size_t requested_count = frame.header.payload_len > 0 ? frame.header.payload_len : 32;
        size_t process_count = std::min<size_t>(requested_count, 32);

        for (size_t i = 0; i < process_count; ++i) {
            total_energy += std::sqrt(static_cast<double>(frame.samples[i].i) * frame.samples[i].i +
                                      static_cast<double>(frame.samples[i].q) * frame.samples[i].q);
        }
        return total_energy;
    }
};

class TargetDSPDecoder {
    double agc_gain_state_{1.0};
    static constexpr double MIN_GAIN = 0.10;
    static constexpr double MAX_GAIN = 2.00;
public:
    void SetAGCGainState(double gain) { 
        agc_gain_state_ = std::clamp(gain, MIN_GAIN, MAX_GAIN); 
    }

    int32_t ComputeEnergyWithAGC(const IQFrame32& frame) {
        int32_t accumulated_q7_energy = 0;
        size_t requested_count = frame.header.payload_len > 0 ? frame.header.payload_len : 32;
        size_t process_count = std::min<size_t>(requested_count, 32);
        
        double frame_peak = 0.0;
        for (size_t idx = 0; idx < process_count; ++idx) {
            double sample_mag = std::abs(frame.samples[idx].i) + std::abs(frame.samples[idx].q);
            if (sample_mag > frame_peak) frame_peak = sample_mag;
        }

        if (frame_peak > 400.0) agc_gain_state_ *= 0.85;
        else if (frame_peak < 100.0) agc_gain_state_ *= 1.05;

        agc_gain_state_ = std::clamp(agc_gain_state_, MIN_GAIN, MAX_GAIN);

        for (size_t idx = 0; idx < process_count; ++idx) {
            int32_t scaled_i = static_cast<int32_t>(std::round(frame.samples[idx].i * agc_gain_state_));
            int32_t scaled_q = static_cast<int32_t>(std::round(frame.samples[idx].q * agc_gain_state_));
            int32_t abs_i = std::abs(scaled_i);
            int32_t abs_q = std::abs(scaled_q);
            int32_t max_val = std::max(abs_i, abs_q);
            int32_t min_val = std::min(abs_i, abs_q);

            // Calibrated Alpha-Max Coefficients (122/128, 51/128)
            accumulated_q7_energy += (122 * max_val + 51 * min_val);
        }
        return static_cast<int32_t>(std::round(((accumulated_q7_energy + 64) >> 7) / agc_gain_state_));
    }
};

void TriageFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return;

    StateSnapshotFrame snapshot{};
    file.read(reinterpret_cast<char*>(&snapshot), sizeof(StateSnapshotFrame));

    TargetDSPDecoder decoder;
    decoder.SetAGCGainState(snapshot.initial_agc_gain);

    double golden_energy = GoldenReferenceModel::ComputeEnergy(snapshot.frame);
    int32_t target_energy = decoder.ComputeEnergyWithAGC(snapshot.frame);
    double relative_error = (std::abs(golden_energy - static_cast<double>(target_energy)) / golden_energy) * 100.0;

    std::cout << std::left << std::setw(22) << fs::path(path).filename().string()
              << std::setw(12) << snapshot.initial_agc_gain
              << std::setw(16) << golden_energy
              << std::setw(14) << target_energy
              << std::setw(12) << relative_error << "%\n";
}

int main(int argc, char** argv) {
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "=========================================================================\n";
    std::cout << "             CALIBRATED ALPHA-MAX DSP TRIAGE REPORT                      \n";
    std::cout << "=========================================================================\n";
    std::cout << std::left << std::setw(22) << "Snapshot File"
              << std::setw(12) << "AGC Gain"
              << std::setw(16) << "Golden FP64"
              << std::setw(14) << "Target Q7"
              << std::setw(12) << "Divergence" << "\n";
    std::cout << "-------------------------------------------------------------------------\n";

    if (argc > 1) {
        TriageFile(argv[1]);
    } else {
        std::vector<fs::path> files;
        if (fs::exists("crashes")) {
            for (const auto& entry : fs::directory_iterator("crashes")) {
                if (entry.path().extension() == ".bin") {
                    files.push_back(entry.path());
                }
            }
            std::sort(files.begin(), files.end());
            for (size_t i = 0; i < std::min<size_t>(files.size(), 15); ++i) {
                TriageFile(files[i].string());
            }
        }
    }
    std::cout << "=========================================================================\n";
    return 0;
}
