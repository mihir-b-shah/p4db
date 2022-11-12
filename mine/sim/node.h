
#ifndef NODE_H
#define NODE_H

#include <unordered_set>

static inline size_t node_for_key(key_t k) {
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

	txn_wrap_t(txn t);
};

struct nthread_t {
	exec_stage_e state;
	txn_wrap_t work;
	/* Some misc state */
	size_t lock_acq_prog;
	size_t ready_ct;
	bool commit;
	size_t wait_time;
	node_t* node;

	nthread_t() : state(IDLE) {}
	nthread_t(node_t* n) : state(IDLE), node(n) {}
};

void nthread_step(nthread& nthr);

struct node_t {
	size_t id;
	std::queue<txn_wrap_t> tq;
	nthread_t thrs[N_THREADS];
	std::unordered_set<key_t> locks; // lock queue is represented by thread order

	node_t(size_t id) : id(id) {
		for (size_t i = 0; i<N_THREADS; ++i) {
			thrs[i].node = this;
		}
	}
}

#endif
