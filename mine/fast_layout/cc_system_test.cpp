
#include "sim.h"

#include <cstdio>

/*
Just use 1 port for now.
*/
#define PORT 0
#define BACKOFF_THR 0.8
    
switch_t p4_switch; 

int main() {
    batch_iter_t iter = get_batch_iter(workload_e::YCSB_99_16);
	std::vector<txn_t> all_txns;
    std::vector<txn_t> batch;
	while ((batch = iter.next_batch()).size() > 0) {
		for (const txn_t& txn : batch) {
			all_txns.push_back(txn);
		}
	}

    layout_t layout(all_txns);
	std::vector<sw_txn_t> sw_txns = prepare_txns_sw(0, all_txns, layout);

    size_t received = 0;
    size_t cycle = 0;
    size_t p_batch = 0;
	size_t rejected = 0;

    while (1) {
        if (p_batch < sw_txns.size() && !p4_switch.ipb_almost_full(PORT, BACKOFF_THR)) {
            const sw_txn_t& sw_txn = sw_txns[p_batch];
            // this CC should ensure zero drops, for simplicity
            assert(p4_switch.send(sw_txn));
            p_batch += 1;
        }
        p4_switch.run_cycle();
        auto res = p4_switch.recv(0);
        received += res.has_value();
		if (res.has_value() && !res.value().first) {
			rejected += 1;
		}
        if (received == sw_txns.size()) {
            break;
        }
        cycle += 1;
    }
	printf("Rejected: %lu\n", rejected);
    return 0;
}
