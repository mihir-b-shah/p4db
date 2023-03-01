
/*
This is our batching oracle. It is very efficient- needs just the sample key for each txn from
each node to schedule stuff, and can be arbitrarily multicore.

TODO This is only for single-tenant, right now.
TODO To support multiple n_thread cts, etc. could add a header to the 1MB messages.
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <errno.h>

#define N_NODES 1
#define BATCH_SIZE_TGT 100000
#define N_NODE_THREADS 20
#define DB_KEY_SIZE 8
#define IN_MSG_SIZE (8*(100000+20))

// XXX: copied from src/ee/sched_intf.c
static void sendall(int sockfd, char* buf, int len) {
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

static void recvall(int sockfd, char* buf, int len) {
	// TODO: zero-copy recv is possible, try if needed.
	while (len > 0) {
		ssize_t sent = recv(sockfd, buf, len, 0);
		buf += sent;
		len -= sent;
	}
}

int main() {
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	assert(sockfd >= 0);
	
	int opt_val = 1;
	assert(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val)) == 0);

	static struct sockaddr_in server_addr; 
	memset(&server_addr, 0, sizeof(server_addr));

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons((unsigned short) 4001);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	char* serveraddr_bytes = (char*) &server_addr;
	for (size_t i = 0; i<sizeof(server_addr); ++i) {
		printf("%d ", serveraddr_bytes[i]);
	}
	printf("\n");
	
	int node_sockfds[N_NODES];
	struct sockaddr_in client_addrs[N_NODES];
	char* bufs[N_NODES];
	for (size_t i = 0; i<N_NODES; ++i) {
		bufs[i] = (char*) malloc(IN_MSG_SIZE);
	}

	assert(bind(sockfd, (sockaddr*) &server_addr, sizeof(server_addr)) == 0);
	printf("Listening on ip: %s, port: %d\n", inet_ntoa(server_addr.sin_addr), server_addr.sin_port);
	printf("Bind succeeded.\n");
	// just a default backlog quantity
	assert(listen(sockfd, 16) == 0);
	printf("Listen succeeded.\n");

	// TODO: obviously, must change in a multi-tenant environment.
	while (1) {
		for (size_t i = 0; i<N_NODES; ++i) {
			socklen_t client_addr_len;
			printf("Reached accept.\n");
			int child_fd = accept(sockfd, (struct sockaddr*) &client_addrs[i], &client_addr_len);
			assert(child_fd >= 0);
			node_sockfds[i] = child_fd;

			recvall(child_fd, bufs[i], IN_MSG_SIZE);
		}
		exit(0);
	}
}
