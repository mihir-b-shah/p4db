
#ifndef CONSTS_H
#define CONSTS_H

#include <cstddef>

static constexpr size_t TXN_SIZE = 2;
static constexpr size_t N_STEPS = 100000;
static constexpr size_t N_NODES = 1;
static constexpr size_t N_THREADS = 1;
static constexpr size_t N_KEYS = 100000;
static constexpr size_t TXNS_PER_STEP = 2;
static constexpr size_t COORD_DELAY = 50;
static constexpr size_t PARTIC_DELAY = 50;
static constexpr size_t ABORT_DELAY = 200;
static constexpr size_t MAX_QUEUE_SIZE = 10;
static constexpr bool WAIT_LOCK = false;

#endif
