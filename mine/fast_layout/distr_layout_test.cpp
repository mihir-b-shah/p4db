
#include "sim.h"

#include <cstdio>
#include <map>
#include <algorithm>

#define N_NODES 6

int main() {
    batch_iter_t iter = get_batch_iter(workload_e::YCSB_99_16);

	std::vector<txn_t> all_txns;
    std::vector<txn_t> batch;
	while ((batch = iter.next_batch()).size() > 0) {
		for (const txn_t& txn : batch) {
			all_txns.push_back(txn);
		}
	}

	std::vector<std::pair<db_key_t, size_t>> key_cts_v = get_key_cts(all_txns);
	std::vector<std::pair<size_t, txn_t>> node_subtxns[N_NODES];
	size_t freqs[N_NODES] = {};

	std::unordered_map<db_key_t, size_t> key_to_node;
	for (size_t i = 0; i<key_cts_v.size(); ++i) {
		size_t best_node = 0;
		for (size_t j = 1; j<N_NODES; ++j) {
			if (freqs[j] < freqs[best_node]) {
				best_node = j;
			}
		}
		key_to_node.insert({key_cts_v[i].first, best_node});
		freqs[best_node] += key_cts_v[i].second;
	}

	for (size_t j = 0; j<all_txns.size(); ++j) {
		const txn_t& txn = all_txns[j];
		
		for (size_t i = 0; i<N_NODES; ++i) {
			std::pair<size_t, txn_t> txn_pair;
			txn_pair.first = j;
			node_subtxns[i].push_back(txn_pair);
		}
		for (db_key_t k : txn.ops) {
			node_subtxns[key_to_node[k]].back().second.ops.push_back(k);
		}
		for (size_t i = 0; i<N_NODES; ++i) {
			if (node_subtxns[i].back().second.ops.size() == 0) {
				node_subtxns[i].pop_back();
			}
		}
	}

	std::vector<std::vector<size_t>> pass_cts_ids(all_txns.size());

	// it's a pain to change N_STAGES to simulate using 1/N_NODES the registers.
	// So just set N_STAGES /= N_NODES in the header file.
	for (size_t n = 0; n<N_NODES; ++n) {
		printf("Stats for node %lu\n", n);
		std::vector<txn_t> sub_txns;
		for (const auto& pr : node_subtxns[n]) {
			sub_txns.push_back(pr.second);
		}
		layout_t layout(sub_txns);

		std::map<size_t, size_t> pass_cts;
		for (const auto& pr : node_subtxns[n]) {
			const txn_t& txn = pr.second;
			sw_txn_t sw_txn(0, layout, txn);
			pass_cts_ids[pr.first].push_back(sw_txn.passes.size());
			if (pass_cts.find(sw_txn.passes.size()) == pass_cts.end()) {
				pass_cts.insert({sw_txn.passes.size(), 0});
			}
			pass_cts[sw_txn.passes.size()] += 1;
		}

		for (const auto& pr : pass_cts) {
			printf("\t%lu passes: %lu\n", pr.first, pr.second);
		}
	}

	std::map<size_t, size_t> pass_cts;
	for (const std::vector<size_t>& pass_els : pass_cts_ids) {
		size_t p = 0;
		for (size_t pel : pass_els) {
			p = std::max(p, pel);
		}
		if (pass_cts.find(p) == pass_cts.end()) {
			pass_cts[p] = 0;
		}
		pass_cts[p] += 1;
	}

	printf("Total stats:\n");
	for (const auto& pr : pass_cts) {
		printf("\t%lu passes: %lu\n", pr.first, pr.second);
	}

    return 0;
}
