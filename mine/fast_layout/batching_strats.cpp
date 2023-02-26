
#include "sim.h"

#include <cstdio>
#include <unordered_map>
#include <cassert>
#include <bitset>
#include <cstdlib>

static constexpr size_t N_BUCKETS = 40;

void hash_batching(const std::vector<txn_t>& txns) {
	std::unordered_map<db_key_t, size_t> freqs;

    for (auto it = txns.begin(); it != txns.end(); ++it) {
		const txn_t& cold_txn = *it;
		for (size_t i = 0; i<cold_txn.ops.size(); ++i) {
			db_key_t k = cold_txn.ops[i];
			if (freqs.find(k) == freqs.end()) {
				freqs.emplace(k, 0);
			}
			freqs[k] += 1;
		}
	}

	std::vector<std::pair<db_key_t, size_t>> freqs_v(freqs.begin(), freqs.end());
	std::sort(freqs_v.begin(), freqs_v.end(), [](const auto& pr1, const auto& pr2) {
		if (pr1.second == pr2.second) {
			return pr1.first < pr2.first;
		} else {
			return pr1.second > pr2.second;
		}
	});
	std::unordered_map<db_key_t, size_t> bucket_hash;
	for (size_t i = 0; i<freqs_v.size(); ++i) {
		bucket_hash.emplace(freqs_v[i].first, i % N_BUCKETS);
	}

	std::vector<txn_t> new_hist;
	std::vector<txn_t> buckets[N_BUCKETS];

	for (auto it = txns.begin(); it != txns.end(); ++it) {
		const txn_t& cold_txn = *it;
		size_t max_freq_idx = 0;
		for (size_t i = 1; i<cold_txn.ops.size(); ++i) {
			if (freqs[cold_txn.ops[i]] > freqs[cold_txn.ops[max_freq_idx]]) {
				max_freq_idx = i;
			}
		}

		buckets[bucket_hash[cold_txn.ops[max_freq_idx]]].push_back(cold_txn);
	}

	for (const std::vector<txn_t>& txns : buckets) {
		std::unordered_set<db_key_t> seen;
		size_t batch_size = 0;
		for (const txn_t& txn : txns) {
			bool ok = true;
			for (db_key_t op : txn.ops) {
				ok &= seen.find(op) != seen.end();
			}
			if (ok) {
				for (db_key_t op : txn.ops) {
					seen.insert(op);
				}
				batch_size += 1;
			}
		}
		printf("batch_size: %lu\n", batch_size);
	}
}

int main() {
    batch_iter_t iter = get_batch_iter(workload_e::YCSB_99_16);
    std::list<std::pair<txn_t, txn_t>> txns = iter.get_comps();
	std::vector<txn_t> cold_txns;
	for (const auto& pr : txns) {
		cold_txns.push_back(pr.second);
	}
	hash_batching(cold_txns);
    return 0;
}
