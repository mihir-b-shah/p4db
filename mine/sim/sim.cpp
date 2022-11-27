
#include <vector>
#include <queue>
#include <utility>
#include <cstdlib>
#include <cmath>
#include <cassert>

#include "consts.h"
#include "txn.h"
#include "node.h"

void step_spray_txns(system_t& sys, size_t s) {
	for (size_t i = 0; i<TXNS_PER_STEP; ++i) {
		txn_t t;
		if (sys.nodes[get_coord(t)].tq.size() < MAX_QUEUE_SIZE) {
			sys.nodes[get_coord(t)].tq.emplace_back(t, s);
		} else {
			sys.dropped += 1;
		}
	}
}

template <typename T>
std::pair<double, double> get_stats(const std::vector<T>& v)
{
	double m0 = 0;
	double m1 = 0;
	double m2 = 0;
	for (const T& diff : v) {
		m0 += 1;
		m1 += diff;
		m2 += diff*diff;
	}
	return {m1/m0, sqrt(m2/m0 - (m1/m0)*(m1/m0))};
}

/* very simple striping of keys to nodes k%3 
	 (bad in consistent hashing, but we aren't adding nodes) */
int main() {
	system_t sys;

	// simulation
	for (size_t s = 0; s<N_STEPS; ++s) {
		step_spray_txns(sys, s);
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
						sys.started += 1;
						sys.nodes[n].thrs[t].state = STG_COORD_ACQ;
					} else {
						// I am the peer
						sys.nodes[n].thrs[t].state = STG_PARTIC_ACQ;
					}
				}
				
				sys.stage_cts[sys.nodes[n].thrs[t].state] += 1;
				nthread_step(s, sys.nodes[n].thrs[t], sys);
			}
		}
	}

	size_t ttl_queue_size = 0;
	for (size_t i = 0; i<N_NODES; ++i) {
		ttl_queue_size += sys.nodes[i].tq.size();
	}

	printf("Txns dropped (flow control) in %lu steps: %lu\n", N_STEPS, sys.dropped);
	printf("Txn queue sum: %lu\n", sys.retry.size() + ttl_queue_size);
	printf("Txns commit/abort prop: %.3f%%\n", (double) 100*sys.committed/(sys.committed + sys.aborted.size()));
	printf("Txns commit/started prop: %.3f%%\n", (double) 100*sys.committed/sys.started);

	std::pair<double, double> tid_diff = get_stats(sys.tid_diffs);
	printf("Txn tid diff, mean: %.3f, sd: %.3f\n", tid_diff.first, tid_diff.second);
	
	std::pair<double, double> step_diff = get_stats(sys.step_diffs);
	printf("Txn step diff, mean: %.3f, sd: %.3f\n", step_diff.first, step_diff.second);

	// When there is no contention, is emblematic of high queueing delays.
	printf("Txn throughput: %.3f txns/step. Tgt throughput: ~%.3f txns/step\n", (double) sys.committed/N_STEPS, (double) (N_NODES*N_THREADS)/((3*COORD_DELAY+2*(TXN_SIZE-1)*PARTIC_DELAY)));

	printf("Time spent in stage XXX:\n");
	for (size_t i = 0; i<STG_SENTINEL; ++i) {
		printf("\t%s: %.3f%%\n", stage_strs[i], (double) 100*sys.stage_cts[i] / (N_STEPS * N_NODES * N_THREADS));
	}
	return 0;
}
