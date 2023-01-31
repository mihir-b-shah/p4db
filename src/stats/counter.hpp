#pragma once


#include "ee/defs.hpp"
#include "utils/util.hpp"

#include <array>
#include <string_view>


namespace stats {

struct Counter {
    Counter();
    ~Counter();

    enum Name {
        local_read_lock_failed,
        local_write_lock_failed,
        local_lock_success,
        local_lock_waiting,
        remote_lock_failed,
        remote_lock_success,
        remote_lock_waiting,
        switch_aborts,
        read_commits,
        write_commits,
        __MAX
    };

    static constexpr std::array<std::string_view, __MAX> enum2str{
        "local_read_lock_failed",
        "local_write_lock_failed",
        "local_lock_success",
        "local_lock_waiting",
        "remote_lock_failed",
        "remote_lock_success",
        "remote_lock_waiting",
        "switch_aborts",
        "read_commits",
        "write_commits",
    };

    // std::atomic<uint64_t> counters[__MAX]{};
    std::array<uint64_t, __MAX> counters{};


#define __forceinline inline __attribute__((always_inline))

    __forceinline void incr(const Name name) {
        if constexpr (!(ENABLED_STATS & StatsBitmask::COUNTER)) {
            return;
        }
        auto& cntr = counters[name];
        // auto local = cntr.load();
        // ++local;
        // cntr.store(local, std::memory_order_relaxed);
        ++cntr;
    }

    __forceinline void incr(const Name name, auto amount) {
        if constexpr (!(ENABLED_STATS & StatsBitmask::COUNTER)) {
            return;
        }
        auto& cntr = counters[name];
        cntr += amount;
    }
};

} // namespace stats
