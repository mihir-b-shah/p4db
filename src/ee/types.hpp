#pragma once

#include <chrono>
#include <cstdint>
#include <istream>
#include <ostream>

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

namespace p4db {

struct table_t {
    uint64_t value;
    operator uint64_t() const {
        return value;
    }
};
static_assert(std::is_trivial<table_t>::value, "table_t is not a POD");

struct key_t {
    uint64_t value;
    operator uint64_t() const {
        return value;
    }
};
static_assert(std::is_trivial<key_t>::value, "key_t is not a POD");

} // namespace p4db
