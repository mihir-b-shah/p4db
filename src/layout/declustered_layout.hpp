#pragma once

#include "ee/defs.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <ostream>
#include <optional>
#include <utility>
#include <limits>

// Note, we don't need to know which stage, which register- just target the p4 reg spec.
// If we were max-cut partitioning for dependencies, etc. then this might matter.
struct TupleLocation {
    uint8_t reg_array_id;
    uint16_t reg_array_idx;
	size_t dist_freq;
	uint8_t lock_pos;

    friend std::ostream& operator<<(std::ostream& os, const TupleLocation& self) {
        os << " reg=" << self.reg_array_id << " idx=" << self.reg_array_idx;
        return os;
    }

	// useful for determining what to lock.
	static bool total_order_gt(
			const size_t dist_freq_1, const db_key_t k1, 
			const size_t dist_freq_2, const db_key_t k2) {
		if (dist_freq_1 != dist_freq_2) {
			return dist_freq_1 < dist_freq_2;
		} else {
			return k1 < k2;
		}
	}
};

struct DeclusteredLayout {
    // we should only request from one arena, we are getting from arena-512.
    // for now let's assume we use all key slots in the block.
    static constexpr uint8_t NO_LOCK = std::numeric_limits<uint8_t>::max();

	DeclusteredLayout(std::vector<std::pair<db_key_t, size_t>>&& id_freq);
    std::pair<bool, TupleLocation> get_location(db_key_t k);
    std::optional<db_key_t> rev_lookup(size_t reg_id, size_t reg_idx);

	size_t block_num;
	// TODO: std::unordered_map is p slow, profile and see.
	std::unordered_map<db_key_t, TupleLocation> virt_map;
    std::unordered_map<size_t, db_key_t> rev_by_reg[N_REGS];
	std::vector<std::pair<db_key_t, size_t>> id_freq;
};
