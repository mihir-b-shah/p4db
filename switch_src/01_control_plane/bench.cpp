
#include "handle.hpp"

#include <cstddef>
#include <cstdio>

int main() {
	handle_init();
	size_t blocks_len = 0;
	block_id_t blocks[5];
	for (size_t i = 0; i<50000000; ++i) {
		blocks_len = handle_alloc(1, 20, 10, blocks);
		handle_free(1, blocks_len, blocks);
	}
	return 0;
}
