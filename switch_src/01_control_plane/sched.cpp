
#include "handle.hpp"

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <climits>
#include <errno.h>
#include <sys/epoll.h>

#include <unordered_map>
#include <algorithm>
#include <queue>
#include <vector>

static constexpr size_t N_NODES = 2;
static constexpr uint32_t NO_BLOCK = UINT32_MAX;

struct __attribute__((packed)) alloc_req_t {
    uint64_t start_delay_ns;
    uint64_t duration_ns;
    uint32_t blk_to_free;
    uint32_t tenant_id;
    uint32_t tenant_num_nodes;
    uint32_t batch_num; // start at 1
};

struct __attribute__((packed)) alloc_resp_t {
    uint32_t batch_num;
    uint32_t alloced_blk_id;
};

struct __attribute__((packed)) alloc_ready_msg_t {
    uint32_t dummy;
};

struct tenant_info_t {
    alloc_resp_t cached;
    size_t n_cached_uses;
    size_t expected_n_fds;
    std::unordered_set<int> sock_fds;
};

int main() {
    handle_init();

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	assert(sockfd >= 0);

	int opt_val = 1;
	assert(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val)) == 0);

	static struct sockaddr_in server_addr; 
	memset(&server_addr, 0, sizeof(server_addr));

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons((unsigned short) 4001);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	int node_sockfds[N_NODES];
	struct sockaddr_in client_addrs[N_NODES];

	int bind_rc = bind(sockfd, (sockaddr*) &server_addr, sizeof(server_addr));
	assert(bind_rc == 0);

	// just a reasonable backlog quantity
	assert(listen(sockfd, N_NODES+5) == 0);
    int epfd = epoll_create(N_NODES);
    assert(epfd >= 0);
    struct epoll_event evs[N_NODES];

    for (size_t n = 0; n<N_NODES; ++n) {
        socklen_t client_addr_len = sizeof(client_addrs[n]);
        int client_sock = accept(sockfd, (struct sockaddr*) &client_addrs[n], &client_addr_len);
	    assert(setsockopt(client_sock, SOL_TCP, TCP_NODELAY, &opt_val, sizeof(opt_val)) == 0);

        evs[n].events = EPOLLIN;
        evs[n].data.fd = client_sock;
        int ec_rc = epoll_ctl(epfd, EPOLL_CTL_ADD, client_sock, &evs[n]);
        assert(ec_rc == 0);
    }

    struct epoll_event event;
    char buf[100];
    std::unordered_map<tenant_id_t, tenant_info_t> tenant_info;
    fprintf(stderr, "Before loop.\n");

    while (1) {
        int nfds = epoll_wait(epfd, &event, 1, -1);
        assert(nfds == 1);
        fprintf(stderr, "Received something!\n");

        // this way, ignore other events like hang-up?
        if (event.events != EPOLLIN) {
            continue;
        }
        
        int ready_fd = event.data.fd;
        // should not break apart a couple of bytes, hopefully.
		int received = recv(ready_fd, buf, sizeof(alloc_req_t), 0);
        printf("received: %d\n", received);
        assert(received == sizeof(alloc_req_t));

        struct alloc_req_t* req = (struct alloc_req_t*) buf;
        if (tenant_info.find(req->tenant_id) == tenant_info.end()) {
            std::pair<tenant_id_t, tenant_info_t> pr;
            pr.first = req->tenant_id;
            tenant_info.insert(pr);
        }
        auto& info = tenant_info[req->tenant_id];
        info.sock_fds.insert(ready_fd);
        info.expected_n_fds = req->tenant_num_nodes;

        if (tenant_info[req->tenant_id].cached.batch_num != req->batch_num) {
            if (req->blk_to_free != NO_BLOCK) {
                // free req->block_to_free
                handle_free(req->tenant_id, req->blk_to_free);
            }
            uint32_t alloced_blk = handle_alloc(req->tenant_id, req->start_delay_ns, req->duration_ns);
            auto& v = tenant_info[req->tenant_id].cached;
            v.batch_num = req->batch_num;
            v.alloced_blk_id = alloced_blk;
            tenant_info[req->tenant_id].n_cached_uses = 0;
        }
        // send tenant_info[req->tenant_id].
        memcpy(buf, &tenant_info[req->tenant_id].cached, sizeof(alloc_resp_t));
        tenant_info[req->tenant_id].n_cached_uses += 1;
        assert(send(ready_fd, buf, sizeof(alloc_resp_t), 0) == sizeof(alloc_resp_t));

        if (tenant_info[req->tenant_id].expected_n_fds == tenant_info[req->tenant_id].sock_fds.size()) {
            std::unordered_set<tenant_id_t>& ready_tenants = get_ready();
            for (auto it = ready_tenants.begin(); it != ready_tenants.end();) {
                tenant_id_t tenant = *it;
                auto& info = tenant_info[tenant];
                if (info.sock_fds.size() == info.expected_n_fds 
                    && info.n_cached_uses == info.expected_n_fds) {
                    printf("Notifying tenant %lu\n", tenant);
                    for (int sock_fd : info.sock_fds) {
                        assert(send(sock_fd, buf, sizeof(alloc_ready_msg_t), 0) 
                            == sizeof(alloc_ready_msg_t));
                    }
                    it = ready_tenants.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
}
