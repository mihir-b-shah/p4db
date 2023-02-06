
#include "handle.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <set>
#include <queue>

struct blk_meta_t {
	block_id_t blk_id;
	ts_t est_finish_ts;

	blk_meta_t(block_id_t blk_id, ts_t ts) : blk_id(blk_id), est_finish_ts(ts) {}
};

struct blk_meta_cmp_t {
	bool operator()(const blk_meta_t& lhs, const blk_meta_t& rhs) const {
		if (lhs.est_finish_ts == rhs.est_finish_ts) {
			return lhs.blk_id < rhs.blk_id;
		} else {
			return lhs.est_finish_ts < rhs.est_finish_ts;
		}
	}
};

class tenant_dist_info_t {
public:
	tenant_info_t(tenant_id_t id) : id(id) {
		want_blocks = 3; // TODO fix.
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

static std::set<blk_meta_t, blk_meta_cmp_t> blocks_sorted;
static std::queue<tenant_id> block_queue[N_BLOCKS];
static std::vector<tenant_dist_info_t> tenant_info;
static tenant_reqs_t tenant_req[N_TENANTS];
static std::vector<tenant_id_t> to_notify;

static ts_t get_real_ts() {
	struct timespec ts;
	assert(clock_gettime(CLOCK_BOOTTIME, &ts) == 0);
	return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

__attribute__((constructor))
static void init_structures() {
	for (size_t i = 0; i<N_BLOCKS; ++i) {
		blocks_sorted.emplace(i, 0);
	}
	tenant_info.reserve(N_TENANTS);
	for (size_t i = 0; i<N_TENANTS; ++i) {
		tenant_info.emplace_back(i);
	}
}

size_t handle_alloc(size_t tenant_id, size_t start_delay, size_t duration, block_id_t* my_blocks) {
	assert(tenant_id != NO_TENANT);
	tenant_req[tenant_id].started = true;

	ts_t usecs_ts = get_real_ts();
	ts_t my_start_ts = usecs_ts + start_delay;
	ts_t my_finish_ts = usecs_ts + start_delay + duration;

	size_t n_acquired = 0;
	struct blk_meta_t dummy_key(N_BLOCKS, my_start_ts);	
	auto blk_set_it = blocks_sorted.upper_bound(dummy_key);

	while (tenant_info[tenant_id].want_more(n_acquired, 0) && blk_set_it != blocks_sorted.begin()) {
		// our timestamp will go forward, so this is safe.
		auto curr_it = blk_set_it--;
		block_id_t block_id = curr_it->block_id;

		block_queue[block_id].push(tenant_id);
		auto node_handle = blocks_sorted.extract(curr_it);
		node_handle.value().est_finish_ts = my_finish_ts;
		blocks_sorted.insert(std::move(node_handle));

		my_blocks[n_acquired++] = block_id;
	}

	assert(tenant_req[tenant_id].n_waiting == 0);
	tenant_req[tenant_id].n_waiting = n_acquired;
	return n_acquired;
}

void handle_free(size_t tenant_id, size_t n_blocks, block_id_t* my_blocks) {
	assert(tenant_id != NO_TENANT);

	for (size_t i = 0; i<n_blocks; ++i) {
		assert(block_queues[my_blocks[i]].front() == tenant_id);
		block_queues[my_blocks[i]].pop();
		if (block_queues[my_blocks[i]].size() > 0) {
			tenant_id_t next = block_queues[my_blocks[i]].front();
			tenant_req[next] -= 1;
			if (tenant_req[next] == 0) {
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
