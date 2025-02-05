#pragma once

#include "comm/comm.hpp"
#include "comm/msg.hpp"
#include "main/config.hpp"
#include "ee/defs.hpp"
#include "ee/errors.hpp"
#include "ee/future.hpp"
#include "handlers/barrier.hpp"
#include "handlers/init.hpp"
#include "handlers/tuple_put_res.hpp"

#include <tbb/concurrent_hash_map.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <pthread.h>
#include <thread>
#include <unordered_map>
#include <vector>


struct Database;

struct MessageHandler {
    using Pkt_t = Communicator::Pkt_t;
    static constexpr auto NUM_FUTURES = 1024;

    Database& db;
    Communicator* comm;
    uint32_t tid;

    InitHandler init;
    BarrierHandler barrier;
    TuplePutResHandler putresponses;

    std::atomic<msg::id_t> next_id{0};
    /*  TODO only the structure of the map needs to be protected, the keys themselves are guaranteed
        never to be accessed concurrently- is the concurrent map a bottleneck? */
    typedef tbb::concurrent_hash_map<msg::id_t, AbstractFuture*> future_map_t;
    future_map_t open_futures;

    MessageHandler(Database& db, Communicator* comm);
    MessageHandler(MessageHandler&&) = default;
    MessageHandler(const MessageHandler&) = delete;


    msg::id_t set_new_id(msg::Header* msg);

    void add_future(msg::id_t msg_id, AbstractFuture* future);


    void handle(Pkt_t* pkt);

private:
    void handle(Pkt_t* pkt, msg::Init* msg);
    void handle(Pkt_t* pkt, msg::Barrier* msg);

    void handle(Pkt_t* pkt, msg::TupleGetReq* req);
    void handle(Pkt_t* pkt, msg::TupleGetRes* res);
    void handle(Pkt_t* pkt, msg::TuplePutReq* req);
    void handle(Pkt_t* pkt, msg::TuplePutRes* res);
};
