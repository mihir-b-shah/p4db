#pragma once

#include <comm/comm.hpp>
#include <comm/msg.hpp>
#include <utils/rbarrier.hpp>

#include <cstdint>
#include <pthread.h>

struct BarrierHandler;

struct barrier_handler_arg_t {
	BarrierHandler* handler;
	bool is_hard;
	bool dont_store;
	uint32_t mini_batch_num;
	uint32_t* thr_batch_done_ct;
};

struct BarrierHandler {
    Communicator* comm;
    uint32_t num_nodes;
	uint32_t* mini_batch_ids;

	uint32_t soft_received;
    uint32_t hard_received;

	// TODO	This is my barrier impl, is this a problem?
	reusable_barrier_t local_barrier;

    BarrierHandler(Communicator* comm);
    ~BarrierHandler() {
		delete[] mini_batch_ids;
	}

    void handle(msg::Barrier* msg);
    void wait_workers_soft();
    void wait_workers_hard(uint32_t* mini_batch_id, uint32_t* thr_batch_done_ct);
    void wait_workers();
    void wait_nodes();
    void my_wait(barrier_handler_arg_t* arg);

private:
};
