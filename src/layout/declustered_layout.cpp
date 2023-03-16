
#include "layout/declustered_layout.hpp"

#include <cassert>

static constexpr uint32_t NO_BLOCK = UINT32_MAX;

DeclusteredLayout::DeclusteredLayout(std::vector<std::pair<db_key_t, size_t>>&& id_freq) 
    : block_num(NO_BLOCK), id_freq(id_freq) {

	/*	allow us to not lock record 0 in the register (and not assume anything about the ordering
		of id_freq. std::sort expects a less than function, so we invert this order. */
	std::sort(id_freq.begin(), id_freq.end(), [](const std::pair<db_key_t, size_t>& pr1, const std::pair<db_key_t, size_t>& pr2){
		return !TupleLocation::total_order_gt(pr1.second, pr1.first, pr2.second, pr2.first);
	});

	// this is a consistent function, so no coord necessary across nodes.
    size_t idx_to_alloc[NUM_REGS] = {};
	size_t weight[NUM_REGS] = {};
	size_t lock_weight[NUM_SW_LOCKS] = {};

    for (const auto& pr : id_freq) {
        db_key_t k = pr.first;

        size_t r_low = 0;
		for (size_t r = 0; r<NUM_REGS; ++r) {
			if (weight[r_low] > weight[r]) {
				r_low = r;
			}
		}
		// TODO note this is a trace-wide frequency, is that problematic?
		weight[r_low] += pr.second;

        TupleLocation loc = {static_cast<uint8_t>(r_low), 
			static_cast<uint16_t>(idx_to_alloc[r_low]++), pr.second, NO_LOCK};
		if (loc.reg_array_idx != 0) {
			size_t lk_low = 0;
			for (size_t lk = 0; lk<NUM_SW_LOCKS; ++lk) {
				if (lock_weight[lk_low] > lock_weight[lk]) {
					lk_low = lk;
				}
			}
			lock_weight[lk_low] += pr.second;
			loc.lock_pos = lk_low;
		}
        virt_map.emplace(k, loc);
    }

    printf("Accelerating %lu keys, going down to freq %lu\n", N_ACCEL_KEYS, id_freq[N_ACCEL_KEYS].second);
}

std::pair<bool, TupleLocation> DeclusteredLayout::get_location(db_key_t k) {
	std::pair<bool, TupleLocation> info;
	TupleLocation& tl = virt_map[k];
	info.first = tl.reg_array_idx < N_ACCEL_KEYS;
    info.second = tl;
    info.second.reg_array_idx = (N_ACCEL_KEYS * this->block_num) + tl.reg_array_idx;
	return info;
}
