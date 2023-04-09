
#include "barrier.hpp"
#include "main/config.hpp"

#include <algorithm>

/*	Subtracting n, like is done in p4db's version of this file, at line 49, I think is incorrect.
	Situation is when I am waiting for a response from one node. But, since my sends have been received
	by another node, it unblocks and quickly moves onto another barrier. This barrier quickly does
	a send. Since I cannot differentiate between sends from different nodes when incrementing the
	variable receive, I think I have cleared when I have not- and cause lots of problems.

	Our simple solution- do not service handle() requests I am not waiting- instead queue them up. */
static void critical_wait(void* arg) {
	barrier_handler_arg_t* bar_arg = (barrier_handler_arg_t*) arg;
	bar_arg->handler->my_wait(bar_arg);
	__atomic_store_n(&bar_arg->handler->received, 0, __ATOMIC_SEQ_CST);
}

BarrierHandler::BarrierHandler(Communicator* comm) : comm(comm), received(0),
	local_barrier(Config::instance().num_txn_workers, critical_wait, true) {
    num_nodes = comm->num_nodes;
    all_nodes_mask = (1 << comm->num_nodes) - 1;
}

//	This function is only ever called from the single network thread.
void BarrierHandler::handle(msg::Barrier* msg) {
	q.emplace(msg->sender);
	size_t q_size = q.size();
	for (size_t i = 0; i<q_size; ++i) {
		uint32_t sender = (uint32_t) q.front().sender;
		if (__atomic_load_n(&received, __ATOMIC_SEQ_CST) & (1 << sender)) {
			q.push(q.front());
		} else {
			__atomic_add_fetch(&received, 1 << sender, __ATOMIC_SEQ_CST);
		}
		q.pop();
	}
}

void BarrierHandler::my_wait(barrier_handler_arg_t* bar_arg) {
	for (uint32_t i = 0; i < num_nodes; ++i) {
		auto pkt = comm->make_pkt();
		auto msg = pkt->ctor<msg::Barrier>();
		msg->sender = comm->node_id;
		comm->send(msg::node_t{i}, pkt);
	}
	//	TODO: is seq cst necessary here? They used relaxed.
	while (__atomic_load_n(&received, __ATOMIC_SEQ_CST) != all_nodes_mask) {
		__builtin_ia32_pause();
	}
}

void BarrierHandler::wait_workers() {
	barrier_handler_arg_t arg;
	arg.handler = this;
	local_barrier.wait(&arg);
}

void BarrierHandler::wait_nodes() {
	barrier_handler_arg_t arg;
	arg.handler = this;
	critical_wait(&arg);
}
