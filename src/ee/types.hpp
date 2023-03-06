#pragma once

#include <chrono>
#include <cstdint>
#include <istream>
#include <ostream>
#include <cassert>

struct AccessMode {
    using value_t = uint32_t;

    static constexpr value_t INVALID = 0x00000000;
    static constexpr value_t READ = 0x00000001;
    static constexpr value_t WRITE = 0x00000002;

    value_t value;

    constexpr AccessMode() : value(AccessMode::INVALID) {}

    constexpr AccessMode(value_t value) : value(value) {}

    operator value_t() const {
        return get_clean();
    }

    bool operator==(const value_t& rhs) const {
        return value == rhs;
    }

    // bool operator==(const AccessMode& rhs) const {
    //     return value == rhs.value;
    // }

    value_t get_clean() const {
        return value & 0x000000ff;
    }

    bool by_switch() const {
        return (value >> 8) & 0xff;
    }

    void set_switch_index(uint16_t idx) {
        value |= static_cast<uint32_t>(__builtin_bswap16(idx)) << 16;
        value |= 0x0000aa00;
    }
};

struct TxnId {
	struct __attribute__((packed)) id_field_t {
		uint8_t node_id : 8;
		uint8_t valid : 1;
		// XXX deliberately keep this big enough we are never at risk of overflowing...
		uint32_t mini_batch_id : 23;

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
