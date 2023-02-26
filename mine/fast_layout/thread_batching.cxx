
#include "sim.h"

#include <cstdio>
#include <unordered_map>
#include <cassert>
#include <bitset>
#include <cstdlib>

#define N_ACTIVE_BATCHES 32

int main() {
    batch_iter_t iter = get_batch_iter(workload_e::YCSB_99_16);
    std::list<std::pair<txn_t, txn_t>> txns = iter.get_comps();
	std::unordered_map<db_key_t, std::bitset<N_ACTIVE_BATCHES>> active;
	std::vector<txn_t> stations[N_ACTIVE_BATCHES];
	size_t evict_p = 0;

	size_t txn_id = 0;
    for (auto it = txns.begin(); it != txns.end();) {
		const txn_t& cold_txn = it->second;

		std::bitset<N_ACTIVE_BATCHES> agg;
		for (size_t i = 0; i<cold_txn.ops.size(); ++i) {
			db_key_t k = cold_txn.ops[i];
			if (active.find(k) == active.end()) {
				active.emplace(k, 0);
			}
			agg |= active[k];
		}

		// find first zero bit- in reality, can be done via __ffs intrinsic.
		size_t first = 0;
		while (first < N_ACTIVE_BATCHES) {
			if (!agg[first]) {
				break;
			}
			first += 1;
		}

		if (first == N_ACTIVE_BATCHES) {
			// evict station FIFO. Maybe need a better eviction policy?
			// do not increment the iterator, try again.
			std::vector<txn_t>& batch = stations[evict_p];
			for (txn_t& txn : batch) {
				for (db_key_t op : txn.ops) {
					past_ref[op].reset(evict_p);
				}
			}
		}

		for (size_t i = 0; i<cold_txn.ops.size(); ++i) {
			if (past_ref.find(cold_txn.ops[i]) != past_ref.end()) {
				printf("%lu ", past_ref[cold_txn.ops[i]]);
				n_conflicts += 1;
			}
		}
		txn_id += 1;
		it++;
	}
    return 0;
}
