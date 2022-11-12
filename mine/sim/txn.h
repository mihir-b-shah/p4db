
#ifndef STRUCTS_H
#define STRUCTS_H

#include "consts.h"

typedef size_t db_key_t;

struct txn_t {
	db_key_t ops[TXN_SIZE];

	txn_t();
};

#endif
