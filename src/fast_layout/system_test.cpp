
#include "sim.h"

#include <cstdio>

/*
Just use 1 port for now.
*/

int main() {
    batch_iter_t iter = get_batch_iter(workload_e::YCSB);
    std::vector<txn_t> batch;

    switch_t p4_switch; 

    while ((batch = iter.next_batch()).size() != 0) {
        layout_t layout = get_layout(batch);
        for (const txn_t& txn : batch) {
            sw_txn_t sw_txn(/*port*/ 0, layout, txn);
            p4_switch.send(sw_txn);
            // no need to receive now, since mock egress queues are unboundedly large.
        }
    }
    return 0;
}
