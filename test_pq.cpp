
#include <tbb/concurrent_priority_queue.h>
#include <utility>
#include <pthread.h>
/*	TODO:
1)	Maybe we don't need the full functionality of a pq?
2)	Ask Dr. Price in OH about solutions to boxes-of-donuts.
3)	There are lots of keys- 30k- but only ~86 frequencies.
4)	Can we tolerate an approximate solution.
5)	Could tbb::concurrent_priority_queue help?

A possible solution:
- observe as an approximate solution, we don't need a full pq.
- the intuition for why picking the hottest item helps is b/c there is a
  bimodal distribution, of sorts. Then, the reason picking cold items to
  fill a batch is bad, quickly we'll run out of the tail, and then be limited
  by the # of hot keys.
- instead, pick hot keys first (and use cold stuff as necessary) to
  fill out batches.
- the unordered_map representing our buckets can be indirected to point onto
  a vector sorted in descending popularity of key.
- then just split that vector into two chunks- one high popularity, one low-
  of equal mass.
- then, based on thread- threads 1-T/2 operate from the high popularity chunk,
  threads 1+T/2-T operate from the low-popularity one.
*/

int main() {
	tbb::concurrent_priority_queue<size_t>
	tbb::
}
