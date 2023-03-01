
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
