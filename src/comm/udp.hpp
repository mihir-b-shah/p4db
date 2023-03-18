#pragma once

#include "comm/buffer.hpp"
#include "comm/msg.hpp"
#include "ee/defs.hpp"
#include "ee/errors.hpp"
#include "utils/spinlock.hpp"
#include "server.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

struct MessageHandler;

class UDPCommunicator {
    // using lock_t = std::mutex;
    using lock_t = SpinLock;

    lock_t mutex;

    int sock;
    PacketBuffer* recv_buffer = nullptr;

public:
    using Pkt_t = PacketBuffer;

    std::vector<struct sockaddr_in> addresses;
    msg::node_t node_id;
    msg::node_t switch_id;
    uint32_t num_nodes;
    MessageHandler* handler = nullptr;
    std::jthread thread;


    // uint16_t num_rx_queues;
    // uint16_t num_tx_queues;
    uint32_t mh_tid;
    // uint16_t spin_tx_queue;


public:
    UDPCommunicator();

    ~UDPCommunicator();


    void set_handler(MessageHandler* handler);

    void send(msg::node_t target, PacketBuffer*& pkt);
    void send(msg::node_t target, Pkt_t*& pkt, uint32_t);


    PacketBuffer* make_pkt();

private:
    PacketBuffer* receive();
    void setup(uint16_t port);
};
