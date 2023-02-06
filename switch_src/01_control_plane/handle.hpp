
#pragma once

#include <cstdint>
#include <cstddef>

typedef size_t tenant_id_t;
typedef size_t block_id_t;
typedef uint64_t ts_t;

static constexpr size_t N_UNIFIED_SLOTS = 32768;
static constexpr size_t SLOTS_PER_BLOCK = 128;
static constexpr size_t N_BLOCKS = N_UNIFIED_SLOTS / SLOTS_PER_BLOCK;
static constexpr size_t N_STAGES = 5;
static constexpr size_t N_MAX_TENANTS = 100;
static constexpr tenant_id_t NO_TENANT = 0;

size_t handle_alloc(size_t tenant_id, size_t start_delay, size_t duration, block_id_t* my_blocks);
void handle_free(size_t tenant_id, size_t n_blocks, block_id_t* my_blocks);
size_t handle_try_ready(tenant_id_t* notify_list_fill);
