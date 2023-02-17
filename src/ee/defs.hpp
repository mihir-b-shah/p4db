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
constexpr auto SINGLE_NUMA = false;
constexpr bool DYNAMIC_IPS = false;

namespace error {

constexpr bool PRINT_ABORT_CAUSE = false;
constexpr bool LOG_TABLE = false;
constexpr bool DUMP_SWITCH_PKTS = false;

} // namespace error

// constexpr uint64_t NUM_KVS = 10'000'000;
constexpr int NUM_OPS = 16;
constexpr size_t BATCH_SIZE_TGT = 5000;

