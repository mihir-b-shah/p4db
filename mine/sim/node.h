
#ifndef NODE_H
#define NODE_H

#include <utility>
#include <vector>
#include <queue>
#include <unordered_set>
#include <unordered_map>

static inline size_t node_for_key(db_key_t k) {
	return k % N_NODES;
}

size_t get_coord(const txn_t& txn);

enum exec_stage_e {
	STG_IDLE,
	STG_COORD_ACQ,
	STG_PREPARE,
	STG_READY,
	STG_COMMIT,
	STG_PARTIC_ACQ,
};

struct node_t;

struct txn_wrap_t {
	txn_t t;
	size_t coord;
	size_t node_mask;
	size_t thrs[N_NODES];

	txn_wrap_t() {}
	txn_wrap_t(txn_t t);
};

struct nthread_t {
	size_t id;
	exec_stage_e state;
	txn_wrap_t work;
	/* Some misc state */
	size_t lock_acq_prog;
	size_t ready_ct;
	bool commit;
	size_t wait_time;
	node_t* node;

	void reset() {
		state = STG_IDLE;
		lock_acq_prog = 0;
		ready_ct = 0;
		commit = false;
		wait_time = 0;
	}

	nthread_t() {
		reset();
		node = nullptr;
	}
	
	nthread_t(node_t* n) : node(n) {
		reset();
	}
};

struct node_t {
	size_t id;
	std::queue<txn_wrap_t> tq;
	nthread_t thrs[N_THREADS];
	// map from key to txn id of holder
	std::unordered_map<db_key_t, size_t> locks;

	node_t(size_t id) : id(id) {
		for (size_t i = 0; i<N_THREADS; ++i) {
			thrs[i].id = i;
			thrs[i].node = this;
		}
	}
};

struct system_t {
	std::vector<node_t> nodes;
	std::unordered_set<size_t> aborted;
	std::queue<std::pair<size_t, txn_wrap_t>> retry;

	system_t() {
		nodes.reserve(N_NODES);
		for (size_t i = 0; i<N_NODES; ++i) {
			nodes.emplace_back(i);
		}
	}
};

void nthread_step(size_t s, nthread_t& nthr, system_t& sys);


#endif
