
#include "layout/declustered_layout.hpp"

#include <cassert>

DeclusteredLayout::DeclusteredLayout(std::vector<std::pair<db_key_t, size_t>>& id_freq)
	// : avail_blocks(0) { TODO impl the scheduler, then change this hard-coded value.
	   : avail_blocks(6) {

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

	/*	TODO, remove this. We need to get an allocation once the scheduler works. */
	for (size_t i = 0; i<NUM_BLOCKS; ++i) {
		virt_block_map[i] = i;
	}
}

std::pair<bool, TupleLocation> DeclusteredLayout::get_location(db_key_t k) {
	std::pair<bool, TupleLocation> info;
	TupleLocation& tl = virt_map[k];
	size_t old_block = tl.reg_array_idx / NUM_KEYS_PER_BLOCK;
	size_t offset = tl.reg_array_idx % NUM_KEYS_PER_BLOCK;
	size_t new_block = old_block < NUM_BLOCKS ? virt_block_map[old_block] : old_block;
	info.second = tl;
	info.second.reg_array_idx = new_block * NUM_KEYS_PER_BLOCK + offset;
	info.first = tl.reg_array_idx < avail_blocks*NUM_KEYS_PER_BLOCK;
	return info;
}

void DeclusteredLayout::update_virt_offsets(const std::vector<size_t>& blocks) {
	assert(blocks.size() == NUM_BLOCKS);
	for (size_t i = 0; i<blocks.size(); ++i) {
		virt_block_map[i] = blocks[i];
	}
	avail_blocks = blocks.size();
}
