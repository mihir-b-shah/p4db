
#include <cstdlib>

#include "txn.h"
#include "node.h"

static db_key_t rand_key() {
	size_t low32 = rand() & 0xffffffffU;
	size_t high32 = rand() % 0xffffffffU;
	return ((high32 << 32) | low32) % N_KEYS;
}

txn_t::txn_t() {
	for (size_t i = 0; i<TXN_SIZE; ++i) {
		ops[i] = rand_key();
	}
}
