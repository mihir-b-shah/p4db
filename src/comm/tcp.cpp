#include "tcp.hpp"

#include "comm/msg.hpp"
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
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <errno.h>

static constexpr size_t MAX_NODES = 10;

static_assert(MSG_SIZE <= PacketBuffer::BUF_SIZE);

static uint64_t calls_recv = 0;
static uint64_t micros_recv = 0;
void tcp_stats() {
	printf("calls: %lu, micros: %lu\n", calls_recv, micros_recv);
}
static uint64_t get_micros(struct timespec tv) {
	return (tv.tv_sec * 1000000) + (tv.tv_nsec / 1000);
}

static void realloc_bufs(TCPCommunicator* comm) {
    for (size_t i = 0; i<N_RECV_BUFFERS; ++i) {
        comm->recv_buffers[i] = PacketBuffer::alloc();
    }
}

TCPCommunicator::TCPCommunicator() {
    atexit(tcp_stats);
    auto& config = Config::instance();
    int rc;

    node_id = config.node_id;
    switch_id = config.switch_id;
    num_nodes = config.num_nodes;
    mh_tid = config.num_txn_workers;

    node_sockfds.resize(config.num_nodes);
    realloc_bufs(this);

    //  Set up a topology where I, node_id, am the client for [0,node_id),
    //  and the server for [node_id+1,num_nodes). I can't connect to myself.
    //  So in a 3 node system, node 0 will call accept twice, node 1 will call
    //  accept once then connect once, node 2 will call connect twice. 
    //  So I need server_addr's for each connection I am the server for. 
    //  And then, repeat in reverse.

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
	printf("err: %d\n", errno);
    assert(rc == 0);
	rc = listen(parent_sock, config.num_nodes+5);
    assert(rc == 0);

	struct sockaddr_in client_addrs[MAX_NODES];
    for (size_t n = 1+config.node_id; n<config.num_nodes; ++n) {
        socklen_t client_addr_len = sizeof(client_addrs[n]);
        int client_sock = accept(parent_sock, (sockaddr*) &client_addrs[n], &client_addr_len);
        assert(client_sock >= 0);

	    int opt_val = 1;
	    rc = setsockopt(client_sock, SOL_TCP, TCP_NODELAY, &opt_val, sizeof(opt_val));
        assert(rc == 0);

        size_t j;
        for (j = 0; j<config.num_nodes; ++j) {
            if (strcmp(inet_ntoa(client_addrs[n].sin_addr), config.servers[j].ip.c_str()) == 0) {
                // set_sock_timeout(client_sock);
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
        // set_sock_timeout(sockfd);
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
        std::vector<Pkt_t*> pkts;
        pkts.reserve(N_RECV_BUFFERS);

        while (!token.stop_requested()) {
            pkts.resize(0);
            receive(pkts);

            for (Pkt_t* pkt : pkts) {
                handler->handle(pkt);
            }
        }
    });
}

void TCPCommunicator::send(msg::node_t target, Pkt_t*& pkt, uint32_t) {
    return send(target, pkt);
}

void TCPCommunicator::send(msg::node_t target, Pkt_t*& pkt) {
    //  Can't send to myself.
    assert(target < node_sockfds.size() && ((uint32_t) target) != node_id);
    // fprintf(stderr, "Sent pkt to tgt=%u.\n", (uint32_t) target);
    int len = ::send(node_sockfds[(uint32_t) target], (const void*) &pkt->buffer[0], MSG_SIZE, 0);
    assert(len == MSG_SIZE);
    pkt->free();
    pkt = nullptr; // to detect cause segfault on write
}

TCPCommunicator::Pkt_t* TCPCommunicator::make_pkt() {
    return PacketBuffer::alloc();
}

void TCPCommunicator::receive(std::vector<Pkt_t*>& pkts) {
    /*  We initialize recv_buffer in constructor, and give it a fresh buffer every time it is
        consumed here. This is single-threaded, so it should be safe. */
    assert(node_sockfds.size() == 2);
    int sock = node_id == 0 ? node_sockfds[1] : node_sockfds[0];

	int rc;
	struct timespec ts_start;
	rc = clock_gettime(CLOCK_MONOTONIC, &ts_start);
	assert(rc == 0);

    //  SO_RCVLOWAT=1 byte on most applications, so specifying a larger MSG_SIZE is ok.
    int len = 0;
    char buf[MSG_SIZE * N_RECV_BUFFERS];

    while (true) {
        rc = recv(sock, &buf[0]+len, N_RECV_BUFFERS*MSG_SIZE-len, 0);
        assert(rc >= 0);
        len += rc;
        if (len % MSG_SIZE == 0) {
            break;
        }
    }

    assert(len % MSG_SIZE == 0);
    // now copy into the different buffers.
    size_t n_filled = len/MSG_SIZE;
    for (size_t i = 0; i<n_filled; ++i) {
        memcpy((void*) &recv_buffers[i]->buffer[0], &buf[0] + MSG_SIZE*i, MSG_SIZE);
        recv_buffers[i]->len = 0;
        pkts.push_back(recv_buffers[i]);
    }

	struct timespec ts_end;
	rc = clock_gettime(CLOCK_MONOTONIC, &ts_end);
	assert(rc == 0);

	micros_recv += get_micros(ts_end) - get_micros(ts_start);	
	calls_recv += 1;
    realloc_bufs(this);
}
