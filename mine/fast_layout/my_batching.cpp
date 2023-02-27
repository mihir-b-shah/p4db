
#include "sim.h"

#include <cstdio>
#include <unordered_map>
#include <cassert>
#include <bitset>
#include <cstdlib>
#include <algorithm>
#include <random>
#include <queue>
#include <stack>
#include <vector>

static constexpr size_t N_THREADS = 20;
static constexpr size_t BATCH_TGT = 500;
static constexpr size_t N_HASH_SLOTS = 8192; // TODO: try smaller

static size_t hash_key(db_key_t x) {
	return x;
	/*
	assert((uint64_t) x < (1ULL << 32));
	x = ((x >> 16) ^ x) * 0x45d9f3b;
	x = ((x >> 16) ^ x) * 0x45d9f3b;
	x = (x >> 16) ^ x;
	return x;
	*/
}

int main() {
    batch_iter_t iter = get_batch_iter(workload_e::YCSB_99_8);
    std::list<std::pair<txn_t, txn_t>> txns = iter.get_comps();
	size_t n_txns = txns.size();
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

	size_t bucket_hash_cts[N_THREADS] = {};
	std::unordered_map<db_key_t, std::stack<std::pair<txn_t, txn_t>>> buckets;
	for (const auto& pr : txns) {
		const txn_t& cold_txn = pr.second;
		if (cold_txn.ops.size() == 0) {
			// just put it anywhere, doesn't matter.
			buckets[0].push(pr);
			continue;
		}
		if (buckets.find(cold_txn.ops[0]) == buckets.end()) {
			std::stack<std::pair<txn_t, txn_t>> q;
			buckets.insert({cold_txn.ops[0], q});
		}
		buckets[cold_txn.ops[0]].push(pr);
		bucket_hash_cts[hash_key(cold_txn.ops[0]) % N_THREADS] += key_cts[cold_txn.ops[0]];
	}

	for (size_t i = 0; i<N_THREADS; ++i) {
		printf("thread i: %lu, ct: %lu\n", i, bucket_hash_cts[i]);
	}

	auto bucket_cmp = [&buckets](const db_key_t k1, const db_key_t k2){
		return buckets[k1].size() < buckets[k2].size();
	};
	
	std::vector<std::priority_queue<db_key_t, std::vector<db_key_t>, decltype(bucket_cmp)>> pqs;
	std::vector<std::stack<db_key_t>> add_back_vs(N_THREADS);
	std::vector<std::queue<txn_t>> bucket_output_vs(N_THREADS);

	for (size_t i = 0; i<N_THREADS; ++i) {
		std::priority_queue<db_key_t, std::vector<db_key_t>, decltype(bucket_cmp)> pq(bucket_cmp);
		pqs.push_back(pq);
	}
	for (const auto& kv : buckets) {
		pqs[hash_key(kv.first) % N_THREADS].push(kv.first);
	}

	for (size_t t = 0; t<N_THREADS; ++t) {
		while (pqs[t].size() > 0) {
			//printf("bucket_output.size(): %lu\n", bucket_output.size());
			size_t added = 0;
			while (pqs[t].size() > 0 && added < BATCH_TGT/N_THREADS) {
				auto& q = buckets[pqs[t].top()];
				if (q.size() == 0) {
					pqs[t].pop();
					continue;
				}
				bucket_output_vs[t].push(q.top().second);
				db_key_t k = pqs[t].top();
				pqs[t].pop();
				q.pop();
				add_back_vs[t].push(k);
				added += 1;
			}
			while (add_back_vs[t].size() > 0) {
				pqs[t].push(add_back_vs[t].top());
				add_back_vs[t].pop();
			}
		}
	}

	for (size_t t = 0; t<N_THREADS; ++t) {
		printf("thread %lu, output len: %lu\n", t, bucket_output_vs[t].size());
	}

	// execute
	std::unordered_set<db_key_t> locks;
	std::bitset<N_HASH_SLOTS> lock_bset;
	size_t total_quick_considered = 0;
	size_t total_considered = 0;
	size_t total_executed = 0;
	while (total_executed < n_txns) {
		locks.clear();
		lock_bset.reset();
		// TODO beware of false sharing
		size_t quick_considered[N_THREADS] = {};
		size_t considered[N_THREADS] = {};
		size_t batch_size[N_THREADS] = {};
		bool done[N_THREADS] = {};
		fprintf(stderr, "started mini-batching phase.\n");

		size_t done_ct = 0;
		while (done_ct < N_THREADS) {
			for (size_t t = 0; t<N_THREADS; ++t) {
				if (bucket_output_vs[t].size() == 0 || batch_size[t] >= BATCH_TGT/N_THREADS) {
					if (!done[t]) {
						done[t] = true;
						done_ct += 1;
					}
					continue;
				}

				const txn_t& txn = bucket_output_vs[t].front();
				quick_considered[t] += 1;
				if (quick_considered[t] >= 2*BATCH_TGT/N_THREADS) {
					if (!done[t]) {
						done[t] = true;
						done_ct += 1;
					}
					continue;
				}
				if ((txn.ops.size() >= 1 && lock_bset.test(hash_key(txn.ops[0]) % N_HASH_SLOTS)) || 
					(txn.ops.size() >= 2 && lock_bset.test(hash_key(txn.ops[1]) % N_HASH_SLOTS))) {
					// fast abort
					bucket_output_vs[t].push(txn);
					bucket_output_vs[t].pop();
					continue;
				}

				size_t n_conflicts = 0;
				for (size_t i = 0; i<txn.ops.size(); ++i) {
					n_conflicts += locks.find(txn.ops[i]) != locks.end();
				}
				if (n_conflicts > 0) {
					// abort, try later.
					bucket_output_vs[t].push(txn);
				} else {
					// now I hold the locks.
					for (size_t i = 0; i<txn.ops.size(); ++i) {
						locks.insert(txn.ops[i]);
						lock_bset.set(hash_key(txn.ops[i]) % N_HASH_SLOTS);
					}
					batch_size[t] += 1;
					total_executed += 1;
				}
				considered[t] += 1;
				bucket_output_vs[t].pop();

				if (considered[t] >= 2*BATCH_TGT/N_THREADS) {
					if (!done[t]) {
						done[t] = true;
						done_ct += 1;
					}
					continue;
				}

			}
		}

		size_t total_batch_size = 0;
		for (size_t t = 0; t<N_THREADS; ++t) {
			total_batch_size += batch_size[t];
			total_considered += considered[t];
			total_quick_considered += quick_considered[t];
		}
		fprintf(stderr, "batch_size: %lu\n", total_batch_size);
	}

	fprintf(stderr, "total_considered: %lu\n", total_considered);
	fprintf(stderr, "total_quick_considered: %lu\n", total_quick_considered);
    return 0;
}
