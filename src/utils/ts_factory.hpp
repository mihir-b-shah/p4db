#pragma once

#include "ee/types.hpp"

#include <chrono>
#include <iostream>
#include <sstream>
#include <atomic>

struct datetime_t {
    uint64_t value;

    static datetime_t now() {
        const auto p1 = std::chrono::system_clock::now();
        using duration = std::chrono::duration<uint64_t, std::micro>;
        const auto ts = std::chrono::duration_cast<duration>(p1.time_since_epoch()).count();
        return datetime_t{ts};
    }

    operator uint64_t() const {
        return value;
    }
};

typedef uint64_t timestamp_t;

struct ClockTimestampFactory {
    using clock = std::chrono::high_resolution_clock;

    clock::time_point start = clock::now();

    ClockTimestampFactory() {
        // std::stringstream ss;
        // ss << "start_ts=" << get() << '\n';
        // std::cout << ss.str();
    }

    timestamp_t get() {
        uint64_t ts = std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - start).count();
        return timestamp_t{ts};
    }
};

struct UniqueClockTimestampFactory {
    using clock = std::chrono::high_resolution_clock;

    clock::time_point start = clock::now();
    uint64_t mask;

    UniqueClockTimestampFactory();

    timestamp_t get() {
        uint64_t ts = std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - start).count();
        return timestamp_t{(ts << 16) | mask}; // 2^48 ns -> 3.25781223 days
    }
};


struct AtomicTimestampFactory {
    static inline std::atomic<uint64_t> cntr{1};

    timestamp_t get() {
        uint64_t ts = cntr.fetch_add(1);
        return timestamp_t{ts};
    }
};


// using TimestampFactory = AtomicTimestampFactory;
// using TimestampFactory = ClockTimestampFactory;
using TimestampFactory = UniqueClockTimestampFactory;
