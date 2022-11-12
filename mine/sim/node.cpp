
#include "consts.h"
#include "txn.h"
#include "node.h"

size_t get_coord(const txn_t& txn) {
	unsigned ctrs[N_NODES];
	for (size_t i = 0; i<TXN_SIZE; ++i) {
		ctrs[node_for_key(txn.ops[i].key)] += 1;
	}
	size_t best = 0;
	for (size_t i = 1; i<N_NODES; ++i) {
		if (ctrs[i] > ctrs[best]) {
			best = i;
		}
	}
	return best;
}

