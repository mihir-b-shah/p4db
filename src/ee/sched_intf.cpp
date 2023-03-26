
#include "ee/executor.hpp"
#include <main/config.hpp>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <cstring>
#include <errno.h>

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

void Database::setup_sched_sock() {	
	static struct sockaddr_in server_addr; 

	auto& config = Config::instance();
	int server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	int opt_val = 1;
	assert(setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val)) == 0);

	memset(&server_addr, 0, sizeof(server_addr));

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons((unsigned short) config.sched_server.port);
	std::string& sched_ip = config.sched_server.ip;
	inet_aton((const char*) sched_ip.c_str(), &server_addr.sin_addr);

	assert(connect(server_sockfd, (struct sockaddr*) &server_addr, (socklen_t) sizeof(struct sockaddr_in)) == 0);
	this->sched_sockfd = server_sockfd;
}

void Database::update_alloc(uint32_t batch_num) {
    static Config& conf = Config::instance();

    struct alloc_req_t req;
    req.start_delay_ns = COLD_BATCH_DUR_EST_NS;
    req.duration_ns = HOT_BATCH_DUR_EST_NS;
    // if no block, this is NO_BLOCK, which is what the scheduler expects.
    req.blk_to_free = conf.decl_layout->block_num;
    // scheduler expects 1-indexed.
    req.tenant_id = 1+conf.tenant_id;
    req.tenant_num_nodes = conf.num_nodes;
    req.batch_num = 1+batch_num;

    // fprintf(stderr, "Line %d, req.batch_num: %u\n", __LINE__, req.batch_num);
	assert(send(sched_sockfd, (char*) &req, sizeof(struct alloc_req_t), 0) == sizeof(struct alloc_req_t));
    // fprintf(stderr, "Line %d\n", __LINE__);

    struct alloc_resp_t fill;
    assert(recv(sched_sockfd, (char*) &fill, sizeof(struct alloc_resp_t), 0) 
        == sizeof(struct alloc_resp_t));
    // fprintf(stderr, "Line %d\n", __LINE__);
    assert(fill.batch_num == req.batch_num); 

    // no one is reading the layout while we do this.
    conf.decl_layout->block_num = fill.alloced_blk_id;
    // fprintf(stderr, "Line %d\n", __LINE__);
}

void Database::wait_sched_ready() {
    char buf[sizeof(struct alloc_ready_msg_t)];
    assert(recv(sched_sockfd, &buf[0], sizeof(struct alloc_ready_msg_t), 0) == sizeof(struct alloc_ready_msg_t));
}
