#pragma once

#include <comm/comm.hpp>
#include <utils/rbarrier.hpp>

#include <atomic>
#include <cstdint>
#include <pthread.h>

struct BarrierHandler {
    Communicator* comm;
    uint32_t num_nodes;
    std::atomic<uint32_t> received{0};
    std::atomic<uint32_t> local{0};

	// TODO I changed this, is this a problem?
	reusable_barrier_t local_barrier;

    BarrierHandler(Communicator* comm);
    ~BarrierHandler() {}

    void handle();
    void wait_nodes();
    void wait_workers();

private:
    void wait();
};
