
#include "sim.h"

#include <cstdio>
#include <map>

int main() {
    batch_iter_t iter = get_batch_iter(workload_e::INSTACART);
    std::vector<txn_t> batch = iter.next_batch();
    layout_t layout = get_layout(batch);
            
    std::map<size_t, size_t> pass_cts;
    for (const txn_t& txn : batch) {
        sw_txn_t sw_txn(0, layout, txn);
        printf("txn: ");
        for (size_t i = 0; i<txn.ops.size(); ++i) {
            printf("%lu ", txn.ops[i]);
        }
        printf("| passes: %lu\n", sw_txn.passes.size());
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
