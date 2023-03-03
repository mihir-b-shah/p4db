#include "barrier.hpp"

#include "main/config.hpp"

struct barrier_handler_arg_t {
	BarrierHandler* handler;
	uint32_t my_wait;
};

static void* critical_wait(void* arg) {
	barrier_handler_arg_t* bar_arg = (barrier_handler_arg_t*) arg;
    if (bar_arg->my_wait == 0) {
        bar_arg->handler->wait();
    }
	return NULL;
}

BarrierHandler::BarrierHandler(Communicator* comm) : comm(comm),
	local_barrier(Config::instance().num_txn_workers, critical_wait) {
    num_nodes = comm->num_nodes;
}

void BarrierHandler::handle() {
    received.fetch_add(1, std::memory_order_relaxed);
}

void BarrierHandler::wait_nodes() {
    wait();
}

void BarrierHandler::wait_workers() {
	barrier_handler_arg_t arg;
	arg.handler = this;
	arg.my_wait = local.fetch_add(1);
	local_barrier.wait(&arg, false);
}

// private
void BarrierHandler::wait() {
    for (uint32_t i = 0; i < num_nodes; ++i) {
        auto pkt = comm->make_pkt();
        auto msg = pkt->ctor<msg::Barrier>();
        msg->sender = comm->node_id;
        comm->send(msg::node_t{i}, pkt);
    }

    while (received.load(std::memory_order_relaxed) < num_nodes) {
        __builtin_ia32_pause();
    }

    std::cerr << "Barrier wait done.\n";
    // std::this_thread::sleep_for(std::chrono::milliseconds(10));
    local.store(0, std::memory_order_release);
    received.fetch_sub(num_nodes, std::memory_order_release);
}
