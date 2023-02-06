
#pragma once

#include <stddef.h>

#define N_UNIFIED_SLOTS 32768
#define SLOTS_PER_BLOCK 128
#define N_STAGES 5

#define ARR_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))

void handle_alloc(size_t tenant_id, size_t duration);
