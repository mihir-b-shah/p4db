#pragma once

#include "ee/types.hpp"
#include "utils/hex_dump.hpp"

#include <cstdint>
#include <iostream>

constexpr auto PERIODIC_CSV_FILENAME = "periodic.csv";
constexpr bool DYNAMIC_IPS = false;
constexpr bool CHECK_DISJOINT_KEYS = false;
constexpr bool USE_FLOW_ORDER = true;
constexpr bool ORIG_MODE = false;
constexpr bool USE_1PASS_PKTS = true;
constexpr bool DO_SCHED = true;
constexpr bool RAW_PACKETS = false;

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
constexpr size_t MAX_PASSES_ACCEL = 1;
constexpr size_t MAX_OPS_PASS2_ACCEL = 8;
constexpr size_t MAX_HOT_OPS = 8;
constexpr size_t N_REGS = 72; // 20
constexpr size_t N_SW_LOCKS = 32;

// cross-check with sched.cpp
constexpr size_t SLOTS_PER_SCHED_BLOCK = 8;
constexpr size_t N_ACCEL_KEYS = N_REGS * SLOTS_PER_SCHED_BLOCK;

// measure and modify (right now around 1 ms each)
constexpr uint64_t COLD_BATCH_DUR_EST_NS = 1000000ULL; 
constexpr uint64_t HOT_BATCH_DUR_EST_NS = 1000000ULL;

static_assert((!USE_FLOW_ORDER && MAX_PASSES_ACCEL == 2) || (USE_FLOW_ORDER && MAX_PASSES_ACCEL == 1));
static_assert(!ORIG_MODE || (MAX_PASSES_ACCEL == 2 && MAX_OPS_PASS2_ACCEL == N_OPS));
