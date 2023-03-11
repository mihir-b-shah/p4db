#pragma once

#include <chrono>
#include <cstdint>
#include <istream>
#include <ostream>
#include <cassert>

enum class AccessMode : uint8_t {
	INVALID = 0,
	READ = 1,
	WRITE = 2,
};

struct TxnId {
	static constexpr size_t MINI_BATCH_ID_WIDTH = 27;
	struct __attribute__((packed)) id_field_t {
		uint8_t node_id : 4;
		uint8_t valid : 1;
		// XXX deliberately keep this big enough we are never at risk of overflowing...
		uint32_t mini_batch_id : MINI_BATCH_ID_WIDTH;

		id_field_t (bool valid, size_t node_id, size_t mini_batch_id) : node_id(node_id), valid(valid), mini_batch_id(mini_batch_id) {}
	};
	union {
		id_field_t field;
		uint32_t repr;
	};

	TxnId() : field(false, 0, 0) {}
	TxnId(uint32_t packed) : repr(packed) {}
	TxnId(bool valid, size_t node_id, size_t mini_batch_id) : field(valid, node_id, mini_batch_id) {}
	uint32_t get_packed() { return repr; }
};

namespace p4db {

struct table_t {
    uint64_t value;
    operator uint64_t() const {
        return value;
    }
};
static_assert(std::is_trivial<table_t>::value, "table_t is not a POD");

} // namespace p4db

typedef uint64_t db_key_t;
