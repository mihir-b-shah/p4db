
#include <ee/database.hpp>
#include <ee/args.hpp>
#include <ee/types.hpp>

#include <utility>
#include <main/config.hpp>

static size_t hash_key(db_key_t x) {
	return x;
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
