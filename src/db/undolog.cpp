#include "undolog.hpp"

#include "comm/msg_handler.hpp"

#include <stdlib.h>
#include <x86intrin.h>

/* Private methods */

static uint64_t wait_cycles[8] = {};
static uint64_t log_cycles[8] = {};

static void dump_times() {
    for (size_t i = 0; i<8; ++i) {
        printf("On core %lu: wait-commit cycles: %lu, log-commit cycles: %lu\n", i, wait_cycles[i], log_cycles[i]);
    }
}

__attribute__((constructor))
static void setup_exit() {
    atexit(dump_times);
}

void Undolog::clear(const timestamp_t ts) {
    unsigned loc;
    uint64_t s = __builtin_ia32_rdtscp(&loc);
    _mm_lfence();

    for (auto& action : actions) {
        action->clear(comm, tid, ts);
    }
    pool.clear();
    actions.clear();

    uint64_t log_ts = __builtin_ia32_rdtscp(&loc);
    _mm_lfence();

    comm->handler->putresponses.wait(tid); // wait for all remote responses

    uint64_t e = __builtin_ia32_rdtscp(&loc);
    _mm_lfence();

    log_cycles[WorkerContext::get().tid] += log_ts-s;
    wait_cycles[WorkerContext::get().tid] += e-log_ts;
}


void Undolog::clear_last_n(const timestamp_t ts, const size_t n) {
    if (actions.size() < n) {
        throw std::runtime_error("tried clearing more in undolog than that is there...");
    }
    for (size_t i = 0; i < n; i++) {
        auto action = actions.back();
        action->clear(comm, tid, ts);
        actions.pop_back();
    }
    // putresponses += 1 on remote.clear()
    comm->handler->putresponses.wait(tid); // wait for all remote responses
}
