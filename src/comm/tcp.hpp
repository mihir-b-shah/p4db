
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
#include <poll.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

struct MessageHandler;

static constexpr size_t N_RECV_BUFFERS = 1;

/*  No need to protect the socket with a lock-
    https://stackoverflow.com/questions/1981372/are-parallel-calls-to-send-recv-on-the-same-socket-valid/1981439#1981439 */
class TCPCommunicator {
public:
    using Pkt_t = PacketBuffer;
    
    Pkt_t* recv_buffers[N_RECV_BUFFERS];
    std::vector<int> node_sockfds;
    MessageHandler* handler = nullptr;
    std::jthread thread;
    msg::node_t node_id;
    msg::node_t switch_id;
    uint32_t num_nodes;
    uint32_t mh_tid;

public:
    TCPCommunicator();
    ~TCPCommunicator() {}

    void set_handler(MessageHandler* handler);
    void send(msg::node_t target, PacketBuffer*& pkt);
    void send(msg::node_t target, Pkt_t*& pkt, uint32_t);

    PacketBuffer* make_pkt();

private:
    void receive(std::vector<Pkt_t*>& pkts);
};
