
#include "handle.hpp"

struct op_t {
	size_t tenant;
	uint64_t start_delay;
	uint64_t duration;
	size_t n_acquire;
};

#define N_MICROS 1000
#define N_TENANTS 40

static struct op_t ops[N_MICROS];

unsigned gen_noise() {
	return rand() % 3;
}

int main() {
	for (size_t t = 0; t<N_TENANTS; ++t) {
		unsigned duration = 10 * (1+ (rand() % 5));
		unsigned wait = 14 * (1 + (rand() % 5));
		unsigned n_acquire = 1 + (rand() % 2);

		size_t i = t;
		ops[i] = (struct op_t) {.tenant = t, .start_delay = 0, .duration = duration + gen_noise(), .n_acquire = n_acquire};
		i += duration + gen_noise();

		while (i < N_MICROS) {
			unsigned d_noise = duration + gen_noise();
			unsigned w_noise = wait + gen_noise();

			struct op_t op = (struct op_t) {.tenant = t, .start_delay = wait, .duration = duration, .n_acquire = n_acquire};
			while (ops[i].tenant != NO_TENANT) {
				i += 1;
			}
			if (i >= N_MICROS) {
				break;
			}
			ops[i] = op;
			i += d_noise + w_noise;
		}
	}

	for (size_t i = 0; i<N_MICROS; ++i) {
		struct op_t op = ops[i];
		if (op.tenant != NO_TENANT) {
			handle_free(op.tenant);
			size_t r = handle_alloc(op.tenant, op.start_delay, op.duration, op.n_acquire);
			printf("tenant %lu tried to acquire %lu, got %lu\n", op.tenant, op.n_acquire, op.n_acquire-r);
		}
	}
}
