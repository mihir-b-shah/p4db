#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <ostream>
#include <utility>

// Note, we don't need to know which stage, which register- just target the p4 reg spec.
// If we were max-cut partitioning for dependencies, etc. then this might matter.
struct TupleLocation {
    uint8_t reg_array_id;
    uint16_t reg_array_idx;
	size_t dist_freq;

    friend std::ostream& operator<<(std::ostream& os, const TupleLocation& self) {
        os << " reg=" << self.reg_array_id << " idx=" << self.reg_array_idx;
        return os;
    }
};

class DeclusteredLayout {
public:
	static constexpr size_t NUM_REGS = 20;
	static constexpr size_t NUM_MAX_OPS = 8;

	// same constants from 01_control_plane, watch out
	static constexpr size_t NUM_BLOCKS = 256;
	static constexpr size_t NUM_KEYS_PER_BLOCK = 128;

	DeclusteredLayout(const std::vector<std::pair<uint64_t, size_t>>& id_freq);
    TupleLocation get_location(uint64_t k);
	std::pair<bool, size_t> is_hot(uint64_t k);

	// TODO: for now, guarantee this never happens in parallel with lookups. Is this true?
	void update_virt_offsets(const std::vector<size_t>& blocks);

private:
	// TODO: std::unordered_map is p slow, profile and see.
	std::unordered_map<uint64_t, TupleLocation> virt_map;
	size_t virt_block_map[NUM_BLOCKS];
	size_t avail_blocks;
};
