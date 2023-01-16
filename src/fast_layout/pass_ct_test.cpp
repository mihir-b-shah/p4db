
#include "sim.h"

#include <cstdio>
#include <map>
#include <algorithm>

#define PERCENT_TICKS 100

int main() {
    batch_iter_t iter = get_batch_iter(workload_e::YCSB);
    std::map<size_t, size_t> pass_cts;
    std::vector<txn_t> batch;
    std::vector<size_t> sizes;

    while ((batch = iter.next_batch()).size() != 0) {
        sizes.push_back(batch.size());
        layout_t layout(batch);
        for (const txn_t& txn : batch) {
            sw_txn_t sw_txn(0, layout, txn);
            if (pass_cts.find(sw_txn.passes.size()) == pass_cts.end()) {
                pass_cts.insert({sw_txn.passes.size(), 0});
            }
            pass_cts[sw_txn.passes.size()] += 1;
        }
    }

    std::sort(sizes.begin(), sizes.end());
    for (size_t i = 0; i<PERCENT_TICKS; i+=10) {
        printf("%lu percentile: %lu\n", i, sizes[sizes.size()*i/PERCENT_TICKS]);
    }
    for (const auto& pr : pass_cts) {
        printf("%lu passes: %lu\n", pr.first, pr.second);
    }
    return 0;
}
