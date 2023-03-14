#pragma once

#include <comm/comm.hpp>
#include <comm/msg.hpp>
#include <utils/rbarrier.hpp>

#include <cstdint>
#include <queue>
#include <pthread.h>

struct BarrierHandler;
class Database;

typedef void (*single_fn_t)(Database* db);

struct barrier_handler_arg_t {
	BarrierHandler* handler;
	bool is_hard;
	uint32_t mini_batch_num;
	single_fn_t reset_fn;
	Database* db;
};

struct queued_msg_t {
    msg::node_t sender;
	bool is_hard;
	uint32_t mini_batch_num;

	queued_msg_t(msg::node_t s, bool hard, uint32_t n) : sender(s), is_hard(hard), mini_batch_num(n) {}
};

struct BarrierHandler {
    Communicator* comm;
    uint32_t num_nodes;
	uint32_t all_nodes_mask;
	uint32_t* mini_batch_ids;

	uint32_t soft_received;
    uint32_t hard_received;

	//	TODO This is my barrier impl, is this a problem?
	reusable_barrier_t local_barrier;
	//	TODO Replace with a circular queue.
	std::queue<queued_msg_t> q;

    BarrierHandler(Communicator* comm);
    ~BarrierHandler() {
		delete[] mini_batch_ids;
	}

    void handle(msg::Barrier* msg);
    void wait_workers_soft();
    void wait_workers_hard(uint32_t* mini_batch_id, single_fn_t fn, Database* arg);
    void wait_workers();
    void wait_nodes();
    void my_wait(barrier_handler_arg_t* arg);
};
