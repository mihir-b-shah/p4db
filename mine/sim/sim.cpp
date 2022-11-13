
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
	system_t sys;

	// simulation
	for (size_t s = 0; s<N_STEPS; ++s) {
		step_spray_txns(sys.nodes);
		while (!sys.retry.empty() && sys.retry.front().first < s) {
			txn_wrap_t tw = sys.retry.front().second;
			sys.nodes[tw.coord].tq.push(tw);
			sys.retry.pop();
		}

		for (size_t n = 0; n<N_NODES; ++n) {
			// assume no contention-aware scheduling.
			for (size_t t = 0; t<N_THREADS; ++t) {
				// if the thread has no work, take a txn from the queue.

				if (sys.nodes[n].thrs[t].state == STG_IDLE && !sys.nodes[n].tq.empty()) {
					sys.nodes[n].thrs[t].work = sys.nodes[n].tq.front();
					sys.nodes[n].tq.pop();
					if (sys.nodes[n].thrs[t].work.coord == n) {
						// I am the coordinator
						sys.nodes[n].thrs[t].state = STG_COORD_ACQ;
					} else {
						// I am the peer
						sys.nodes[n].thrs[t].state = STG_PARTIC_ACQ;
					}
				} else {
					nthread_step(s, sys.nodes[n].thrs[t], sys);
				}
				//printf("s: %lu, n: %lu, t: %lu, state: %d\n", s, n, t, nodes[n].thrs[t].state);
			}
		}
	}

	return 0;
}
