
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
    size_t received = 0;
    size_t cycle = 0;

    while (1) {
        if (cycle < batch.size()) {
            sw_txn_t sw_txn(/*port*/ 0, layout, batch[cycle]);
            p4_switch.send(sw_txn);
        }
        p4_switch.run_cycle();
        received += p4_switch.recv(0).has_value();
        if (received == batch.size()) {
            break;
        }
        cycle += 1;
    }
    return 0;
}
