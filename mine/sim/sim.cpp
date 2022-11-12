
#include <vector>
#include <queue>
#include <cstdlib>

#include "consts.h"
#include "txn.h"
#include "node.h"

void step_spray_txns(std::vector<node_t>& nodes) {
	for (size_t i = 0; i<TXNS_PER_STEP; ++i) {
		// insert a random txn (zero-args constructor for txn)
		nodes[get_coord(txn)].tq.emplace();
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
		step_spray_txns(nodes);

		for (size_t n = 0; n<N_NODES; ++n) {
			// assume no contention-aware scheduling.
			for (size_t t = 0; t<N_THREADS; ++t) {
				// if the thread has no work, take a txn from the queue.
				if (!nodes[n].thrs[t].busy) {
					nodes[n].thrs[t].work = nodes[n].tq.front();
					nodes[n].tq.pop();
					nodes[n].thrs[t].busy = true;
				}
			}
		}
	}

	return 0;
}
