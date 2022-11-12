
#include <cstdlib>

#include "txn.h"
#include "node.h"

static db_key_t rand_key() {
	size_t low32 = rand() & 0xffffffffU;
	size_t high32 = rand() % 0xffffffffU;
	return ((high32 << 32) | low32) % N_KEYS;
}

txn_t::txn_t() {
	unsigned bloom = 0;
	for (size_t i = 0; i<TXN_SIZE; ++i) {
		db_key_t k;
		do {
			k = rand_key();
		} while (bloom & (1 << (k % 32)));
		bloom |= 1 << (k % 32);
		ops[i] = k;
	}
}
