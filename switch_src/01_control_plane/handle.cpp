
#include "handle.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <set>
#include <queue>
#include <algorithm>

// Have arenas of blocks
static constexpr size_t N_UNIFIED_SLOTS = 2048;
static constexpr size_t SLOTS_PER_BLOCK = 8;
static constexpr size_t N_BLOCKS = N_UNIFIED_SLOTS / SLOTS_PER_BLOCK;
static constexpr size_t N_MAX_TENANTS = 150;
static constexpr tenant_id_t NO_TENANT = 0;
static constexpr ts_t INIT_TS = 0;

struct blk_meta_t {
	block_id_t blk_id;
	ts_t est_finish_ts;

	blk_meta_t(block_id_t blk_id, ts_t ts) : blk_id(blk_id), est_finish_ts(ts) {}
};

struct blk_meta_cmp_t {
	// this is a total ordering.
	bool operator()(const blk_meta_t& lhs, const blk_meta_t& rhs) const {
		if (lhs.est_finish_ts == rhs.est_finish_ts) {
			return lhs.blk_id < rhs.blk_id;
		} else {
			return lhs.est_finish_ts < rhs.est_finish_ts;
		}
	}
};

struct tenant_reqs_t {
	bool started;
	size_t n_waiting;

	tenant_reqs_t() : started(false), n_waiting(0) {}
};

static std::set<blk_meta_t, blk_meta_cmp_t> blocks_sorted;
static std::queue<tenant_id_t> block_queue[N_BLOCKS];
static tenant_reqs_t tenant_req[N_MAX_TENANTS];
static std::unordered_set<tenant_id_t> to_notify;

static ts_t get_real_ts() {
	struct timespec ts;
	int rc = clock_gettime(CLOCK_BOOTTIME, &ts);
    assert(rc == 0);
	return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

void handle_init() {
	for (size_t i = 0; i<N_BLOCKS; ++i) {
		blocks_sorted.emplace(i, INIT_TS);
	}
}

block_id_t handle_alloc(size_t tenant_id, size_t start_delay, size_t duration) {
    /*
    for (auto it = blocks_sorted.begin(); it != blocks_sorted.end(); ++it) {
        printf("(%lu,%lu) ", it->blk_id, it->est_finish_ts);
    }
    printf("\n");
    */

	assert(tenant_id != NO_TENANT);
	assert(blocks_sorted.size() == N_BLOCKS);
	tenant_req[tenant_id].started = true;

	ts_t usecs_ts = get_real_ts();
	ts_t my_start_ts = usecs_ts + start_delay;
	ts_t my_finish_ts = usecs_ts + start_delay + duration;

    blk_meta_t query(0, my_start_ts);
    auto it = blocks_sorted.upper_bound(query);
    if (it != blocks_sorted.begin()) {
        it--;
    }

    blk_meta_t found = *it;
    blocks_sorted.erase(it);
    found.est_finish_ts = my_finish_ts + (found.est_finish_ts >= my_start_ts 
        ? found.est_finish_ts - my_start_ts : 0);

	assert(tenant_req[tenant_id].n_waiting == 0);
    if (!block_queue[found.blk_id].empty()) {
        tenant_req[tenant_id].n_waiting = 1;
    } else {
		to_notify.insert(tenant_id);
    }
    block_queue[found.blk_id].push(tenant_id);
    blocks_sorted.insert(found);
	return found.blk_id;
}

void handle_free(size_t tenant_id, block_id_t block) {
	assert(tenant_id != NO_TENANT);
    assert(block_queue[block].front() == tenant_id);
    block_queue[block].pop();
    if (block_queue[block].size() > 0) {
        tenant_id_t next = block_queue[block].front();
        tenant_req[next].n_waiting -= 1;
        if (tenant_req[next].n_waiting == 0) {
            // this tenant is ready to go for real, send him a message.
            to_notify.insert(next);
        }
    }
}

std::unordered_set<tenant_id_t>& get_ready() {
    return to_notify;
}
