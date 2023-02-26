
#include "sim.h"

#include <cstdio>
#include <unordered_map>
#include <cassert>
#include <bitset>
#include <cstdlib>
#include <algorithm>
#include <random>
#include <queue>
#include <vector>

static constexpr size_t LOOKAHEAD = 4;
static constexpr size_t N_THREADS = 20;

void gen_batch(std::list<std::pair<txn_t, txn_t>>& txns,
			std::unordered_map<db_key_t, size_t>& key_cts,
			std::vector<txn_t>& ret) {

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
			printf("Considered-All\n");
			if ((cold_txn.ops.size() >= 1 && locks.find(cold_txn.ops[0]) != locks.end()) ||
				(cold_txn.ops.size() >= 2 && locks.find(cold_txn.ops[1]) != locks.end())) {
				continue;
			}
			printf("Considered-Real\n");

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
			} else {
				/*
				// Let's see, what did the conflict occur on?
				printf("n_conflicts: %lu\n", n_conflicts);
				size_t max_idx = 0;
				for (size_t i = 0; i<cold_txn.ops.size(); ++i) {
					if (key_cts[cold_txn.ops[max_idx]] < key_cts[cold_txn.ops[i]]) {
						max_idx = i;
					}
				}
				for (size_t i = 0; i<cold_txn.ops.size(); ++i) {
					if (locks.find(cold_txn.ops[i]) != locks.end()) {
						if (max_idx == i) {
							printf("Conflicted-max.\n");
						} else {
							printf("Conflicted-non-max.\n");
						}
					}
				}
				*/
			}
		}

		
		if (bef_considered_size == considered_size) {
			break;
		}
	}
	
	if (ret.size() == 0 && txns.size() > 0) {
		gen_batch(txns, key_cts, ret);
	}
}

int main() {
    batch_iter_t iter = get_batch_iter(workload_e::YCSB_99_8);
    std::list<std::pair<txn_t, txn_t>> txns = iter.get_comps();
	std::unordered_map<db_key_t, size_t> key_cts;
	for (const auto& pr : txns) {
		for (db_key_t op : pr.second.ops) {
			if (key_cts.find(op) == key_cts.end()) {
				key_cts.emplace(op, 0);
			}
			key_cts[op] += 1;
		}
	}
	for (auto& pr : txns) {
		txn_t& cold_txn = pr.second;
		std::sort(pr.second.ops.begin(), pr.second.ops.end(), [&key_cts](db_key_t k1, db_key_t k2){
			return key_cts[k1] > key_cts[k2];
		});
		size_t max_idx = 0;
		for (size_t i = 0; i<cold_txn.ops.size(); ++i) {
			if (key_cts[cold_txn.ops[max_idx]] < key_cts[cold_txn.ops[i]]) {
				max_idx = i;
			}
		}
		assert(max_idx == 0);
	}

	std::unordered_map<db_key_t, std::queue<std::pair<txn_t, txn_t>>> buckets;
	for (const auto& pr : txns) {
		const txn_t& cold_txn = pr.second;
		if (cold_txn.ops.size() == 0) {
			// just put it anywhere, doesn't matter.
			buckets[0].push(pr);
			continue;
		}
		if (buckets.find(cold_txn.ops[0]) == buckets.end()) {
			std::queue<std::pair<txn_t, txn_t>> q;
			buckets.insert({cold_txn.ops[0], q});
		}
		buckets[cold_txn.ops[0]].push(pr);
	}

	auto bucket_cmp = [&buckets](const db_key_t k1, const db_key_t k2){
		return buckets[k1].size() < buckets[k2].size();
	};
	std::priority_queue<db_key_t, std::vector<db_key_t>, decltype(bucket_cmp)> pq(bucket_cmp);
	for (const auto& kv : buckets) {
		pq.push(kv.first);
	}

	std::queue<db_key_t> add_back;
	std::list<std::pair<txn_t, txn_t>> bucket_output;
	while (bucket_output.size() < txns.size()) {
		//printf("bucket_output.size(): %lu\n", bucket_output.size());
		size_t added = 0;
		while (add_back.size() > 0) {
			pq.push(add_back.front());
			add_back.pop();
		}
		while (pq.size() > 0 && added < 400) {
			auto& q = buckets[pq.top()];
			if (q.size() == 0) {
				pq.pop();
				continue;
			}
			bucket_output.push_back(q.front());
			db_key_t k = pq.top();
			pq.pop();
			q.pop();
			add_back.push(k);
			added += 1;
		}
		printf("added: %lu\n", added);

		/*
		// Old approach:
		for (auto& key_bucket : buckets) {
			if (key_bucket.second.size() > 0) {
				bucket_output.push_back(key_bucket.second.front());
				key_bucket.second.pop();
			}
		}
		*/
	}
	/*
	txns = bucket_output;

	std::vector<txn_t> ret;
	std::vector<txn_t> new_hist;
	std::vector<size_t> starts;
	while (1) {
		gen_batch(txns, key_cts, ret);
		if (ret.size() == 0) {
			assert(txns.empty());
			break;
		}
		starts.push_back(new_hist.size());
		printf("batch_size: %lu, hist_size: %lu\n", ret.size(), new_hist.size());
		new_hist.insert(new_hist.end(), ret.begin(), ret.end());
		ret.clear();
	}
	*/

	/*
	std::vector<std::pair<size_t, txn_t>> per_core_txns[N_THREADS];
	for (size_t i = 0; i<new_hist.size(); ++i) {
		auto st_it = std::upper_bound(starts.begin(), starts.end(), i);
		assert(st_it != starts.begin());
		st_it--;
		per_core_txns[i % N_THREADS].emplace_back(std::distance(starts.begin(), st_it), new_hist[i]);
	}

	auto rng = std::default_random_engine {};
	for (size_t i = 0; i<N_THREADS; ++i) {
		std::shuffle(per_core_txns[i].begin(), per_core_txns[i].end(), rng);
	}

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
	*/
    return 0;
}
