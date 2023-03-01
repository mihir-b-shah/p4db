
#include <pthread.h>

/*	A spinning reusable barrier impl, since pthread_barrier_t is NOT reusable.
	TODO: optimize by rolling my own set of two counters if necessary, right now just 2 barriers.
	https://github.com/stephentu/silo/blob/master/spinbarrier.h
	P4DB's distributed barrier is also a 2-phase barrier, but is potentially wrong? Need to check */

template <typename CriticalF>
class reusable_barrier_t {
public:
	reusable_barrier_t(size_t n_threads, const CriticalF& critical_func) : critical_func(critical_func) {
		pthread_barrier_init(&bar1, NULL, n_threads);
		pthread_barrier_init(&bar2, NULL, n_threads);
	}

	void wait() {
		int rc1 = pthread_barrier_wait(&bar1);
		if (rc1 == PTHREAD_BARRIER_SERIAL_THREAD) {
			critical_func();
		} else {
			assert(rc1 == 0);
		}
		int rc2 = pthread_barrier_wait(&bar2);
		assert(rc2 == PTHREAD_BARRIER_SERIAL_THREAD || rc2 == 0);
	}

private:
	pthread_barrier_t bar1;
	pthread_barrier_t bar2;
	const CriticalF& critical_func;
};
