#include "init.hpp"


InitHandler::InitHandler(Communicator* comm) : comm(comm) {
    num_nodes = comm->num_nodes;
    nodes.resize(num_nodes);
}

void InitHandler::handle(msg::node_t node) {
    nodes.at(node) = true;
}

void InitHandler::wait() {
    while (true) {
        bool done = true;
        for (size_t n = 0; n<nodes.size(); ++n) {
            done &= (n == comm->node_id) || nodes[n];
        }

        // send out one more time, even after we received all
        for (uint32_t i = 0; i < num_nodes; ++i) {
            if (i != comm->node_id) {
                auto pkt = comm->make_pkt();
                auto msg = pkt->ctor<msg::Init>();
                msg->sender = comm->node_id;
                comm->send(msg::node_t{i}, pkt);
            }
        }

        if (done) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cerr << "Init done.\n";
}
