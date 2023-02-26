
#include "sim.h"

#include <cstdio>
#include <unordered_map>
#include <cassert>
#include <bitset>
#include <cstdlib>

int main() {
    batch_iter_t iter = get_batch_iter(workload_e::YCSB_99_8);
    std::list<std::pair<txn_t, txn_t>> txns = iter.get_comps();
	std::unordered_map<db_key_t, size_t> freqs;

    for (auto it = txns.begin(); it != txns.end(); ++it) {
		const txn_t& cold_txn = it->second;
		for (size_t i = 0; i<cold_txn.ops.size(); ++i) {
			db_key_t k = cold_txn.ops[i];
			if (freqs.find(k) == freqs.end()) {
				freqs.emplace(k, 0);
			}
			freqs[k] += 1;
		}
	}

	std::unordered_map<db_key_t, size_t> past_ref;
	std::unordered_set<size_t> refd;

	size_t txn_id = 0;
    for (auto it = txns.begin(); it != txns.end(); ++it) {
		const txn_t& cold_txn = it->second;

		printf("{");
		size_t n_conflicts = 0;
		for (size_t i = 0; i<cold_txn.ops.size(); ++i) {
			if (past_ref.find(cold_txn.ops[i]) != past_ref.end()) {
				refd.insert(past_ref.find(cold_txn.ops[i])->second);
				printf("(k:%lu,f:%lu) ", cold_txn.ops[i], freqs[cold_txn.ops[i]]);
				n_conflicts += 1;
			}
		}
		printf("} | n_conflicts: %lu\n", n_conflicts);
		for (size_t i = 0; i<cold_txn.ops.size(); ++i) {
			if (past_ref.find(cold_txn.ops[i]) == past_ref.end()) {
				past_ref.emplace(cold_txn.ops[i], txn_id);
			} else {
				past_ref[cold_txn.ops[i]] = txn_id;
			}
			assert(past_ref[cold_txn.ops[i]] == txn_id);
		}
		txn_id += 1;
	}
	printf("refd_size: %lu\n", refd.size());
    return 0;
}
