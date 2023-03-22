#pragma once

#include "ee/defs.hpp"
#include "ee/types.hpp"
#include "ee/loc_info.hpp"
#include "layout/declustered_layout.hpp"

#include <array>
#include <bitset>
#include <cstdint>
#include <optional>

struct Txn {
	struct OP {
		db_key_t id;
		LocationInfo loc_info;	
		AccessMode mode;
		uint32_t value;
	};
	// start off with everything in cold, opportunistically move to hot.
	std::array<OP, N_OPS> cold_ops;
	std::array<std::pair<OP, TupleLocation>, N_OPS> hot_ops_pass1;
	std::array<std::pair<OP, TupleLocation>, MAX_OPS_PASS2_ACCEL> hot_ops_pass2;
	bool init_done;
	bool do_accel;
	size_t n_aborts;
	std::optional<size_t> hottest_cold_i1;
	std::optional<size_t> hottest_cold_i2;
	std::bitset<DeclusteredLayout::NUM_SW_LOCKS> locks_check;
	std::bitset<DeclusteredLayout::NUM_SW_LOCKS> locks_acquire;
	TxnId id;
    size_t loader_id;

	Txn() : init_done(false), n_aborts(0) {}
};
