
#include <ee/database.hpp>
#include <ee/args.hpp>
#include <ee/types.hpp>

#include <utility>
#include <main/config.hpp>

static size_t hash_key(db_key_t x) {
	return x;
}

#define BUCKET_CMP_FUNC [this](const size_t p1, const size_t p2){ return buckets[p1].size() < buckets[p2].size(); }

void Database::init_pq(size_t core_id) {
	std::vector<size_t>& pq_vec = per_core_pqs[core_id].second;
	std::make_heap(pq_vec.begin(), pq_vec.end(), BUCKET_CMP_FUNC);
}

//	TODO: should this be batched- i.e. get the next 10 txns, to reduce overheads?
//	TODO: are there txn copying overheads here?
bool Database::next_txn(size_t core_id, sched_state_t& state, std::pair<Txn,Txn>& fill) {
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
}

void Database::schedule_txn(const size_t n_threads, const std::pair<Txn, Txn>& hot_cold) {
	db_key_t cold_top_k = hot_cold.second.ops[0].mode != AccessMode::INVALID ? hot_cold.second.ops[0].id : 0;
	tbb::concurrent_hash_map<db_key_t, size_t>::accessor acc;
	/*	TODO: this has the nice behavior that if already exists, just returns
		an iterator and returns false. Does this function limit concurrency? */
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
}
