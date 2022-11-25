
#include <vector>
#include <queue>
#include <cstdlib>
#include <cmath>
#include <cassert>

#include "consts.h"
#include "txn.h"
#include "node.h"

void step_spray_txns(system_t& sys) {
	for (size_t i = 0; i<TXNS_PER_STEP; ++i) {
		txn_t t;
		if (sys.nodes[get_coord(t)].tq.size() < MAX_QUEUE_SIZE) {
			sys.nodes[get_coord(t)].tq.emplace_back(t);
		} else {
			sys.dropped += 1;
		}
	}
}

/* very simple striping of keys to nodes k%3 
	 (bad in consistent hashing, but we aren't adding nodes) */

int main() {
	system_t sys;

	// simulation
	for (size_t s = 0; s<N_STEPS; ++s) {
		step_spray_txns(sys);
		while (!sys.retry.empty() && sys.retry.front().first < s) {
			txn_wrap_t tw = sys.retry.front().second;
			sys.nodes[tw.coord].tq.push_front(tw);
			sys.retry.pop();
		}

		for (size_t n = 0; n<N_NODES; ++n) {
			// assume no contention-aware scheduling.
			for (size_t t = 0; t<N_THREADS; ++t) {
				// if the thread has no work, take a txn from the queue.

				if (sys.nodes[n].thrs[t].state == STG_IDLE && !sys.nodes[n].tq.empty()) {
					sys.nodes[n].thrs[t].work = sys.nodes[n].tq.front();
					sys.nodes[n].tq.pop_front();
					if (sys.nodes[n].thrs[t].work.coord == n) {
						// I am the coordinator
						sys.nodes[n].thrs[t].state = STG_COORD_ACQ;
					} else {
						// I am the peer
						sys.nodes[n].thrs[t].state = STG_PARTIC_ACQ;
					}
				} else {
					if (sys.nodes[n].thrs[t].state == STG_IDLE) {
						sys.idle_time += 1;
					}
					nthread_step(s, sys.nodes[n].thrs[t], sys);
				}
				//printf("s: %lu, n: %lu, t: %lu, state: %d\n", s, n, t, nodes[n].thrs[t].state);
			}
		}
	}

	size_t ttl_queue_size = 0;
	for (size_t i = 0; i<N_NODES; ++i) {
		ttl_queue_size += sys.nodes[i].tq.size();
	}

	printf("Txns committed in %lu steps: %lu\n", N_STEPS, sys.committed);
	printf("Txns aborted in %lu steps: %lu\n", N_STEPS, sys.aborted.size());
	// good flow control would avoid drops
	// printf("Txns dropped in %lu steps: %lu\n", N_STEPS, sys.dropped);
	printf("Txn queue sum: %lu\n", sys.retry.size() + ttl_queue_size);
	printf("%% of time idle: %f\n", (double) sys.idle_time / (N_STEPS * N_NODES * N_THREADS));
	printf("%% of txns committed: %f\n", (double) sys.committed/(sys.committed + sys.aborted.size()));

	double m0 = 0;
	double m1 = 0;
	double m2 = 0;
	for (size_t diff : sys.tid_diffs) {
		m0 += 1;
		m1 += diff;
		m2 += diff*diff;
	}
	printf("Txn tid diff, mean: %.3f, sd: %.3f\n", m1/m0, sqrt(m2/m0 - (m1/m0)*(m1/m0)));
	printf("Txn throughput: %lu txns/step\n", sys.committed/N_STEPS);

	return 0;
}
