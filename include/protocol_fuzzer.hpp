#pragma once
#include <cstdint>
#include <array>

namespace ProtocolPlane {

enum class RRCState : uint8_t { RRC_IDLE = 0, RRC_CONNECTED = 1, RRC_HANDOVER = 2, STATE_COUNT = 3 };
enum class RRCEvent : uint8_t { CONN_REQ = 0, LINK_FAIL = 1, HO_CMD = 2, HO_COMPLETE = 3, EVENT_COUNT = 4 };

struct RRCMessage {
    uint8_t event_id;
    uint8_t transaction_id;
    uint16_t signal_power_dbm;
    uint32_t payload_checksum;
};

using TransitionFunction = RRCState(*)(uint16_t signal_power) noexcept;

inline RRCState HandleIdleConnReq(uint16_t) noexcept { return RRCState::RRC_CONNECTED; }
inline RRCState HandleConnectedFail(uint16_t) noexcept { return RRCState::RRC_IDLE; }
inline RRCState HandleConnectedHO(uint16_t power) noexcept { return (power < 30) ? RRCState::RRC_IDLE : RRCState::RRC_HANDOVER; }
inline RRCState HandleHandoverComplete(uint16_t) noexcept { return RRCState::RRC_CONNECTED; }
inline RRCState DefaultInvalidPath(uint16_t) noexcept { return RRCState::RRC_CONNECTED; }

constexpr auto BuildCompileTimeMatrix() noexcept {
    std::array<std::array<TransitionFunction, static_cast<size_t>(RRCEvent::EVENT_COUNT)>, static_cast<size_t>(RRCState::STATE_COUNT)> matrix{};
    for (size_t s = 0; s < static_cast<size_t>(RRCState::STATE_COUNT); ++s) {
        for (size_t e = 0; e < static_cast<size_t>(RRCEvent::EVENT_COUNT); ++e) {
            matrix[s][e] = DefaultInvalidPath;
        }
    }
    matrix[0][0] = HandleIdleConnReq;
    matrix[1][1] = HandleConnectedFail;
    matrix[1][2] = HandleConnectedHO;
    matrix[2][3] = HandleHandoverComplete;
    return matrix;
}

class FSMEngine {
public:
    constexpr FSMEngine() noexcept : transition_matrix(BuildCompileTimeMatrix()) {}

    inline RRCState ComputeTransition(RRCState current, RRCEvent event, uint16_t signal_power) const noexcept {
        return transition_matrix[static_cast<size_t>(current)][static_cast<size_t>(event)](signal_power);
    }
private:
    std::array<std::array<TransitionFunction, static_cast<size_t>(RRCEvent::EVENT_COUNT)>, static_cast<size_t>(RRCState::STATE_COUNT)> transition_matrix;
};

class AIPatternEngine {
public:
    constexpr AIPatternEngine() noexcept : state_exploration_weights{12, -45, 96, -8, 64, -11, 33, 7} {}
    
    inline RRCEvent PredictNextDestructiveEvent(size_t tracking_index) const noexcept {
        int8_t internal_bias = state_exploration_weights[tracking_index & 0x07];
        if (internal_bias > 50) return RRCEvent::LINK_FAIL;
        if (internal_bias > 0)  return RRCEvent::HO_CMD;
        if (internal_bias > -20) return RRCEvent::HO_COMPLETE;
        return RRCEvent::CONN_REQ;
    }
private:
    std::array<int8_t, 8> state_exploration_weights;
};

} // namespace ProtocolPlane
