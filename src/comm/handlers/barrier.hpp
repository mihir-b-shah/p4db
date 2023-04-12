#pragma once

#include <comm/comm.hpp>
#include <comm/msg.hpp>
#include <utils/rbarrier.hpp>

#include <cstdint>
#include <queue>
#include <pthread.h>

struct BarrierHandler;

struct barrier_handler_arg_t {
	BarrierHandler* handler;
    uint32_t id;
};

struct queued_msg_t {
	msg::node_t sender;
	queued_msg_t(msg::node_t s) : sender(s) {}
};

struct BarrierHandler {
    Communicator* comm;
    uint32_t num_nodes;
	uint32_t all_nodes_mask;
	uint32_t received;

	reusable_barrier_t local_barrier;
	std::queue<queued_msg_t> q;

    BarrierHandler(Communicator* comm);

    void handle(msg::Barrier* msg);
    void wait_workers();
    void wait_nodes();
    void my_wait(barrier_handler_arg_t* arg);
};
