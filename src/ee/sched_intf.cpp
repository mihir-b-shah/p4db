
#include "ee/executor.hpp"
#include <main/config.hpp>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <cstring>
#include <errno.h>

int setup_txn_sched_sock() {	
	static struct sockaddr_in server_addr; 

	auto& config = Config::instance();

	int server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	memset(&server_addr, 0, sizeof(server_addr));

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons((unsigned short) config.sched_server.port);
	std::string& sched_ip = config.sched_server.ip;
	inet_aton((const char*) sched_ip.c_str(), &server_addr.sin_addr);

	// TODO: what is the necessary lifetime of the server_addr object?
	assert(connect(server_sockfd, (struct sockaddr*) &server_addr, (socklen_t) sizeof(struct sockaddr_in)) == 0);

	// TODO: https://stackoverflow.com/questions/45031093/can-we-use-zero-copy-for-tcp-send-recv-with-the-default-linux-tcp-ip-stack allows zero-copy TCP send/recv. If this is an overhead, let's do it.
	// int opt_value = 1;
	// assert(setsockopt(server_sockfd, SOL_SOCKET, SO_ZEROCOPY, &opt_value, sizeof(opt_value)));
	return server_sockfd;
}

void sendall(int sockfd, char* buf, int len) {
	printf("Trying to send %d bytes.\n", len);
	/*	TODO: zero-copy send is possible, try if needed.
		Although, maybe it's a bad idea. zero-copy will probably engage the send for longer, since 
		my buf has to be valid for some amt of time. In contrast, non-zero-copy + non-blocking can allow
		spending less time interacting with the syscall. */
	while (len > 0) {
		ssize_t sent = send(sockfd, buf, len, 0);
		buf += sent;
		len -= sent;
	}
}

void recvall(int sockfd, char* buf, int len) {
	// TODO: zero-copy recv is possible, try if needed.
	while (len > 0) {
		ssize_t sent = recv(sockfd, buf, len, 0);
		buf += sent;
		len -= sent;
	}
}

void TxnExecutor::setup_txn_sched() {
	assert(BATCH_SIZE_TGT % db.n_threads == 0);
	size_t node_id = Config::instance().node_id;
	sched_packet_buf_len = sizeof(sched_pkt_hdr_t) + sizeof(out_sched_entry_t)*(BATCH_SIZE_TGT/db.n_threads+1);
	// TODO: can I get away with 2-byte ids in the reply instead?
	sched_reply_len = sizeof(uint32_t)*(BATCH_SIZE_TGT/db.n_threads+1);

	raw_buf = (char*) malloc(sched_packet_buf_len);
	assert(sched_reply_len <= sched_packet_buf_len);

	sched_pkt_hdr_t* hdr = (sched_pkt_hdr_t*) raw_buf;
	hdr->node_id = node_id;
	hdr->thread_id = tid;
	sched_packet_buf = (out_sched_entry_t*) (sizeof(sched_pkt_hdr_t) + raw_buf);
	txn_sched_sockfd = setup_txn_sched_sock();
}

void TxnExecutor::send_get_txn_sched() {
	uint64_t* buf_view = (uint64_t*) raw_buf;
	sendall(txn_sched_sockfd, raw_buf, sched_packet_buf_len);
	db.txn_sched_bar.wait(NULL, true);
	recvall(txn_sched_sockfd, raw_buf, sched_reply_len);
}
