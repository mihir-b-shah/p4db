
#include "sim.h"

#include <cstdio>
#include <map>

#define N_NODES 3

int main() {
    batch_iter_t iter = get_batch_iter(workload_e::INSTACART);
    std::vector<txn_t> batch = iter.next_batch();
    layout_t layout(batch);
            
    std::map<size_t, size_t> pass_cts;
    for (const txn_t& txn : batch) {
        sw_txn_t sw_txn(0, layout, txn);
        printf("txn: ");
        for (size_t i = 0; i<txn.ops.size(); ++i) {
            printf("k=%lu[f=%lu] ", txn.ops[i], layout.get_key_ct(txn.ops[i]));
        }
        printf("| passes: %lu\n", sw_txn.passes.size());

        if (sw_txn.passes.size() == 2) {
            const sw_pass_txn_t& pass0 = sw_txn.passes[0];
            const sw_pass_txn_t& pass1 = sw_txn.passes[1];
            for (size_t i = 0; i<N_STAGES; ++i) {
                for (size_t j = 0; j<REGS_PER_STAGE; ++j) {
                    // only look at multipass
                    if (pass1.grid[i][j].has_value()) {
                        // let's see who came before me, and our relative popularities.
                        db_key_t confl_key = layout.rev_lookup(i, j, pass1.grid[i][j].value());
                        assert(pass0.grid[i][j].has_value());
                        db_key_t b_key = layout.rev_lookup(i, j, pass0.grid[i][j].value());
                        printf("key %lu on pass %lu popularity: %lu, confl %lu key on pass %lu popularity: %lu\n", b_key, 0UL, layout.get_key_ct(b_key), confl_key, 1UL, layout.get_key_ct(confl_key));
                    }
                }
            }
        }

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
