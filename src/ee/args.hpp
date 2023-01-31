#pragma once

#include "ee/defs.hpp"
#include "ee/types.hpp"

#include <array>
#include <cstdint>

struct Txn {
	struct OP {
		uint64_t id;
		AccessMode mode;
		uint32_t value;
	};
	std::array<OP, NUM_OPS> ops;
	bool on_switch;
	bool is_hot;
};
