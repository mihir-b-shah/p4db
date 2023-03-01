
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

#define N_NODES 2
#define BATCH_SIZE_TGT 100000
#define N_NODE_THREADS 1
#define DB_KEY_SIZE 8
#define IN_MSG_SIZE (8*(100000+1))

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

	int node_sockfds[N_NODES];
	struct sockaddr_in client_addrs[N_NODES];
	char* bufs[N_NODES];
	for (size_t i = 0; i<N_NODES; ++i) {
		bufs[i] = (char*) malloc(IN_MSG_SIZE);
	}

	assert(bind(sockfd, (sockaddr*) &server_addr, sizeof(server_addr)) == 0);
	// just a default backlog quantity
	assert(listen(sockfd, 16) == 0);

	// TODO: obviously, must change in a multi-tenant environment.
	while (1) {
		int left[N_NODES] = {};
		char* buf_ptrs[N_NODES] = {};

		for (size_t i = 0; i<N_NODES; ++i) {
			socklen_t client_addr_len;
			int child_fd = accept(sockfd, (struct sockaddr*) &client_addrs[i], &client_addr_len);
			assert(child_fd >= 0);
			node_sockfds[i] = child_fd;
			buf_ptrs[i] = bufs[i];
			left[i] = IN_MSG_SIZE;
		}

		bool done;
		do {
			done = true;
			for (size_t i = 0; i<N_NODES; ++i) {
				if (left[i] > 0) {
					done = false;
					// TODO: maybe make this zero-copy or non-blocking?
					ssize_t sent = recv(node_sockfds[i], buf_ptrs[i], left[i], 0);
					printf("received %ld from node %lu\n", sent, i);
					buf_ptrs[i] += sent;
					left[i] -= sent;
				}
			}
		} while (!done);

		printf("received all sched info!\n");
	}
}
