
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#define N_UNIFIED_SLOTS 32768
#define SLOTS_PER_BLOCK 128
#define N_STAGES 5
#define ARR_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))
#define NO_TENANT 0

/*	Note these are all time estimates.
	Ideas to optimize this, if need be:
	1)	A smarter data structure (maybe a ring buffer)
	2)	Coarser granularity blocks
	3)	SSE instructions for checking the blocks, and quantizing the block
		timestamps to 2-byte, with a base offset. 
	Probably not necessary to optimize. I can do 4M alloc/free ops/s on cpu. */

struct blk_meta_t {
	size_t owner_id;
	uint64_t est_finish_ts;
};

static struct blk_meta_t blk_meta[N_UNIFIED_SLOTS / SLOTS_PER_BLOCK];

static uint64_t get_real_usecs() {
	struct timespec ts;
	assert(clock_gettime(CLOCK_REALTIME, &ts) == 0);
	return (((uint64_t) ts.tv_sec) * 1000000) + (ts.tv_nsec / 1000);
}

__attribute__((constructor))
static void init_structures() {
	for (size_t i = 0; i<ARR_SIZE(blk_meta); ++i) {
		blk_meta[i].owner_id = NO_TENANT;
	}
}

// returns how many allocs I wasn't able to do.
size_t handle_alloc(size_t tenant_id, size_t start_delay, size_t duration, size_t n_acquire) {
	assert(tenant_id != NO_TENANT);

	uint64_t usecs_ts = get_real_usecs();
	uint64_t my_start_ts = usecs_ts + start_delay;
	uint64_t my_finish_ts = usecs_ts + start_delay + duration;

	for (size_t i = 0; i<ARR_SIZE(blk_meta); ++i) {
		if (n_acquire == 0) {
			break;
		}
		if (blk_meta[i].owner_id == NO_TENANT || my_start_ts > blk_meta[i].est_finish_ts) {
			n_acquire -= 1;
			blk_meta[i].owner_id = tenant_id;
			blk_meta[i].est_finish_ts = my_finish_ts;
		}
	}

	return n_acquire;
}

void handle_free(size_t tenant_id) {
	assert(tenant_id != NO_TENANT);
	for (size_t i = 0; i<ARR_SIZE(blk_meta); ++i) {
		if (blk_meta[i].owner_id == tenant_id) {
			blk_meta[i].owner_id = NO_TENANT;
		}
	}
}

struct op_t {
	size_t tenant;
	uint64_t start_delay;
	uint64_t duration;
	size_t n_acquire;
};

#define N_MICROS 1000
#define N_TENANTS 100

static struct op_t ops[N_MICROS];

unsigned gen_noise() {
	return rand() % 10;
}

int main() {
	for (size_t t = 0; t<N_TENANTS; ++t) {
		unsigned duration = 50 * (1+ (rand() % 5));
		unsigned wait = 70 * (1 + (rand() % 5));
		unsigned n_acquire = 1 + (rand() % 2);

		size_t i = t;
		ops[i] = (struct op_t) {.tenant = t, .start_delay = 0, .duration = duration + gen_noise(), .n_acquire = n_acquire};
		i += duration + gen_noise();

		while (i < N_MICROS) {
			unsigned d_noise = duration + gen_noise();
			unsigned w_noise = wait + gen_noise();

			struct op_t op = (struct op_t) {.tenant = t, .start_delay = wait, .duration = duration, .n_acquire = n_acquire};
			while (ops[i].tenant != NO_TENANT) {
				i += 1;
			}
			if (i >= N_MICROS) {
				break;
			}
			ops[i] = op;
			i += d_noise + w_noise;
		}
	}

	for (size_t i = 0; i<N_MICROS; ++i) {
		struct op_t op = ops[i];
		if (op.tenant != NO_TENANT) {
			handle_free(op.tenant);
			handle_alloc(op.tenant, op.start_delay, op.duration, op.n_acquire);
		}
	}
}
