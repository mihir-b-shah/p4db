
#pragma once

#include <cstdint>
#include <cstddef>
#include <unordered_set>

typedef size_t tenant_id_t;
typedef size_t block_id_t;
typedef uint64_t ts_t;

void handle_init();
block_id_t handle_alloc(size_t tenant_id, size_t start_delay, size_t duration);
void handle_free(size_t tenant_id, block_id_t block);
std::unordered_set<tenant_id_t>& get_ready();
