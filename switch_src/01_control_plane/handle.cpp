
#include "handle.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <set>
#include <queue>
#include <algorithm>

#define ASSERT_ON
#ifdef ASSERT_ON
#define ASSERT(c) assert((c))
#else
#define ASSERT(c) ;
#endif

static constexpr size_t N_UNIFIED_SLOTS = 32768;
static constexpr size_t SLOTS_PER_BLOCK = 128;
static constexpr size_t N_BLOCKS = N_UNIFIED_SLOTS / SLOTS_PER_BLOCK;
static constexpr size_t N_STAGES = 5;
static constexpr size_t N_MAX_TENANTS = 150;
static constexpr tenant_id_t NO_TENANT = 0;
static constexpr ts_t INIT_TS = 0;

struct blk_meta_t {
	block_id_t blk_id;
	ts_t est_finish_ts;

	blk_meta_t(block_id_t blk_id, ts_t ts) : blk_id(blk_id), est_finish_ts(ts) {}
};

struct blk_meta_g_cmp_t {
	// this is a total ordering.
	bool operator()(const blk_meta_t& lhs, const blk_meta_t& rhs) const {
		if (lhs.est_finish_ts == rhs.est_finish_ts) {
			return lhs.blk_id > rhs.blk_id;
		} else {
			return lhs.est_finish_ts > rhs.est_finish_ts;
		}
	}
};

class tenant_dist_info_t {
public:
	tenant_dist_info_t(tenant_id_t id) : id(id) {
		// TODO: fix this number.
		want_blocks = 4;
	}

	bool want_more(size_t curr_blocks, ts_t est_wait) {
		// TODO: based on a id-based distribution.
		if (est_wait == 0 && curr_blocks < want_blocks) {
			return true;
		} else {
			return false;
		}
	}

private:
	tenant_id_t id;
	size_t want_blocks;
};

struct tenant_reqs_t {
	bool started;
	size_t n_waiting;

	tenant_reqs_t() : started(false), n_waiting(0) {}
};

/*	TODO: For large variances in start_delay, this can cause utilization problems.
	Use a std::set instead. See the Todo doc earlier for our old code. 
	We want to binary search in the set for the finish time closest to our start time.
	Then, create an optimal mix of stuff to the left (i.e. finishing before, resulting in 0
	wait time but wasting space on switch), and right (finishing after, but denying me
	switch space). */
// static std::set<blk_meta_t, blk_meta_cmp_t> blocks_sorted;
static std::priority_queue<blk_meta_t, std::vector<blk_meta_t>, blk_meta_g_cmp_t> blocks_sorted;
static std::queue<tenant_id_t> block_queue[N_BLOCKS];
static std::vector<tenant_dist_info_t> tenant_info;
static tenant_reqs_t tenant_req[N_MAX_TENANTS];
static std::vector<tenant_id_t> to_notify;
static std::vector<blk_meta_t> deq_buf;

static ts_t get_real_ts() {
	struct timespec ts;
	ASSERT(clock_gettime(CLOCK_BOOTTIME, &ts) == 0);
	return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

void handle_init() {
	for (size_t i = 0; i<N_BLOCKS; ++i) {
		blocks_sorted.emplace(i, INIT_TS);
	}
	tenant_info.reserve(N_MAX_TENANTS);
	for (size_t i = 0; i<N_MAX_TENANTS; ++i) {
		tenant_info.emplace_back(i);
	}
	deq_buf.reserve(N_BLOCKS);
}

size_t handle_alloc(size_t tenant_id, size_t start_delay, size_t duration, block_id_t* my_blocks) {
	ASSERT(tenant_id != NO_TENANT);
	ASSERT(blocks_sorted.size() == N_BLOCKS);
	tenant_req[tenant_id].started = true;

	ts_t usecs_ts = get_real_ts();
	ts_t my_start_ts = usecs_ts + start_delay;
	ts_t my_finish_ts = usecs_ts + start_delay + duration;

	// search for blocks.	
	size_t n_acquired = 0;
	size_t max_finish_time = my_finish_ts;
	size_t n_wait = 0;

	while (blocks_sorted.size() > 0) {
		const blk_meta_t& meta = blocks_sorted.top();

		size_t est_wait_time = meta.est_finish_ts >= my_start_ts ? meta.est_finish_ts - my_start_ts : 0;
		if (!tenant_info[tenant_id].want_more(n_acquired, est_wait_time)) {
			break;
		}
		max_finish_time = std::max(max_finish_time, meta.est_finish_ts + duration);
		deq_buf.push_back(meta);
		block_id_t blk_id = meta.blk_id;
		if (!block_queue[blk_id].empty()) {
			n_wait += 1;
		}
		block_queue[blk_id].push(tenant_id);
		my_blocks[n_acquired] = blk_id;
		n_acquired += 1;

		blocks_sorted.pop();
	}

	for (size_t i = 0; i<deq_buf.size(); ++i) {
		deq_buf[i].est_finish_ts = max_finish_time;
		blocks_sorted.push(deq_buf[i]);
	}
	deq_buf.clear();

	ASSERT(tenant_req[tenant_id].n_waiting == 0);
	tenant_req[tenant_id].n_waiting = n_wait;
	if (n_wait == 0) {
		to_notify.push_back(tenant_id);
	}
	return n_acquired;
}

void handle_free(size_t tenant_id, size_t n_blocks, block_id_t* my_blocks) {
	ASSERT(tenant_id != NO_TENANT);

	for (size_t i = 0; i<n_blocks; ++i) {
		ASSERT(block_queue[my_blocks[i]].front() == tenant_id);
		block_queue[my_blocks[i]].pop();
		if (block_queue[my_blocks[i]].size() > 0) {
			tenant_id_t next = block_queue[my_blocks[i]].front();
			tenant_req[next].n_waiting -= 1;
			if (tenant_req[next].n_waiting == 0) {
				// this tenant is ready to go for real, send him a message.
				to_notify.push_back(next);
			}
		}
	}
}

size_t handle_try_ready(tenant_id_t* notify_list_fill) {
	size_t notify_v_size = to_notify.size();
	for (size_t i = 0; i<notify_v_size; ++i) {
		notify_list_fill[i] = to_notify[i];
	}
	to_notify.clear();
	return notify_v_size;
}
