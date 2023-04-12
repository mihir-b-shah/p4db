
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
    //  Only works for n=2 nodes
	__atomic_add_fetch(&bar_arg->handler->received, -1, __ATOMIC_SEQ_CST);
}

BarrierHandler::BarrierHandler(Communicator* comm) : comm(comm), received(0),
	local_barrier(Config::instance().num_txn_workers, critical_wait, true) {
    num_nodes = comm->num_nodes;
}

//	This function is only ever called from the single network thread.
void BarrierHandler::handle(msg::Barrier* msg) {
	__atomic_add_fetch(&received, 1, __ATOMIC_SEQ_CST);
}

void BarrierHandler::my_wait(barrier_handler_arg_t* bar_arg) {
	for (uint32_t i = 0; i < num_nodes; ++i) {
        if (i != comm->node_id) {
            auto pkt = comm->make_pkt();
            auto msg = pkt->ctor<msg::Barrier>();
            msg->sender = comm->node_id;
            msg->num = bar_arg->id;
            comm->send(msg::node_t{i}, pkt);
        }
	}
	//	TODO: is seq cst necessary here? They used relaxed.
	while (__atomic_load_n(&received, __ATOMIC_SEQ_CST) != (comm->num_nodes-1)) {
		__builtin_ia32_pause();
	}
}

static uint32_t id_ctr = 1;

void BarrierHandler::wait_workers() {
	barrier_handler_arg_t arg;
	arg.handler = this;
    arg.id = __atomic_fetch_add(&id_ctr, 1, __ATOMIC_SEQ_CST);
	local_barrier.wait(&arg);
}

void BarrierHandler::wait_nodes() {
	barrier_handler_arg_t arg;
	arg.handler = this;
    arg.id = __atomic_fetch_add(&id_ctr, 1, __ATOMIC_SEQ_CST);
	critical_wait(&arg);
    __sync_synchronize();
}
