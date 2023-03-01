#pragma once

#include "ee/defs.hpp"
#include "ee/types.hpp"
#include "ee/loc_info.hpp"

#include <array>
#include <cstdint>

struct Txn {
	struct OP {
		db_key_t id;
		LocationInfo loc_info;	
		AccessMode mode;
		uint32_t value;
	};
	// start off with everything in cold, opportunistically move to hot.
	std::array<OP, NUM_OPS> cold_ops;
	std::array<OP, NUM_OPS> hot_ops;
	bool cold_all_local;
	TxnId id;
};
