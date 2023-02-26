
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

static constexpr size_t BATCH_TGT = 500;
static constexpr size_t N_HASH_SLOTS = 8192; // TODO: try smaller

class approx_set_t {
public:
	void clear() {
		impl.clear();
	}
	bool test(size_t i) {
		return impl.find(i) != impl.end();
	}
	void set(size_t i) {
		impl.insert(i);
	}

private:
	std::unordered_set<size_t> impl;
};

static size_t hash_key(db_key_t k) {
	return k & (N_HASH_SLOTS-1);
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
	std::vector<db_key_t> extract_keys;
	while (pq.size() > 0) {
		db_key_t k = pq.top();
		extract_keys.push_back(k);
		pq.pop();
	}
	for (db_key_t k : extract_keys) {
		printf("freq: %lu\n", buckets[k].size());
		pq.push(k);
	}
	printf("pq.size(): %lu\n", pq.size());

	std::queue<db_key_t> add_back;
	std::queue<txn_t> bucket_output;
	while (bucket_output.size() < txns.size()) {
		//printf("bucket_output.size(): %lu\n", bucket_output.size());
		size_t added = 0;
		while (add_back.size() > 0) {
			pq.push(add_back.front());
			add_back.pop();
		}
		while (pq.size() > 0 && added < BATCH_TGT) {
			auto& q = buckets[pq.top()];
			if (q.size() == 0) {
				pq.pop();
				continue;
			}
			bucket_output.push(q.front().second);
			db_key_t k = pq.top();
			pq.pop();
			q.pop();
			add_back.push(k);
			added += 1;
		}
	}

	std::bitset<N_HASH_SLOTS> lock_bset;
	size_t total_quick_considered = 0;
	size_t total_considered = 0;
	while (bucket_output.size() > 0) {
		std::unordered_set<db_key_t> locks;
		lock_bset.reset();
		size_t quick_considered = 0;
		size_t considered = 0;
		size_t batch_size = 0;
		while (bucket_output.size() > 0 && batch_size < BATCH_TGT) {
			const txn_t& txn = bucket_output.front();
			quick_considered += 1;

			if (quick_considered >= 2*BATCH_TGT) {
				break;
			}
			if ((txn.ops.size() >= 1 && lock_bset.test(hash_key(txn.ops[0]))) || 
				(txn.ops.size() >= 2 && lock_bset.test(hash_key(txn.ops[1])))) {
				// fast abort
				bucket_output.push(txn);
				bucket_output.pop();
				continue;
			}

			size_t n_conflicts = 0;
			for (size_t i = 0; i<txn.ops.size(); ++i) {
				n_conflicts += locks.find(txn.ops[i]) != locks.end();
			}
			if (n_conflicts > 0) {
				// abort, try later.
				bucket_output.push(txn);
			} else {
				// now I hold the locks.
				for (size_t i = 0; i<txn.ops.size(); ++i) {
					locks.insert(txn.ops[i]);
					lock_bset.set(hash_key(txn.ops[i]));
				}
				batch_size += 1;
			}
			considered += 1;
			bucket_output.pop();

			if (considered >= 2*BATCH_TGT) {
				break;
			}
		}
		total_considered += considered;
		total_quick_considered += quick_considered;
		printf("batch_size: %lu\n", batch_size);
	}
	printf("total_considered: %lu\n", total_considered);
	printf("total_quick_considered: %lu\n", total_quick_considered);
    return 0;
}
