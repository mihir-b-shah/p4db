
#include "handle.hpp"

#include <cstdlib>
#include <cstdio>
#include <bitset>
#include <cassert>

static constexpr size_t N_MICROS = 1000;
static constexpr size_t N_TENANTS = 40;

enum class state_e {
	ALLOC, /* init value */
	WAIT,
	RUN,
	FREE,
};
struct state_t {
	state_e state;
	block_id_t blks[5];
	size_t blk_len;
	unsigned duration;
	unsigned wait;
	unsigned i_resume;
};

static size_t notify_list_len = 0;
static tenant_id_t notify_list[N_TENANTS];
static state_t state[N_TENANTS];

static unsigned gen_noise() {
	return rand() % 3;
}

int main() {
	handle_init();

	for (size_t i = 0; i<N_MICROS; ++i) {
		notify_list_len = handle_try_ready(notify_list);
		std::bitset<N_TENANTS> ready_bitmap;
		for (size_t i = 0; i<notify_list_len; ++i) {
			ready_bitmap.set(notify_list[i]);
		}

		for (size_t t = 1; t<N_TENANTS; ++t) {
			switch (state[t].state) {
				case state_e::ALLOC:
					state[t].duration = 10 * (1+ (rand() % 5));
					state[t].wait = 14 * (1 + (rand() % 5));
					state[t].blk_len = handle_alloc(
						t, state[t].wait, state[t].duration, &state[t].blks[0]);
					state[t].state = state_e::WAIT;
					break;
				case state_e::WAIT:
					if (ready_bitmap[t]) {
						state[t].state = state_e::RUN;
						state[t].i_resume = i + state[t].duration + gen_noise();
					}
					break;
				case state_e::RUN:
					if (i >= state[t].i_resume) {
						state[t].state = state_e::FREE;
					}
					break;
				case state_e::FREE:
					handle_free(t, state[t].blk_len, &state[t].blks[0]);
					state[t].state = state_e::ALLOC;
					break;
				default:
					assert(false && "Unhandled case.\n");
			}
		}
	}
}
