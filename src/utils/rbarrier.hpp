
#pragma once

#include <pthread.h>

typedef void (*middle_func)(void*);

/*	A reusable barrier impl, https://github.com/stephentu/silo/blob/master/spinbarrier.h */

class reusable_barrier_t {
public:
	reusable_barrier_t(size_t n_threads, middle_func mf, bool single) : mf(mf), single(single) {
		pthread_barrier_init(&bar1, NULL, n_threads);
		pthread_barrier_init(&bar2, NULL, n_threads);
	}

	void wait(void* arg) {
		int rc1 = pthread_barrier_wait(&bar1);
		if (!single || rc1 == PTHREAD_BARRIER_SERIAL_THREAD) {
			mf(arg);
		} else {
			assert(rc1 == 0);
		}
		int rc2 = pthread_barrier_wait(&bar2);
		assert(rc2 == PTHREAD_BARRIER_SERIAL_THREAD || rc2 == 0);
	}

private:
	pthread_barrier_t bar1;
	pthread_barrier_t bar2;
	middle_func mf;
    bool single;
};
