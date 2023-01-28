
#include "sim.h"

#include <cstdio>
#include <unordered_map>
#include <vector>

int main() {
    batch_iter_t iter = get_batch_iter(workload_e::INSTACART);
    std::vector<txn_t> batch;
	std::vector<txn_t> all_txns;

    while ((batch = iter.next_batch()).size() != 0) {
		for (const txn_t& txn : batch) {
			all_txns.push_back(txn);
		}
    }

	auto key_cts_v = get_key_cts(all_txns);
	std::unordered_map<db_key_t, size_t> key_cts(key_cts_v.begin(), key_cts_v.end());

	std::unordered_map<db_key_t, std::pair<size_t, size_t>> intervals;
	for (size_t i = 0; i<all_txns.size(); ++i) {
		const txn_t& txn = all_txns[i];
		for (db_key_t op : txn.ops) {
			if (intervals.find(op) == intervals.end()) {
				intervals[op] = {i, i};
			}
			intervals[op].second = i;
		}
	}

	for (const auto& pr : intervals) {
		printf("Key=%lu with freq=%lu lies on interval [%lu,%lu], density=%f\n", pr.first, key_cts[pr.first], pr.second.first, pr.second.second, (double) key_cts[pr.first]/(pr.second.second-pr.second.first));
	}
    return 0;
}
