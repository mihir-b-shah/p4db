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
	std::array<OP, NUM_OPS> cold_ops;
	std::array<std::pair<OP, TupleLocation>, NUM_OPS> hot_ops_pass1;
	std::array<std::pair<OP, TupleLocation>, MAX_OPS_PASS2_ACCEL> hot_ops_pass2;
	std::optional<db_key_t> hottest_any_cold_k;
	std::optional<db_key_t> hottest_local_cold_k;
	bool cold_all_local;
	bool init_done;
	bool do_accel;
	std::bitset<DeclusteredLayout::NUM_SW_LOCKS> locks_check;
	std::bitset<DeclusteredLayout::NUM_SW_LOCKS> locks_acquire;
	TxnId id;

	Txn() : init_done(false) {}
};
