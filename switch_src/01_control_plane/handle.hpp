
#pragma once

#include <cstdint>
#include <cstddef>

typedef size_t tenant_id_t;
typedef size_t block_id_t;
typedef uint64_t ts_t;

void handle_init();
size_t handle_alloc(size_t tenant_id, size_t start_delay, size_t duration, block_id_t* my_blocks);
void handle_free(size_t tenant_id, size_t n_blocks, block_id_t* my_blocks);
size_t handle_try_ready(tenant_id_t* notify_list_fill);
