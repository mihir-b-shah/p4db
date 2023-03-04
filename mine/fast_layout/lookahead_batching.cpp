
#include "sim.h"

#include <cstdio>
#include <unordered_map>
#include <cassert>
#include <cstdlib>
#include <algorithm>
#include <random>
#include <queue>
#include <vector>

static constexpr size_t BATCH_TGT = 1000;

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
			fprintf(stderr, "%lu ", key_cts[cold_txn.ops[i]]);
			if (key_cts[cold_txn.ops[max_idx]] < key_cts[cold_txn.ops[i]]) {
				max_idx = i;
			}
		}
		fprintf(stderr, "\n");
		assert(max_idx == 0);
	}

	std::unordered_map<db_key_t, std::queue<std::pair<txn_t, txn_t>>> buckets;
	std::queue<txn_t> bucket_output;
	for (auto& pr : txns) {
		txn_t& cold_txn = pr.second;
		if (cold_txn.ops.size() == 0) {
			// just put it anywhere, doesn't matter.
			bucket_output.push(cold_txn);
			continue;
		}
		if (buckets.find(cold_txn.ops[0]) == buckets.end()) {
			std::queue<std::pair<txn_t, txn_t>> q;
			buckets.insert({cold_txn.ops[0], q});
		}
		// cold_txn.ops.resize(1);
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
	while (bucket_output.size() < txns.size()) {
		//printf("bucket_output.size(): %lu\n", bucket_output.size());
		size_t added = 0;
		while (add_back.size() > 0) {
			pq.push(add_back.front());
			add_back.pop();
		}
		std::unordered_set<db_key_t> no_consider;
		while (pq.size() > 0 && added < BATCH_TGT) {
			auto& q = buckets[pq.top()];
			if (q.size() == 0) {
				pq.pop();
				continue;
			}
			if (no_consider.find(pq.top()) != no_consider.end()) {
				add_back.push(pq.top());
				pq.pop();
				continue;
			}
			if (q.front().second.ops.size() >= 2) {
				no_consider.insert(q.front().second.ops[1]);
			}
			bucket_output.push(q.front().second);
			db_key_t k = pq.top();
			pq.pop();
			q.pop();
			add_back.push(k);
			added += 1;
		}
	}

	size_t total_considered = 0;
	size_t total_batch_size = 0;
	size_t n_batches = 0;
	while (bucket_output.size() > 0) {
		std::unordered_set<db_key_t> locks;
		size_t considered = 0;
		size_t batch_size = 0;
		while (bucket_output.size() > 0 && considered < BATCH_TGT) {
			const txn_t& txn = bucket_output.front();
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
				}
				batch_size += 1;
			}
			considered += 1;
			bucket_output.pop();
		}
		total_considered += considered;
		total_batch_size += batch_size;
		n_batches += 1;
		printf("considered: %lu, batch_size: %lu\n", considered, batch_size);
	}
	printf("n_batches: %lu, total_considered: %lu\n", n_batches, total_considered);
    return 0;
}
