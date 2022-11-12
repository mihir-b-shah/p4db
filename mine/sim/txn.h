
#ifndef STRUCTS_H
#define STRUCTS_H

#include "consts.h"

typedef size_t key_t;

/*
How to execute:
1) Iterate ops in order and "acquire locks"
2) 


*/
struct txn_t {
	key_t ops[TXN_SIZE];

	txn_t();
};

#endif
