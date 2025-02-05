#pragma once

#include "ee/types.hpp"
#include "utils/hex_dump.hpp"

#include <cstdint>
#include <iostream>

#define RAW_PACKETS

namespace error {

constexpr bool PRINT_ABORT_CAUSE = false;
constexpr bool LOG_TABLE = false;
constexpr bool DUMP_SWITCH_PKTS = false;

} // namespace error


constexpr auto PERIODIC_CSV_FILENAME = "periodic.csv";
constexpr bool DYNAMIC_IPS = false;
constexpr bool CHECK_DISJOINT_KEYS = false;
constexpr bool USE_1PASS_PKTS = true;

// all workload-dependent.
constexpr int N_OPS = 8;
constexpr size_t MAX_TIMES_ACCEL_ABORT = 1;
constexpr size_t REMOTE_FRAC = 10;
constexpr size_t BATCH_SIZE_TGT = 100000;
constexpr size_t MINI_BATCH_SIZE_TGT = 5000;
constexpr size_t MIN_MINI_BATCH_THR_SIZE = 50;
constexpr size_t MAX_PASSES_ACCEL = 1;
constexpr size_t MAX_OPS_PASS2_ACCEL = 8;
constexpr size_t MAX_HOT_OPS = 8;
constexpr size_t N_REGS = 72; // do even for orig mode...
constexpr size_t N_SW_LOCKS = 32;

// measure and modify (right now around 1 ms each)
constexpr uint64_t COLD_BATCH_DUR_EST_NS = 1000000ULL; 
constexpr uint64_t HOT_BATCH_DUR_EST_NS = 1000000ULL;

// #define IS_ORIG_MODE

#if defined(IS_ORIG_MODE)
constexpr bool ORIG_MODE = true;
constexpr bool USE_FLOW_ORDER = false;
constexpr bool DO_SCHED = false;
constexpr size_t SLOTS_PER_SCHED_BLOCK = 8;
#else
constexpr bool ORIG_MODE = false;
constexpr bool USE_FLOW_ORDER = true;
constexpr bool DO_SCHED = true;
constexpr size_t SLOTS_PER_SCHED_BLOCK = 32;
#endif

constexpr size_t N_ACCEL_KEYS = N_REGS * SLOTS_PER_SCHED_BLOCK;
