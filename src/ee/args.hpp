#pragma once

#include "ee/defs.hpp"
#include "ee/types.hpp"

#include <array>
#include <cstdint>

struct Txn {
	struct OP {
		db_key_t id;
		AccessMode mode;
		uint32_t value;
	};
	std::array<OP, NUM_OPS> ops;
	bool on_switch;
	TxnId id;
};
