
#include <ee/database.hpp>
#include <ee/args.hpp>
#include <ee/types.hpp>

#include <utility>
#include <main/config.hpp>

static size_t hash_key(db_key_t x) {
	return x;
}

/*	A spinning reusable barrier impl, since pthread_barrier_t is NOT reusable.
	TODO: optimize by rolling my own set of two counters if necessary, right now just 2 barriers.
	https://github.com/stephentu/silo/blob/master/spinbarrier.h
	P4DB's distributed barrier is also a 2-phase barrier, but is potentially wrong? Need to check */
void Database::run_batch_txn_sched() {
	int rc1 = pthread_barrier_wait(&bar1_txn_sched);
	if (rc1 == PTHREAD_BARRIER_SERIAL_THREAD) {
		printf("buf_len outside: %d\n", sched_packet_buf_len);
		sendall(txn_sched_sockfd, (char*) sched_packet_buf, sched_packet_buf_len);
		recvall(txn_sched_sockfd, (char*) sched_packet_buf, sched_packet_buf_len);
	} else {
		assert(rc1 == 0);
	}
	int rc2 = pthread_barrier_wait(&bar2_txn_sched);
	assert(rc2 == PTHREAD_BARRIER_SERIAL_THREAD || rc2 == 0);
}

#define BUCKET_CMP_FUNC [this](const size_t p1, const size_t p2){ return buckets[p1].size() < buckets[p2].size(); }

void Database::init_pq(size_t core_id) {
	std::vector<size_t>& pq_vec = per_core_pqs[core_id].second;
	std::make_heap(pq_vec.begin(), pq_vec.end(), BUCKET_CMP_FUNC);
}

//	TODO: should this be batched- i.e. get the next 10 txns, to reduce overheads?
//	TODO: are there txn copying overheads here?
bool Database::next_txn(size_t core_id, sched_state_t& state, std::pair<Txn,Txn>& fill) {
	return true;
	/*
	std::vector<size_t>& pq = per_core_pqs[core_id].second;
	if (state.added == MINI_BATCH_TGT/n_threads) {
		while (state.buckets_skip.size() > 0) {
			pq.push_back(state.buckets_skip.back());
			std::push_heap(pq.begin(), pq.end(), BUCKET_CMP_FUNC);
			state.buckets_skip.pop_back();
		}
		state.added = 0;
	}
	// note pq[0] is the top.
	while (pq.size() > 0 && buckets[pq[0]].size() == 0) {
		std::pop_heap(pq.begin(), pq.end(), BUCKET_CMP_FUNC);
		pq.pop_back();
	}
	if (pq.size() == 0) {
		return false;
	}
	size_t pos = pq[0];
	auto& avail_txns = buckets[pos];
	fill = avail_txns.back();
	std::pop_heap(pq.begin(), pq.end(), BUCKET_CMP_FUNC);
	avail_txns.pop_back();
	state.buckets_skip.push_back(pos);
	state.added += 1;
	return true;
	*/
}

void Database::schedule_txn(const size_t n_threads, const std::pair<Txn, Txn>& hot_cold) {
	/*
	db_key_t cold_top_k = hot_cold.second.ops[0].mode != AccessMode::INVALID ? hot_cold.second.ops[0].id : 0;
	tbb::concurrent_hash_map<db_key_t, size_t>::accessor acc;
	//	TODO: this has the nice behavior that if already exists, just returns
	//	an iterator and returns false. Does this function limit concurrency?
	bool yes_insert = bucket_map.insert(acc, cold_top_k);
	if (yes_insert) {
		//	XXX: no deadlock, since we always acquire map lock before bucket lock.
		bucket_insert_lock.lock();
		acc->second = buckets.size();
		buckets.emplace_back();
		bucket_insert_lock.unlock();
		
		size_t pq_id = hash_key(cold_top_k) % n_threads;
		per_core_pqs[pq_id].first.lock();
		per_core_pqs[pq_id].second.push_back(acc->second);
		per_core_pqs[pq_id].first.unlock();
	}
	// TODO: waste less than 10x space holding these, to avoid frequent allocs.
	buckets[acc->second].reserve(8);
	std::vector<std::pair<Txn,Txn>>& my_bucket = buckets[acc->second];
	// XXX: safe access, since only through the map can we access the bucket.
	my_bucket.push_back(hot_cold);
	*/
}
