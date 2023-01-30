
#include "sim.h"

#include <cstdio>
#include <map>

#define N_NODES 3

int main() {
    batch_iter_t iter = get_batch_iter(workload_e::SYN_UNIF);

	std::vector<txn_t> all_txns;
    std::vector<txn_t> batch;
	while ((batch = iter.next_batch()).size() > 0) {
		for (const txn_t& txn : batch) {
			all_txns.push_back(txn);
		}
	}

    layout_t layout(all_txns);
            
    std::map<size_t, size_t> pass_cts;
    for (const txn_t& txn : all_txns) {
        sw_txn_t sw_txn(0, layout, txn);

		/*
        printf("txn: ");
        for (size_t i = 0; i<txn.ops.size(); ++i) {
			std::optional<tuple_loc_t> tl = layout.lookup(txn.ops[i]);
			if (!tl.has_value()) {
				continue;
			}
			size_t freq = layout.get_key_ct(txn.ops[i]);
            printf("k=%lu[l={%lu,%lu},f=%lu] ", txn.ops[i], tl->stage, tl->reg, freq);
        }
        printf("| passes: %lu\n", sw_txn.passes.size());

		size_t pass2m_freqs[N_STAGES*REGS_PER_STAGE] = {};
		for (const tuple_loc_t& tl : sw_txn.locs) {
			size_t freq_idx = tl.stage * REGS_PER_STAGE + tl.reg;
			size_t k = layout.rev_lookup(tl.stage, tl.reg, tl.idx);
			size_t freq = layout.get_key_ct(k);
			if (pass2m_freqs[freq_idx] > 0) {
				printf("collided key freq: %lu\n", freq);
			}
			pass2m_freqs[freq_idx] = freq;
        }
		*/

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
