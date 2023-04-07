
#pragma once

#include <pthread.h>

typedef void (*serial_func)(void*);

/*	A reusable barrier impl, https://github.com/stephentu/silo/blob/master/spinbarrier.h */

static void empty_serial_func(void* arg) {}

class reusable_barrier_t {
public:
	reusable_barrier_t(size_t n_threads, serial_func sf) : sf(sf) {
		pthread_barrier_init(&bar1, NULL, n_threads);
		pthread_barrier_init(&bar2, NULL, n_threads);
	}
	reusable_barrier_t(size_t n_threads) : reusable_barrier_t(n_threads, empty_serial_func) {}

	void wait(void* arg) {
		int rc1 = pthread_barrier_wait(&bar1);
		if (rc1 == PTHREAD_BARRIER_SERIAL_THREAD) {
			sf(arg);
		} else {
			assert(rc1 == 0);
		}
		int rc2 = pthread_barrier_wait(&bar2);
		assert(rc2 == PTHREAD_BARRIER_SERIAL_THREAD || rc2 == 0);
	}

private:
	pthread_barrier_t bar1;
	pthread_barrier_t bar2;
	serial_func sf;
};
