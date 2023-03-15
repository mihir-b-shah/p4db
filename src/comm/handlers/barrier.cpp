
#include "barrier.hpp"

#include "main/config.hpp"

#include <algorithm>

/*	Subtracting n, like is done in p4db's version of this file, at line 49, I think is incorrect.
	Situation is when I am waiting for a response from one node. But, since my sends have been received
	by another node, it unblocks and quickly moves onto another barrier. This barrier quickly does
	a send. Since I cannot differentiate between sends from different nodes when incrementing the
	variable receive, I think I have cleared when I have not- and cause lots of problems.

	Our simple solution- do not service handle() requests I am not waiting- instead queue them up. */
static void* critical_wait(void* arg) {
	barrier_handler_arg_t* bar_arg = (barrier_handler_arg_t*) arg;
	bar_arg->handler->my_wait(bar_arg);
	if (bar_arg->is_hard) {
		//	TODO is sequentially consistent store necessary here?
		if (bar_arg->reset_fn != nullptr) {
			bar_arg->reset_fn(bar_arg->db);
		}
		__atomic_store_n(&bar_arg->handler->hard_received, 0, __ATOMIC_SEQ_CST);
	}
	//	TODO not necessary in all code-paths?
	__atomic_store_n(&bar_arg->handler->soft_received, 0, __ATOMIC_SEQ_CST);

    // fprintf(stderr, "RET crit_arg->mb_num: %u\n", bar_arg->mini_batch_num);
	return arg;
}

BarrierHandler::BarrierHandler(Communicator* comm) : comm(comm), soft_received(0), hard_received(0),
	local_barrier(Config::instance().num_txn_workers, critical_wait) {
    num_nodes = comm->num_nodes;
    all_nodes_mask = (1 << comm->num_nodes) - 1;
	mini_batch_ids = new uint32_t[num_nodes];
}

//	This function is only ever called from the single network thread.
void BarrierHandler::handle(msg::Barrier* msg) {
	q.emplace(msg->sender, msg->is_hard, msg->mini_batch_num);
	size_t q_size = q.size();
	for (size_t i = 0; i<q_size; ++i) {
		uint32_t sender = (uint32_t) q.front().sender;
		if (q.front().is_hard && 
			(__atomic_load_n(&hard_received, __ATOMIC_SEQ_CST) & (1 << sender))) {
			q.push(q.front());
		} else if (!q.front().is_hard && 
			(__atomic_load_n(&soft_received, __ATOMIC_SEQ_CST) & (1 << sender))) {
			q.push(q.front());
		} else {
			// actually process.
			if (q.front().is_hard) {
				/* 	TODO	Is a lighter memory model fine? Right now I use seq cst since I have this
					dependency, where I want mini_batch_ids to be written before the increment happens.
					TODO	Is seq cst sufficient, or do I need a fence too? */
				mini_batch_ids[sender] = q.front().mini_batch_num;
				__atomic_add_fetch(&hard_received, 1 << sender, __ATOMIC_SEQ_CST);
			} else {
				//	TODO	Relaxed prob suffices here, just didn't want to mix consistency models.
				__atomic_add_fetch(&soft_received, 1 << sender, __ATOMIC_SEQ_CST);
			}
		}
		q.pop();
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
		while (__atomic_load_n(&hard_received, __ATOMIC_SEQ_CST) != all_nodes_mask) {
			__builtin_ia32_pause();
		}
		uint32_t highest_id = 0;
		for (size_t i = 0; i<num_nodes; ++i) {
			highest_id = std::max(highest_id, mini_batch_ids[i]);
		}
		__atomic_store_n(&bar_arg->mini_batch_num, 1+highest_id, __ATOMIC_SEQ_CST);
	} else {
		for (uint32_t i = 0; i < num_nodes; ++i) {
			auto pkt = comm->make_pkt();
			auto msg = pkt->ctor<msg::Barrier>();
			msg->sender = comm->node_id;
			comm->send(msg::node_t{i}, pkt);
		}
		//	TODO: is seq cst necessary here? They used relaxed.
		while (__atomic_load_n(&soft_received, __ATOMIC_SEQ_CST) + __atomic_load_n(&hard_received, __ATOMIC_SEQ_CST) != all_nodes_mask) {
			__builtin_ia32_pause();
		}
	}
}


void BarrierHandler::wait_workers_soft() {
	barrier_handler_arg_t arg;
	arg.handler = this;
	arg.is_hard = false;
	arg.reset_fn = nullptr;
	arg.mini_batch_num = 0;
	local_barrier.wait(&arg);
    // fprintf(stderr, "Finished soft barrier.\n");
}

//  TODO THIS DOES NOT WORK, HAS WEIRD UN-INITIALIZED BEHAVIOR.
void BarrierHandler::wait_workers_hard(uint32_t* mini_batch_num, single_fn_t fn, Database* db) {
	barrier_handler_arg_t arg;
	arg.handler = this;
	arg.is_hard = true;
	arg.reset_fn = fn;
	arg.db = db;
	// safe since everyone should have same mini_batch_num in their tb.
	arg.mini_batch_num = *mini_batch_num;
    __sync_synchronize();

	barrier_handler_arg_t* crit_arg = (barrier_handler_arg_t*) local_barrier.wait(&arg);

	//	TODO I think this is safe, since locally all threads are sequentially consistent?
    uint32_t* p_mb_num = &crit_arg->mini_batch_num;
    // fprintf(stderr, "crit_mb_num: %p, crit_arg->mb_num: %u\n", p_mb_num, crit_arg->mini_batch_num);
	*mini_batch_num = __atomic_load_n(&crit_arg->mini_batch_num, __ATOMIC_SEQ_CST);
}

void BarrierHandler::wait_workers() {
	barrier_handler_arg_t arg;
	arg.handler = this;
	arg.is_hard = true;
	arg.reset_fn = nullptr;
	// safe since everyone should have same mini_batch_num in their tb.
	arg.mini_batch_num = 0;
	local_barrier.wait(&arg);
}

void BarrierHandler::wait_nodes() {
	barrier_handler_arg_t arg;
	arg.handler = this;
	arg.is_hard = true;
	arg.reset_fn = nullptr;
	arg.mini_batch_num = 0;
	critical_wait(&arg);
}
