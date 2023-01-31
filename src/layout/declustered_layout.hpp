#pragma once

#include "tuple_location.hpp"

#include <cstdint>
#include <unordered_map>

struct DeclusteredLayout {
    // Intel confidential
    static constexpr auto STAGES = 64;
    static constexpr auto REGS_PER_STAGE = 1;
	static constexpr auto NUM_REGS = STAGES * REGS_PER_STAGE;
    static constexpr auto REG_SIZE = 10000;
    static constexpr auto LOCK_BITS = 2;
    static constexpr auto PARTITIONS = STAGES * REGS_PER_STAGE;
    static constexpr auto MAX_ACCESSES = 64;
	static constexpr size_t NUM_INSTRS = 8;

    std::unordered_map<uint64_t, TupleLocation> switch_tuples;

    bool is_hot(uint64_t idx) const { return true; }

    TupleLocation get_location(uint64_t idx) {
		TupleLocation tl;
		tl.stage_id = 0;
		tl.reg_array_id = 0;
		tl.reg_array_idx = (uint16_t) (idx & 0xffff);
		tl.lock_bit = 0;
		return tl;
	}
};
