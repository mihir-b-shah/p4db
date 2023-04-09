#include "tcp.hpp"

#include "main/config.hpp"
#include "msg_handler.hpp"
#include "utils/context.hpp"

#include <cstdio>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <errno.h>
#include <sys/epoll.h>

static constexpr size_t MAX_NODES = 10;

static constexpr size_t MSG_SIZE = 100;
static_assert(MSG_SIZE <= PacketBuffer::BUF_SIZE);

TCPCommunicator::TCPCommunicator() {
    auto& config = Config::instance();
    int rc;

    node_id = config.node_id;
    switch_id = config.switch_id;
    num_nodes = config.num_nodes;
    mh_tid = config.num_txn_workers;

    node_sockfds.resize(config.num_nodes);
    recv_buffer = Pkt_t::alloc();

    /*  Set up a topology where I, node_id, am the client for [0,node_id),
        and the server for [node_id+1,num_nodes). I can't connect to myself.
        So in a 3 node system, node 0 will call accept twice, node 1 will call
        accept once then connect once, node 2 will call connect twice. 
        
        So I need server_addr's for each connection I am the server for. 
        And then, repeat in reverse. */

    assert(config.num_nodes <= MAX_NODES);

    //  Now I am the server
    int parent_sock = socket(AF_INET, SOCK_STREAM, 0);
	assert(parent_sock >= 0);

	int opt_val = 1;
    rc = setsockopt(parent_sock, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val));
    assert(rc == 0);

    struct sockaddr_in my_addr;
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(config.servers[config.node_id].port);
	my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	rc = bind(parent_sock, (sockaddr*) &my_addr, sizeof(my_addr));
    assert(rc == 0);
	rc = listen(parent_sock, config.num_nodes+5);
    assert(rc == 0);

	struct sockaddr_in client_addrs[MAX_NODES];
    for (size_t n = 1+config.node_id; n<config.num_nodes; ++n) {
        socklen_t client_addr_len = sizeof(client_addrs[n]);
        int client_sock = accept(parent_sock, (sockaddr*) &client_addrs[n], &client_addr_len);

	    int opt_val = 1;
	    rc = setsockopt(client_sock, SOL_TCP, TCP_NODELAY, &opt_val, sizeof(opt_val));
        assert(rc == 0);

        size_t j;
        for (j = 0; j<config.num_nodes; ++j) {
            if (strcmp(inet_ntoa(client_addrs[n].sin_addr), config.servers[j].ip.c_str()) == 0) {
                fprintf(stderr, "Set sockfds[%lu]\n", j);
                node_sockfds[j] = client_sock;
                break;
            }
        }
        assert(j > config.node_id && j < config.num_nodes);
        assert(rc == 0);
    }

	struct sockaddr_in server_addrs[MAX_NODES];
	memset(&server_addrs[0], 0, MAX_NODES*sizeof(server_addrs[0]));

    // I am the client here
    for (size_t n = 0; n<config.node_id; ++n) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        assert(sockfd >= 0);

	    int opt_val = 1;
	    rc = setsockopt(sockfd, SOL_TCP, TCP_NODELAY, &opt_val, sizeof(opt_val));
        assert(rc == 0);

        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(config.servers[n].port);
        std::string& sched_ip = config.servers[n].ip;
        inet_aton((const char*) sched_ip.c_str(), &server_addr.sin_addr);

        while (1) {
            rc = connect(sockfd, (sockaddr*) &server_addr, (socklen_t) sizeof(struct sockaddr_in));
            if (rc == 0) {
                break;
            } else if (rc == -1 && errno == ECONNREFUSED) {
                sleep(1);
                continue;
            } else {
                assert(false && "Invalid connect()");
            }
        }
        fprintf(stderr, "Set sockfds[%lu]\n", n);
        node_sockfds[n] = sockfd;
    }
}

void TCPCommunicator::set_handler(MessageHandler* handler) {
    this->handler = handler;
    auto& config = Config::instance();
    thread = std::jthread([&, handler](std::stop_token token) {
        const WorkerContext::guard worker_ctx;
		// TODO: change in production, right now running on single machine.
        uint32_t core = config.num_txn_workers;
        printf("Pinning tcp core on %u\n", core);
        pin_worker(core);
        while (!token.stop_requested()) {
            auto pkt = receive();
            if (!pkt) {
                //  End of all streams.
                continue;
            }
            handler->handle(pkt);
        }
    });
}

void TCPCommunicator::send(msg::node_t target, Pkt_t*& pkt, uint32_t) {
    return send(target, pkt);
}

void TCPCommunicator::send(msg::node_t target, Pkt_t*& pkt) {
    //  Can't send to myself.
    assert(target < node_sockfds.size() && ((uint32_t) target) != node_id);
    fprintf(stderr, "Sent pkt to tgt=%u.\n", (uint32_t) target);
    int len = ::send(node_sockfds[(uint32_t) target], (const void*) &pkt->buffer[0], MSG_SIZE, 0);
    assert(len == MSG_SIZE);
    pkt->free();
    pkt = nullptr; // to detect cause segfault on write
}

TCPCommunicator::Pkt_t* TCPCommunicator::make_pkt() {
    return PacketBuffer::alloc();
}

TCPCommunicator::Pkt_t* TCPCommunicator::receive() {
    /*  We initialize recv_buffer in constructor, and give it a fresh buffer every time it is
        consumed here. This is single-threaded, so it should be safe. */

    bool found = false;
    for (size_t i = 0; i<node_sockfds.size(); ++i) {
        if (i == node_id) {
            continue;
        }

        int len = recv(node_sockfds[i], recv_buffer, MSG_SIZE, MSG_DONTWAIT);
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // errno set -> nothing available, spin+come back -> doesn't mean anything about len.
            continue;
        } else if (len == 0) {
            break;
        } else {
            fprintf(stderr, "Recv pkt from src=%lu.\n", i);
            assert(len == MSG_SIZE);
            found = true;
            /*  Is this safe? Will we starve later sockets? Probably not, as long as the sockets aren't
                sending at sustained peak rate */
            break;
        }
    }

    if (!found) {
        return nullptr;
    } else {
        TCPCommunicator::Pkt_t* msg = recv_buffer;
        //  Don't know, so just init it to catch bugs.
        msg->len = 0;
        recv_buffer = Pkt_t::alloc();
        return msg;
    }
}
