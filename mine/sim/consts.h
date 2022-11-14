
#ifndef CONSTS_H
#define CONSTS_H

#include <cstddef>

static constexpr size_t TXN_SIZE = 4;
static constexpr size_t N_STEPS = 10000;
static constexpr size_t N_NODES = 2;
static constexpr size_t N_THREADS = 32;
static constexpr size_t N_KEYS = 1000000000;
static constexpr size_t TXNS_PER_STEP = 3;
static constexpr size_t NETWORK_DELAY = 1;
static constexpr size_t ABORT_DELAY = 5;
static constexpr size_t MAX_QUEUE_SIZE = 5;
static constexpr bool WAIT_LOCK = false;

#endif
