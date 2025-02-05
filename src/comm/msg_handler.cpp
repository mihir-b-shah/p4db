#include "msg_handler.hpp"

#include "main/config.hpp"
#include "ee/database.hpp"

MessageHandler::MessageHandler(Database& db, Communicator* comm)
    : db(db), comm(comm), tid(comm->mh_tid), init(comm), barrier(comm), open_futures(NUM_FUTURES) {
    comm->set_handler(this);
}

msg::id_t MessageHandler::set_new_id(msg::Header* msg) {
    return msg->msg_id = msg::id_t{next_id.fetch_add(1)};
}

void MessageHandler::add_future(msg::id_t msg_id, AbstractFuture* future) {
    future_map_t::accessor acc;
    bool success = open_futures.insert(acc, msg_id);
    assert(success == true);
    acc->second = future;
}

void MessageHandler::handle(Pkt_t* pkt) {
    using namespace msg;

    auto msg = pkt->as<msg::Header>();

    if constexpr (error::DUMP_SWITCH_PKTS) {
        std::cout << "Received:\n";
        pkt->dump(std::cout);
    }

    switch (msg->type) {
        case Type::INIT:
            return handle(pkt, msg->as<msg::Init>());
        case Type::BARRIER:
            return handle(pkt, msg->as<msg::Barrier>());
        case Type::TUPLE_GET_REQ:
            return handle(pkt, msg->as<msg::TupleGetReq>());
        case Type::TUPLE_GET_RES:
            return handle(pkt, msg->as<msg::TupleGetRes>());
        case Type::TUPLE_PUT_REQ:
            return handle(pkt, msg->as<msg::TuplePutReq>());
        case Type::TUPLE_PUT_RES:
            return handle(pkt, msg->as<msg::TuplePutRes>());
    }
}


// Private methods

void MessageHandler::handle(Pkt_t* pkt, msg::Init* msg) {
    // std::cout << "Received msg::Init from " << msg->sender << '\n';
    init.handle(msg->sender);
    pkt->free();
}

void MessageHandler::handle(Pkt_t* pkt, msg::Barrier* msg) {
    barrier.handle(msg);
    pkt->free();
}

void MessageHandler::handle(Pkt_t* pkt, msg::TupleGetReq* req) {
    // std::cerr << "msg::TupleGetReq tid=" << req->tid << " rid=" << req->rid << " mode=" << static_cast<int>(req->mode) << '\n';

    auto table = db[req->tid];
    table->remote_get(pkt, req);
}

void MessageHandler::handle(Pkt_t* pkt, msg::TupleGetRes* res) {
    // std::cerr << "msg::TupleGetRes tid=" << res->tid << " rid=" << res->rid << " mode=" << static_cast<int>(res->mode) << '\n';
    msg::id_t id = res->msg_id;
    future_map_t::accessor acc;
    bool found = open_futures.find(acc, id);
    assert(found == true);

    AbstractFuture* future = acc->second;
    bool success = open_futures.erase(acc);
    assert(success == true);

    future->set_pkt(pkt);
}

void MessageHandler::handle(Pkt_t* pkt, msg::TuplePutReq* req) {
    // std::cerr << "msg::TuplePutReq tid=" << req->tid << " rid=" << req->rid << " mode=" << static_cast<int>(req->mode) << '\n';
    auto table = db[req->tid];
	// fprintf(stderr, "Executing put with id: %u\n", req->last_acq_pack);
    table->remote_put(req);

    auto res = req->convert<msg::TuplePutRes>();
    pkt->resize(res->size());
    comm->send(res->sender, pkt, tid);
}

void MessageHandler::handle(Pkt_t* pkt, msg::TuplePutRes* res) {
    // std::cerr << "msg::TuplePutRes tid=" << res->tid << " rid=" << res->rid << " mode=" << static_cast<int>(res->mode) << '\n';

    if (res->mode == AccessMode::INVALID) [[unlikely]] {
        std::cerr << "Received TuplePutRes with invalid AccessMode.\n";
    }

    putresponses.handle(res->sender);
    pkt->free();
}
