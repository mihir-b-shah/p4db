
#include "sim.h"

#include <cstdio>
#include <map>

#define N_NODES 3

int main() {
    batch_iter_t iter = get_batch_iter(workload_e::YCSB_99_8);

	std::vector<txn_t> all_txns;
    std::vector<txn_t> batch;
	while ((batch = iter.next_batch()).size() > 0) {
		for (const txn_t& txn : batch) {
			all_txns.push_back(txn);
		}
	}

    layout_t layout(all_txns);
	std::vector<sw_txn_t> sw_txns = prepare_txns_sw(0, all_txns, layout);
            
    std::map<size_t, size_t> pass_cts;
    for (const sw_txn_t& sw_txn : sw_txns) {
        if (pass_cts.find(sw_txn.passes.size()) == pass_cts.end()) {
            pass_cts.insert({sw_txn.passes.size(), 0});
        }
        pass_cts[sw_txn.passes.size()] += 1;
    }

    for (const auto& pr : pass_cts) {
        printf("%lu passes: %lu\n", pr.first, pr.second);
    }

    return 0;
}
