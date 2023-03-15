
#pragma once

#include <pthread.h>

typedef void* (*serial_func)(void*);

/*	A spinning reusable barrier impl, since pthread_barrier_t is NOT reusable.
	TODO: optimize by rolling my own set of two counters if necessary, right now just 2 barriers.
	https://github.com/stephentu/silo/blob/master/spinbarrier.h
	TODO: P4DB's distributed barrier is also a 2-phase barrier, but is potentially wrong? Need to check */

static void* empty_serial_func(void* arg) {
	return arg;
}

class reusable_barrier_t {
public:
	reusable_barrier_t(size_t n_threads, serial_func sf) : sf(sf), res(NULL) {
		pthread_barrier_init(&bar1, NULL, n_threads);
		pthread_barrier_init(&bar2, NULL, n_threads);
	}
	reusable_barrier_t(size_t n_threads) : reusable_barrier_t(n_threads, empty_serial_func) {}

	void* wait(void* arg) {
		int rc1 = pthread_barrier_wait(&bar1);
		if (rc1 == PTHREAD_BARRIER_SERIAL_THREAD) {
			//	TODO: Is seq cst necessary here? I don't think so...
			__atomic_store_n(&res, sf(arg), __ATOMIC_SEQ_CST);
		} else {
			assert(rc1 == 0);
		}
		int rc2 = pthread_barrier_wait(&bar2);
		assert(rc2 == PTHREAD_BARRIER_SERIAL_THREAD || rc2 == 0);
		// everyone should read the new value...
		return __atomic_load_n(&res, __ATOMIC_SEQ_CST);
	}

private:
	pthread_barrier_t bar1;
	pthread_barrier_t bar2;
	serial_func sf;
	void* res;
};
