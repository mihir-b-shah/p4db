
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
#include <cstdint>
#include <errno.h>

#include <unordered_map>
#include <algorithm>
#include <queue>
#include <vector>

typedef uint32_t txn_pos_t;
struct __attribute__((packed)) out_sched_entry_t {
	txn_pos_t idx : 24;
	uint8_t thr_id : 8;
};
static_assert(sizeof(out_sched_entry_t) == sizeof(uint32_t));

typedef uint64_t db_key_t; 
struct __attribute__((packed)) in_sched_entry_t {
	db_key_t k : 40;
	uint32_t idx : 24;
};
static_assert(sizeof(in_sched_entry_t) == sizeof(uint64_t));

static size_t hash_key(db_key_t x) {
	return x;
}

static constexpr size_t N_NODES = 2;
static constexpr size_t BATCH_SIZE_TGT = 100000;
static constexpr size_t N_NODE_THREADS = 1;
static constexpr size_t IN_MSG_SIZE = sizeof(in_sched_entry_t)*(BATCH_SIZE_TGT/N_NODE_THREADS+1);
static constexpr size_t OUT_MSG_SIZE = sizeof(out_sched_entry_t)*(BATCH_SIZE_TGT/N_NODE_THREADS+1);
static constexpr uint64_t SENTINEL_KEY = 0xffffffffffULL;
static constexpr uint32_t SENTINEL_POS = 0xffffffULL;
static constexpr size_t MINI_BATCH_TGT_SIZE = 500;

// XXX: copied from src/ee/sched_intf.
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

struct sched_pkt_hdr_t {
	size_t node_id;
	size_t thread_id;
};

struct bucket_txn_id_t {
	size_t node_id;
	size_t thread_id;
	size_t txn_id;

