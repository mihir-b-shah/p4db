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
	static constexpr size_t NODE_BITS = 5;
	static constexpr size_t THREAD_BITS = 5;
	static constexpr size_t TXN_BITS = 14;
	static constexpr size_t EPOCH_BITS = 8;

	uint8_t node_id;
	uint8_t thread_id;
	uint16_t txn_id;
	uint8_t epoch_id;

	TxnId() {}
	TxnId(size_t node_id, size_t thread_id, size_t txn_id, size_t epoch_id) :
		node_id(node_id), thread_id(thread_id), txn_id(txn_id), epoch_id(epoch_id) {
		
		assert(node_id < (1 << NODE_BITS));
		assert(thread_id < (1 << THREAD_BITS));
		assert(txn_id < (1 << TXN_BITS));
		assert(epoch_id < (1 << EPOCH_BITS));
	}

	TxnId(uint32_t packed) {
		node_id = packed & ((1 << NODE_BITS) - 1);
		thread_id = (packed >> NODE_BITS) & ((1 << THREAD_BITS) - 1);
		txn_id = (packed >> (NODE_BITS + THREAD_BITS)) & ((1 << TXN_BITS) - 1);
		epoch_id = (packed >> (NODE_BITS + THREAD_BITS + TXN_BITS)) & ((1 << EPOCH_BITS) - 1);
	}

	uint32_t get_packed() {
		return node_id | (thread_id << NODE_BITS) |
			(txn_id << (NODE_BITS + THREAD_BITS)) | 
			(epoch_id << (NODE_BITS + THREAD_BITS + TXN_BITS));
	}
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
