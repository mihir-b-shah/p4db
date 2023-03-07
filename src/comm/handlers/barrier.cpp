
#include "barrier.hpp"

#include "main/config.hpp"

#include <algorithm>

static void* critical_wait(void* arg) {
	barrier_handler_arg_t* bar_arg = (barrier_handler_arg_t*) arg;
	bar_arg->handler->my_wait(bar_arg);
	if (bar_arg->is_hard) {
		// TODO is sequentially consistent store necessary here?
		if (!bar_arg->dont_store) {
			__atomic_store_n(&bar_arg->thr_batch_done_ct, 0, __ATOMIC_SEQ_CST);
		}
		__atomic_store_n(&bar_arg->handler->hard_received, 0, __ATOMIC_SEQ_CST);
	}
	if (!bar_arg->dont_store) {
		__atomic_store_n(&bar_arg->handler->soft_received, 0, __ATOMIC_SEQ_CST);
	}
	return arg;
}

BarrierHandler::BarrierHandler(Communicator* comm) : comm(comm),
	local_barrier(Config::instance().num_txn_workers, critical_wait) {
    num_nodes = comm->num_nodes;
	mini_batch_ids = new uint32_t[num_nodes];
}

/*	If I receive a hard barrier, I should write down the mini_batch_num. 
*/
void BarrierHandler::handle(msg::Barrier* msg) {
	if (msg->is_hard) {
		/* 	TODO	Is a lighter memory model fine? Right now I use seq cst since I have this
			dependency, where I want mini_batch_ids to be written before the increment happens.
			TODO	Is seq cst sufficient, or do I need a fence too? */
		mini_batch_ids[msg->sender] = msg->mini_batch_num;
		__atomic_add_fetch(&hard_received, 1, __ATOMIC_SEQ_CST);
	} else {
		//	TODO	Relaxed prob suffices here, just didn't want to mix consistency models.
		__atomic_add_fetch(&soft_received, 1, __ATOMIC_SEQ_CST);
	}
}

void BarrierHandler::my_wait(barrier_handler_arg_t* bar_arg) {
	//	This is a local barrier, as such, everyone is soft or everyone is hard.
	if (bar_arg->is_hard) {
		for (uint32_t i = 0; i < num_nodes; ++i) {
			auto pkt = comm->make_pkt();
			auto msg = pkt->ctor<msg::Barrier>(bar_arg->mini_batch_num);
			msg->sender = comm->node_id;
			comm->send(msg::node_t{i}, pkt);
		}
		//	TODO: is seq cst necessary here? They used relaxed.
		while (__atomic_load_n(&hard_received, __ATOMIC_SEQ_CST) < num_nodes) {
			__builtin_ia32_pause();
		}
		uint32_t highest_id = 0;
		for (size_t i = 0; i<num_nodes; ++i) {
			highest_id = std::max(highest_id, mini_batch_ids[i]);
		}
		bar_arg->mini_batch_num = 1+highest_id;
	} else {
		for (uint32_t i = 0; i < num_nodes; ++i) {
			auto pkt = comm->make_pkt();
			auto msg = pkt->ctor<msg::Barrier>();
			msg->sender = comm->node_id;
			comm->send(msg::node_t{i}, pkt);
		}
		//	TODO: is seq cst necessary here? They used relaxed.
		while (__atomic_load_n(&soft_received, __ATOMIC_SEQ_CST) + __atomic_load_n(&hard_received, __ATOMIC_SEQ_CST) < num_nodes) {
			__builtin_ia32_pause();
		}
	}
}


void BarrierHandler::wait_workers_soft() {
	barrier_handler_arg_t arg;
	arg.handler = this;
	arg.dont_store = false;
	arg.is_hard = false;
	arg.thr_batch_done_ct = NULL;
	arg.mini_batch_num = 0;
	local_barrier.wait(&arg);
}

void BarrierHandler::wait_workers_hard(uint32_t* mini_batch_num, uint32_t* thr_batch_done_ct) {
	barrier_handler_arg_t arg;
	arg.handler = this;
	arg.is_hard = true;
	arg.dont_store = false;
	arg.thr_batch_done_ct = thr_batch_done_ct;
	// safe since everyone should have same mini_batch_num in their tb.
	arg.mini_batch_num = *mini_batch_num;
	barrier_handler_arg_t* crit_arg = (barrier_handler_arg_t*) local_barrier.wait(&arg);
	//	TODO I think this is safe, since locally all threads are sequentially consistent?
	*mini_batch_num = crit_arg->mini_batch_num;
}

void BarrierHandler::wait_workers() {
	barrier_handler_arg_t arg;
	arg.handler = this;
	arg.is_hard = true;
	arg.dont_store = false;
	arg.thr_batch_done_ct = NULL;
	// safe since everyone should have same mini_batch_num in their tb.
	arg.mini_batch_num = 0;
	local_barrier.wait(&arg);
}

void BarrierHandler::wait_nodes() {
	barrier_handler_arg_t arg;
	arg.handler = this;
	arg.is_hard = true;
	arg.dont_store = true;
	arg.thr_batch_done_ct = NULL;
	arg.mini_batch_num = 0;
	my_wait(&arg);
}
