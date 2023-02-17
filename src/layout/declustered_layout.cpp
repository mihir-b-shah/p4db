
#include "layout/declustered_layout.hpp"

#include <cassert>

DeclusteredLayout::DeclusteredLayout(const std::vector<std::pair<uint64_t, size_t>>& id_freq)
	: avail_blocks(0) {

	// this is a consistent function, so no coord necessary across nodes.
    size_t idx_to_alloc[NUM_REGS] = {};
	size_t weight[NUM_REGS] = {};

    for (const auto& pr : id_freq) {
        uint64_t k = pr.first;

        size_t r_low = 0;
		for (size_t r = 0; r<NUM_REGS; ++r) {
			if (weight[r_low] > weight[r]) {
				r_low = r;
			}
		}

        TupleLocation loc = {r_low, idx_to_alloc[r_low]++};
		weight[r_low] += pr.second;
        virt_map.emplace(k, loc);
    }

	/*	TODO, remove this. We need to get an allocation once the scheduler works. */
	for (size_t i = 0; i<NUM_BLOCKS; ++i) {
		virt_block_map[i] = i;
	}
}

TupleLocation DeclusteredLayout::get_location(uint64_t k) {
	TupleLocation tl = virt_map[k];
	size_t old_block = tl.reg_array_idx / NUM_KEYS_PER_BLOCK;
	size_t offset = tl.reg_array_idx % NUM_KEYS_PER_BLOCK;
	size_t new_block = virt_block_map[old_block];
	tl.reg_array_idx = new_block * NUM_KEYS_PER_BLOCK + offset;
	return tl;
}

bool DeclusteredLayout::is_hot(uint64_t k) {
	TupleLocation virt_loc = virt_map[k];
	size_t virt_blk = virt_loc.reg_array_idx / NUM_KEYS_PER_BLOCK;
	return virt_blk < avail_blocks;
}

void DeclusteredLayout::update_virt_offsets(const std::vector<size_t>& blocks) {
	assert(blocks.size() <= NUM_BLOCKS);
	for (size_t i = 0; i<blocks.size(); ++i) {
		virt_block_map[i] = blocks[i];
	}
	avail_blocks = blocks.size();
}
