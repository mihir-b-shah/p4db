
#include <vector>
#include <queue>
#include <cstdlib>

#include "consts.h"
#include "txn.h"
#include "node.h"

void step_spray_txns(std::vector<node_t>& nodes) {
	for (size_t i = 0; i<TXNS_PER_STEP; ++i) {
		txn_t t;
		nodes[get_coord(t)].tq.emplace(t);
	}
}

/* very simple striping of keys to nodes k%3 
	 (bad in consistent hashing, but we aren't adding nodes) */

int main() {
	std::vector<node_t> nodes;	
	nodes.reserve(N_NODES);

	for (size_t i = 0; i<N_NODES; ++i) {
		nodes.emplace_back(i);
	}

	// simulation
	for (size_t s = 0; s<N_STEPS; ++s) {
		if (s % 10 == 0) step_spray_txns(nodes);

		for (size_t n = 0; n<N_NODES; ++n) {
			// assume no contention-aware scheduling.
			for (size_t t = 0; t<N_THREADS; ++t) {
				// if the thread has no work, take a txn from the queue.

				if (nodes[n].thrs[t].state == STG_IDLE && !nodes[n].tq.empty()) {
					nodes[n].thrs[t].work = nodes[n].tq.front();
					nodes[n].tq.pop();
					if (nodes[n].thrs[t].work.coord == n) {
						// I am the coordinator
						nodes[n].thrs[t].state = STG_COORD_ACQ;
					} else {
						// I am the peer
						nodes[n].thrs[t].state = STG_PARTIC_ACQ;
					}
				} else {
					nthread_step(nodes[n].thrs[t], nodes);
				}
				printf("s: %lu, n: %lu, t: %lu, state: %d\n", s, n, t, nodes[n].thrs[t].state);
			}
		}
	}

	return 0;
}
