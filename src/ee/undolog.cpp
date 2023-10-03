#include "undolog.hpp"

#include "comm/msg_handler.hpp"

#include <stdlib.h>
#include <x86intrin.h>

uint64_t log_wait_time[32] = {};

static uint64_t micros_diff(struct timespec* t_start, struct timespec* t_end) {
    uint64_t s_micros = ((((uint64_t) t_start->tv_sec) * 1000000000) + t_start->tv_nsec) / 1000;
    uint64_t e_micros = ((((uint64_t) t_end->tv_sec) * 1000000000) + t_end->tv_nsec) / 1000;
    return e_micros-s_micros;
}


void Undolog::clear(const timestamp_t ts) {
    int rc;
    struct timespec ts_begin;
    rc = clock_gettime(CLOCK_REALTIME, &ts_begin);
    assert(rc == 0);

    for (auto& action : actions) {
        action->clear(comm, tid, ts);
    }
    pool.clear();
    actions.clear();
    comm->handler->putresponses.wait(tid); // wait for all remote responses

    struct timespec ts_end;
    rc = clock_gettime(CLOCK_REALTIME, &ts_end);
    assert(rc == 0);

    if (WorkerContext::context != nullptr) {
        log_wait_time[WorkerContext::get().tid] += micros_diff(&ts_begin, &ts_end);
    }
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
