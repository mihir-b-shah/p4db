#pragma once

#include "ee/types.hpp"
#include "utils/hex_dump.hpp"

#include <cstdint>
#include <iostream>


using namespace std::chrono_literals;

enum class StatsBitmask : uint64_t {
    NONE = 0x00,
    COUNTER = 0x01,
    CYCLES = 0x02,
    PERIODIC = 0x04,

    ALL = 0xffffffffffffffff,
};
constexpr StatsBitmask operator|(StatsBitmask lhs, StatsBitmask rhs) {
    using T = std::underlying_type<StatsBitmask>::type;
    return static_cast<StatsBitmask>(static_cast<T>(lhs) | static_cast<T>(rhs));
}
constexpr bool operator&(StatsBitmask lhs, StatsBitmask rhs) {
    using T = std::underlying_type<StatsBitmask>::type;
    return static_cast<T>(lhs) & static_cast<T>(rhs);
}

// constexpr StatsBitmask ENABLED_STATS = StatsBitmask::COUNTER | StatsBitmask::CYCLES | StatsBitmask::PERIODIC;

constexpr StatsBitmask ENABLED_STATS = StatsBitmask::ALL;
constexpr bool STATS_PER_WORKER = false;
constexpr auto STATS_CYCLE_SAMPLE_TIME = 10ms; //100us;
constexpr auto STATS_PERIODIC_SAMPLE_TIME = 500ms;
constexpr auto PERIODIC_CSV_FILENAME = "periodic.csv";
constexpr bool DYNAMIC_IPS = false;
constexpr bool USE_FLOW_ORDER = false;
constexpr bool ORIG_MODE = true;

constexpr size_t N_CORES = 8;

namespace error {

constexpr bool PRINT_ABORT_CAUSE = false;
constexpr bool LOG_TABLE = false;
constexpr bool DUMP_SWITCH_PKTS = false;

} // namespace error

// all workload-dependent.
constexpr int N_OPS = 8;
constexpr size_t MAX_TIMES_ACCEL_ABORT = 1;
constexpr size_t REMOTE_FRAC = 10;
constexpr size_t BATCH_SIZE_TGT = 100000;
constexpr size_t MINI_BATCH_SIZE_TGT = 5000;
constexpr size_t MIN_MINI_BATCH_THR_SIZE = 50;
constexpr size_t MAX_PASSES_ACCEL = 2;
constexpr size_t MAX_OPS_PASS2_ACCEL = 8;

// measure and modify (right now around 1 ms each)
constexpr uint64_t COLD_BATCH_DUR_EST_NS = 1000000ULL; 
constexpr uint64_t HOT_BATCH_DUR_EST_NS = 1000000ULL;

static_assert((!USE_FLOW_ORDER && MAX_PASSES_ACCEL == 2) || (USE_FLOW_ORDER && MAX_PASSES_ACCEL == 1));
static_assert(!ORIG_MODE || (MAX_PASSES_ACCEL == 2 && MAX_OPS_PASS2_ACCEL == N_OPS));
