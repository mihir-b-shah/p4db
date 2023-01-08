
#include "sim.h"

#include <cstdio>

/*
Just use 1 port for now.
*/
    
switch_t p4_switch; 

int main() {
    batch_iter_t iter = get_batch_iter(workload_e::YCSB);
    std::vector<txn_t> batch = iter.next_batch();
    layout_t layout = get_layout(batch);

    for (size_t cycle = 0; cycle < 50000; ++cycle) {
        if (cycle < batch.size()) {
            sw_txn_t sw_txn(/*port*/ 0, layout, batch[cycle]);
            p4_switch.send(sw_txn);
        }
        p4_switch.run_cycle();
        // no need to receive now, since mock egress queues are unboundedly large.
    }
    return 0;
}
