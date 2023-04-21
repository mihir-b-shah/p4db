
#include "util.hpp"

#include <numeric>
#include <main/config.hpp>
#include <utils/context.hpp>

/*  Run only on a single numa socket, cores 0-n_cores-1. No hyperthreads */
void pin_worker(uint32_t core) {
	// TODO: remove when we run on multiple machines, for real.
    WorkerContext::get().tid = core % Config::instance().num_txn_workers;

    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(2*core, &mask);
    
    pthread_t pid = pthread_self();
    int rc = pthread_setaffinity_np(pid, sizeof(cpu_set_t), &mask);
    assert(rc == 0);
}


// formatting of bytes

std::string stringifyFraction(const uint64_t num, const unsigned den, const unsigned precision) {
    constexpr unsigned base = 10;

    // prevent division by zero if necessary
    if (den == 0) {
        return "inf";
    }

    // integral part can be computed using regular division
    std::string result = std::to_string(num / den);

    // perform first step of long division
    // also cancel early if there is no fractional part
    unsigned tmp = num % den;
    if (tmp == 0 || precision == 0) {
        return result;
    }

    // reserve characters to avoid unnecessary re-allocation
    result.reserve(result.size() + precision + 1);

    // fractional part can be computed using long divison
    result += '.';
    for (size_t i = 0; i < precision; ++i) {
        tmp *= base;
        char nextDigit = '0' + static_cast<char>(tmp / den);
        result.push_back(nextDigit);
        tmp %= den;
    }

    return result;
}
