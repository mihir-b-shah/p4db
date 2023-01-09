
#include "sim.h"

#include <cstdio>

/*
Just use 1 port for now.
*/
#define PORT 0
#define BACKOFF_THR 0.8
    
switch_t p4_switch; 

int main() {
    batch_iter_t iter = get_batch_iter(workload_e::SYN_HOT_8);
    std::vector<txn_t> batch = iter.next_batch();
    layout_t layout = get_layout(batch);
    size_t received = 0;
    size_t cycle = 0;
    size_t p_batch = 0;

    while (1) {
        if (p_batch < batch.size() && !p4_switch.ipb_almost_full(PORT, BACKOFF_THR)) {
            sw_txn_t sw_txn(PORT, layout, batch[p_batch]);
            // this CC should ensure zero drops, for simplicity
            assert(p4_switch.send(sw_txn));
            p_batch += 1;
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
