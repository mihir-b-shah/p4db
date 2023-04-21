
#pragma once

#include <pthread.h>

typedef void (*middle_func)(void*);

/*	A reusable barrier impl:
    https://www.cs.cmu.edu/afs/cs/academic/class/15740-s18/www/lectures/10-11-synchronization.pdf */

//  TODO programatically determine this?
static constexpr size_t CACHE_LINE_BYTES = 64;

class reusable_barrier_t {
public:
	reusable_barrier_t(size_t n_threads, middle_func mf, bool single) : enter_ct(0), exit_ct(0), n_threads(n_threads), mf(mf), single(single) {}

	void wait(void* arg) {
        //  Start with this, optimize later
        __sync_synchronize();

        if (__atomic_add_fetch(&enter_ct, 1, __ATOMIC_SEQ_CST) == n_threads) {
            mf(arg);
            enter_ct = 0;
        } else {
            while (enter_ct != 0) {
                __builtin_ia32_pause();
            }
        }

        if (__atomic_add_fetch(&exit_ct, 1, __ATOMIC_SEQ_CST) == n_threads) {
            exit_ct = 0;
        } else {
            while (exit_ct != 0) {
                __builtin_ia32_pause();
            }
        }

        __sync_synchronize();
	}

private:
    alignas(CACHE_LINE_BYTES) uint32_t enter_ct;
    alignas (CACHE_LINE_BYTES) uint32_t exit_ct;
    uint32_t n_threads;
	middle_func mf;
    bool single;
};

/*
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
*/