	bucket_txn_id_t(size_t n, size_t t, size_t tx) : node_id(n), thread_id(t), txn_id(tx) {}
};

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

	int node_sockfds[N_NODES][N_NODE_THREADS];
	struct sockaddr_in client_addrs[N_NODES][N_NODE_THREADS];
	in_sched_entry_t* bufs[N_NODES][N_NODE_THREADS];
	out_sched_entry_t* out_bufs[N_NODES][N_NODE_THREADS];

	for (size_t n = 0; n<N_NODES; ++n) {
		for (size_t t = 0; t<N_NODE_THREADS; ++t) {
			bufs[n][t] = (in_sched_entry_t*) malloc(IN_MSG_SIZE);
			out_bufs[n][t] = (out_sched_entry_t*) malloc(OUT_MSG_SIZE);
		}
	}
	printf("Reached 104. sockfd=%d, server_addr: %p\n", sockfd, &server_addr);
	int bind_rc = bind(sockfd, (sockaddr*) &server_addr, sizeof(server_addr));
	printf("rc: %d, err: %s\n", bind_rc, strerror(errno));
	assert(bind_rc == 0);

	// just a reasonable backlog quantity
	assert(listen(sockfd, N_NODES*N_NODE_THREADS+5) == 0);

	// TODO: obviously, must change in a multi-tenant environment.
	while (1) {
		int left[N_NODES][N_NODE_THREADS] = {{}};
		char* buf_ptrs[N_NODES][N_NODE_THREADS] = {{}};
		int out_left[N_NODES][N_NODE_THREADS] = {{}};
		char* out_buf_ptrs[N_NODES][N_NODE_THREADS] = {{}};
		size_t out_buf_write_p[N_NODES][N_NODE_THREADS] = {{}};

		for (size_t t = 0; t<N_NODE_THREADS; ++t) {
			for (size_t n = 0; n<N_NODES; ++n) {
				socklen_t client_addr_len = sizeof(client_addrs[n][t]);
				int child_fd = accept(sockfd, (struct sockaddr*) &client_addrs[n][t], &client_addr_len);
				printf("sockfd: %d, child_fd: %d, t: %lu, n: %lu, err: %s\n", sockfd, child_fd, t, n, strerror(errno));
				assert(child_fd >= 0);

				char hdr_buf[sizeof(sched_pkt_hdr_t)];
				// TODO: too lazy to implement recvall here... ill fix it if it breaks...
				printf("recv %lu bytes from fd %d\n", sizeof(sched_pkt_hdr_t), child_fd);
				assert(recv(child_fd, hdr_buf, sizeof(sched_pkt_hdr_t), 0) == sizeof(sched_pkt_hdr_t));

				node_sockfds[n][t] = child_fd;
				buf_ptrs[n][t] = (char*) bufs[n][t];
				left[n][t] = IN_MSG_SIZE;
				out_buf_ptrs[n][t] = (char*) out_bufs[n][t];
				out_left[n][t] = OUT_MSG_SIZE;
				out_buf_write_p[n][t] = 0;
			}
		}

		bool done;
		do {
			done = true;
			for (size_t t = 0; t<N_NODE_THREADS; ++t) {
				for (size_t n = 0; n<N_NODES; ++n) {
					if (left[t][n] > 0) {
						done = false;
						// TODO: maybe make this zero-copy or non-blocking?
						ssize_t received = recv(node_sockfds[n][t], buf_ptrs[n][t], left[n][t], 0);
						printf("received %ld from node %lu, thread %lu\n", received, n, t);
						buf_ptrs[n][t] += received;
						left[n][t] -= received;
					}
				}
			}
		} while (!done);

		printf("received all sched info!\n");

		/*	XXX: the multicore non-distributed version of this was written/committed around 2/26/2023,
			in src/ee/database.{hpp,cpp}, for reference */
		std::unordered_map<db_key_t, size_t> bucket_map;
		// the pair is (node_id, txn id)
		std::vector<std::vector<bucket_txn_id_t>> buckets;
		buckets.reserve(N_NODES*BATCH_SIZE_TGT);
		std::vector<size_t> pq_vec;
		pq_vec.reserve(N_NODES*BATCH_SIZE_TGT);
		bool node_thr_done[N_NODES][N_NODE_THREADS] = {{}};

		for (size_t i = 0; i<IN_MSG_SIZE/sizeof(in_sched_entry_t); ++i) {
			for (size_t t = 0; t<N_NODE_THREADS; ++t) {
				for (size_t n = 0; n<N_NODES; ++n) {
					if (node_thr_done[n][t]) {
						continue;
					}

					in_sched_entry_t entry = bufs[n][t][i];
					db_key_t key_to_hash = entry.k;
					if (key_to_hash == SENTINEL_KEY) {
						node_thr_done[n][t] = true;
						continue;
					}
					assert(key_to_hash < 10000000);

					auto it = bucket_map.find(key_to_hash);
					size_t bucket_pos;
					if (it == bucket_map.end()) {
						bucket_pos = buckets.size();
						buckets.emplace_back();
						buckets.back().reserve(8);
						bucket_map.emplace(key_to_hash, bucket_pos);
						pq_vec.push_back(bucket_pos);
					} else {
						bucket_pos = it->second;
					}
					assert(bucket_pos >= 0 && bucket_pos < buckets.size());
					buckets[bucket_pos].emplace_back(n, t, static_cast<uint32_t>(entry.idx));
				}
			}
		}

		auto bucket_cmp_func = [&buckets](const size_t p1, const size_t p2){
			if (p1 >= buckets.size() || p2 >= buckets.size()) {
				printf("p1: %lu, p2: %lu\n", p1, p2);
				assert(false);
			}
			return buckets[p1].size() < buckets[p2].size();
		};
		std::make_heap(pq_vec.begin(), pq_vec.end(), bucket_cmp_func);
		printf("rat!\n");
		// round-robin thread assignment.
		size_t node_thr_rr[N_NODES] = {};

		// TODO: this could be multi-threaded, not for now.
		std::vector<size_t> buckets_skip;
		while (pq_vec.size() > 0) {
			assert(buckets_skip.size() == 0);
			while (pq_vec.size() > 0 && buckets_skip.size() < MINI_BATCH_TGT_SIZE) {
				size_t b_top = pq_vec[0];
				auto& v = buckets[b_top];
				if (v.size() == 0) {
					std::pop_heap(pq_vec.begin(), pq_vec.end(), bucket_cmp_func);
					pq_vec.pop_back();
					continue;
				}
				// per-core output.
				bucket_txn_id_t& txn_id = v.back();
				// we can re-assign among threads, not among nodes.
				size_t thr_assign = node_thr_rr[txn_id.node_id];
				node_thr_rr[txn_id.node_id] = (node_thr_rr[txn_id.node_id]+1) % N_NODE_THREADS;

				size_t out_bufs_idx = out_buf_write_p[txn_id.node_id][thr_assign]++;
				assert(txn_id.node_id < N_NODES && thr_assign < N_NODE_THREADS && out_bufs_idx < OUT_MSG_SIZE/sizeof(out_sched_entry_t));
				auto& buf_e = out_bufs[txn_id.node_id][thr_assign][out_bufs_idx];
				buf_e.idx = txn_id.txn_id;
				buf_e.thr_id = txn_id.thread_id;
				fprintf(stderr, "n: %lu, t: %lu, i: %lu | idx: %u, thr: %u\n", txn_id.node_id, thr_assign, out_bufs_idx, buf_e.idx, buf_e.thr_id);
				v.pop_back();

				std::pop_heap(pq_vec.begin(), pq_vec.end(), bucket_cmp_func);
				pq_vec.pop_back();
				buckets_skip.push_back(b_top);
			}
			// printf("pq_vec.size(): %lu, buckets_skip.size(): %lu\n", pq_vec.size(), buckets_skip.size());
			while (buckets_skip.size() > 0) {
				pq_vec.push_back(buckets_skip.back());
				std::push_heap(pq_vec.begin(), pq_vec.end(), bucket_cmp_func);
				buckets_skip.pop_back();
			}
		}
		
		/*	send revised schedules out.
			TODO: notice that schedules freely re-organize among threads' submitted txns. This is
			not a problem, just read from other threads' original txn queues. Alternative would be
			for nodes to re-organize before sending (also makes multi-core processing near-trivial).
			This however requires locks on building packets, etc and is complicated. */
		bool first_it = true;
		do {
			done = true;
			for (size_t t = 0; t<N_NODE_THREADS; ++t) {
				for (size_t n = 0; n<N_NODES; ++n) {
					if (first_it) {
						out_bufs[n][t][out_buf_write_p[n][t]].idx = SENTINEL_POS;
					}
					if (out_left[t][n] > 0) {
						done = false;
						// TODO: maybe make this zero-copy or non-blocking?
						ssize_t sent = send(node_sockfds[n][t], out_buf_ptrs[n][t], out_left[n][t], 0);
						printf("sent %ld to node %lu, thread %lu\n", sent, n, t);
						out_buf_ptrs[n][t] += sent;
						out_left[n][t] -= sent;
					}
				}
			}
			first_it = false;
		} while (!done);
	}
}
