
#include "handle.hpp"

#include <cstdlib>
#include <cstdio>
#include <bitset>
#include <cassert>

static constexpr size_t N_MICROS = 100000;
static constexpr size_t N_TENANTS = 16;

enum class state_e {
	ALLOC, /* init value */
	WAIT,
	RUN,
	FREE,
};
static const char* state_strings[] = {"ALLOC", "WAIT", "RUN", "FREE"};

struct state_t {
	state_e state;
	block_id_t blk;
	unsigned duration;
	unsigned wait;
	unsigned i_resume;
	unsigned i_alloc;
};

static size_t notify_list_len = 0;
static tenant_id_t notify_list[1+N_TENANTS];
static state_t state[1+N_TENANTS];

void print_states(size_t i) {
	printf("Time %lu:\n", i);
	for (size_t t = 1; t <= N_TENANTS; ++t) {
		printf("\tTenant %lu: state: %s, duration: %u, wait: %u\n", t, state_strings[static_cast<unsigned>(state[t].state)], state[t].duration, state[t].wait);
	}
}

static unsigned gen_noise() {
	return rand() % 3;
}

int main() {
	handle_init();

	for (size_t i = 0; i<N_MICROS; ++i) {
		print_states(i);
		notify_list_len = handle_try_ready(notify_list);
		std::bitset<1+N_TENANTS> ready_bitmap;
		for (size_t i = 0; i<notify_list_len; ++i) {
			ready_bitmap.set(notify_list[i]);
		}

		for (size_t t = 1; t<=N_TENANTS; ++t) {
			switch (state[t].state) {
				case state_e::ALLOC:
					state[t].duration = 10 * (1+ (rand() % 5));
					state[t].wait = 14 * (1 + (rand() % 5));
					state[t].blk = handle_alloc(t, state[t].wait, state[t].duration);
                    // printf("Tenant %lu received block %lu\n", t, state[t].blk);
					state[t].state = state_e::WAIT;
					state[t].i_alloc = i;
					break;
				case state_e::WAIT:
					if (ready_bitmap[t]) {
						// printf("Tenant %lu waited for %lu ticks, expected %u ticks.\n", t, i-state[t].i_alloc, state[t].wait);
						state[t].state = state_e::RUN;
						state[t].i_resume = std::max(i, (size_t) (state[t].i_alloc + state[t].wait)) + state[t].duration + gen_noise();
					}
					break;
				case state_e::RUN:
					if (i >= state[t].i_resume) {
						state[t].state = state_e::FREE;
					}
					break;
				case state_e::FREE:
					handle_free(t, state[t].blk);
					state[t].state = state_e::ALLOC;
					break;
				default:
					assert(false && "Unhandled case.\n");
			}
		}
	}
}
