
#ifndef NODE_H
#define NODE_H

static inline size_t node_for_key(key_t k) {
	return k % N_NODES;
}

size_t get_coord(const txn_t& txn);

struct nthread_t {
	bool busy;
	txn_t work;

	nthread_t() : busy(false) {}
};

struct node_t {
	size_t id;
	std::queue<txn_t> tq;
	nthread_t thrs[N_THREADS];

	node_t(size_t id) : id(id) {}
}

#endif
