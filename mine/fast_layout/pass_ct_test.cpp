
#include "sim.h"

#include <cstdio>
#include <map>
#include <algorithm>

#define PERCENT_TICKS 100

int main() {
    batch_iter_t iter = get_batch_iter(workload_e::INSTACART);
    std::map<size_t, size_t> pass_cts;
    std::vector<txn_t> batch;
    std::vector<size_t> sizes;

    while ((batch = iter.next_batch()).size() != 0) {
        sizes.push_back(batch.size());
        layout_t layout(batch);
        printf("Layout.num_keys(): %lu\n", layout.num_keys());
        for (const txn_t& txn : batch) {
            sw_txn_t sw_txn(0, layout, txn);
            if (pass_cts.find(sw_txn.passes.size()) == pass_cts.end()) {
                pass_cts.insert({sw_txn.passes.size(), 0});
            }
            pass_cts[sw_txn.passes.size()] += 1;

            printf("txn: ");
            for (size_t i = 0; i<txn.ops.size(); ++i) {
                tuple_loc_t tl = layout.lookup(txn.ops[i]).value();
                printf("{s=%lu,r=%lu,i=%lu}[f=%lu] ", tl.stage, tl.reg, tl.idx, layout.get_key_ct(txn.ops[i]));
            }
            printf("| passes: %lu\n", sw_txn.passes.size());
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
