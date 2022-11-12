
#ifndef STRUCTS_H
#define STRUCTS_H

#include "consts.h"

typedef size_t key_t;

struct txn_t {
	key_t ops[TXN_SIZE];

	txn_t();
};

#endif
