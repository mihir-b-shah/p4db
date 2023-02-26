
#include "sim.h"

#include <cstdio>
#include <unordered_map>
#include <cassert>
#include <bitset>
#include <cstdlib>
#include <algorithm>
#include <random>

static constexpr size_t LOOKAHEAD = 40;
static constexpr size_t N_THREADS = 20;

void gen_batch(std::list<std::pair<txn_t, txn_t>>& txns, std::vector<txn_t>& ret) {
    std::unordered_set<db_key_t> locks;
	while (ret.size() < MAX_BATCH && txns.size() > 0) {
		size_t p = 0;
		size_t bef_considered_size = ret.size();
		size_t considered_size = ret.size();

		for (auto it = txns.begin(); it != txns.end(); ++it) {
			if (p++ >= LOOKAHEAD) {
				break;
			}

			const txn_t& cold_txn = it->second;
			size_t n_conflicts = 0;
			for (size_t i = 0; i<cold_txn.ops.size(); ++i) {
				n_conflicts += locks.find(cold_txn.ops[i]) != locks.end();
			}
			if (n_conflicts == 0) {
				for (size_t i = 0; i<cold_txn.ops.size(); ++i) {
					locks.insert(cold_txn.ops[i]);
				}
				ret.push_back(cold_txn);
				considered_size += 1;
				txns.erase(it);
				break;
			}
		}

		
		if (bef_considered_size == considered_size) {
			break;
		}
	}
	
	if (ret.size() == 0 && txns.size() > 0) {
		gen_batch(txns, ret);
	}
}

int main() {
    batch_iter_t iter = get_batch_iter(workload_e::YCSB_99_16);
    std::list<std::pair<txn_t, txn_t>> txns = iter.get_comps();

	std::vector<txn_t> ret;
	std::vector<txn_t> new_hist;
	std::vector<size_t> starts;
	while (1) {
		gen_batch(txns, ret);
		if (ret.size() == 0) {
			assert(txns.empty());
			break;
		}
		starts.push_back(new_hist.size());
		printf("batch_size: %lu, hist_size: %lu\n", ret.size(), new_hist.size());
		new_hist.insert(new_hist.end(), ret.begin(), ret.end());
		ret.clear();
	}

	std::vector<std::pair<size_t, txn_t>> per_core_txns[N_THREADS];
	for (size_t i = 0; i<new_hist.size(); ++i) {
		auto st_it = std::upper_bound(starts.begin(), starts.end(), i);
		assert(st_it != starts.begin());
		st_it--;
		per_core_txns[i % N_THREADS].emplace_back(std::distance(starts.begin(), st_it), new_hist[i]);
	}

	/*
	auto rng = std::default_random_engine {};
	for (size_t i = 0; i<N_THREADS; ++i) {
		std::shuffle(per_core_txns[i].begin(), per_core_txns[i].end(), rng);
	}
	*/

	std::unordered_map<db_key_t, size_t> past_ref;
	for (size_t j = 0; j<new_hist.size()/N_THREADS; ++j) {
		for (size_t t = 0; t<N_THREADS; ++t) {
			const std::pair<size_t, txn_t> pr = per_core_txns[t][j];
			const txn_t& cold_txn = pr.second;

			printf("{");
			size_t n_conflicts = 0;
			for (size_t i = 0; i<cold_txn.ops.size(); ++i) {
				if (past_ref.find(cold_txn.ops[i]) != past_ref.end()) {
					if (past_ref[cold_txn.ops[i]] >= pr.first) {
						printf("%lu ", cold_txn.ops[i]);
						n_conflicts += 1;
					}
				}
			}
			printf("} | n_conflicts: %lu\n", n_conflicts);
			for (size_t i = 0; i<cold_txn.ops.size(); ++i) {
				if (past_ref.find(cold_txn.ops[i]) == past_ref.end()) {
					past_ref.emplace(cold_txn.ops[i], pr.first);
				} else {
					past_ref[cold_txn.ops[i]] = pr.first;
				}
				assert(past_ref[cold_txn.ops[i]] == pr.first);
			}
		}
	}
    return 0;
}
