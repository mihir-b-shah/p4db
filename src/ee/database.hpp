#pragma once

#include "comm/comm.hpp"
#include "comm/msg_handler.hpp"
#include "ee/table.hpp"

#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <cstdio>
#include <tbb/concurrent_hash_map.h>

struct sched_state_t {
	size_t added;
	std::vector<size_t> buckets_skip;

	sched_state_t() : added(0) {}
};

class Database {
    std::vector<Table*> table_ids;
    std::unordered_map<std::string, Table*> table_names;

public:
    std::unique_ptr<MessageHandler> msg_handler;
    std::unique_ptr<Communicator> comm;

	// TODO: measure, see if pthread_barrier_t is sufficiently cheap, or do I need my own spin-variant?
	// TODO: This should **work** with C++ std::thread, right?
	pthread_barrier_t txn_exec_barrier;

	/*	TODO: there are two potential solutions here:
		Solution 1:
		1)	The main thread loads the txn trace in, and round-robin assigns txns to individual queues.
		2)	The threads in parallel, run get_hot_cold on each, and assign txns to the correct pq,
			with locking.
		3)	Now, there is no need to lock when building up the buckets, and keeping the buckets
			consistent with the pq.
		4)	Run using the pq's like normal. Advantage of this was no bucket locking, disadv was
			an extra pass for the get_hot_cold.
		Solution 2:
		Same thing, but no second pass. This means multiplexing hasn't happened yet when building
		the buckets, so they must be locked as well.
		I'll do the second approach for now- there's some locking overheads, but I think its ok,
		and simpler logic.
		Two potential sources of overhead here- the std::mutex, and some indirection. */

	// TODO: right now, we can only remove efficiently stack-wise. Maybe let's impl a queue-style vector?
	std::vector<std::vector<std::pair<Txn,Txn>>> buckets;
	bool bucket_cmp_func(const size_t p1, const size_t p2){
		return buckets[p1].size() < buckets[p2].size();
	};

	tbb::concurrent_hash_map<db_key_t, size_t> bucket_map;
	/*	TODO: false sharing problems on this per_core_pq structure? */
	std::pair<std::mutex, std::vector<size_t>>* per_core_pqs;
	std::mutex bucket_insert_lock;
	size_t n_threads;

	void init_sched_ds(size_t n_threads) {
		// avoid copying while holding bucket_insert_lock mutex.
		buckets.resize(0);
		buckets.reserve(BATCH_SIZE_TGT*n_threads);
		for (size_t i = 0; i<n_threads; ++i) {
			per_core_pqs[i].second.resize(0);
			per_core_pqs[i].second.reserve(static_cast<int>(BATCH_SIZE_TGT*1.5));
		}
	}

public:
    Database(size_t n_threads) : n_threads(n_threads) {
		per_core_pqs = new std::pair<std::mutex, std::vector<size_t>>[n_threads];
		init_sched_ds(n_threads);
        comm = std::make_unique<Communicator>();
        msg_handler = std::make_unique<MessageHandler>(*this, comm.get());
        msg_handler->init.wait();
		pthread_barrier_init(&txn_exec_barrier, NULL, n_threads);
    }

    Database(Database&&) = default;
    Database(const Database&) = delete;

    ~Database() {
        for (auto& table : table_ids) {
            delete table;
        }
		delete[] per_core_pqs;
    }

	void init_pq(size_t core_id);
	bool next_txn(size_t core_id, sched_state_t& state, std::pair<Txn,Txn>& fill);
	void schedule_txn(const size_t n_threads, const std::pair<Txn, Txn>& hot_cold);

    template <typename T, typename... Args>
    auto make_table(std::string key, Args&&... args) {
        if (has_table(key)) {
            throw std::logic_error("Table already present in database");
        }
        std::cout << "Allocating Table: " << key << '\n';
        auto table = new T{std::forward<Args>(args)..., *comm};
        table->id = p4db::table_t{table_ids.size()};
        table->name = key;
        table_ids.emplace_back(table);
        table_names[key] = table;
        return table;
    }

    Table* operator[](std::string key) {
        return table_names.at(key);
    }
    Table* operator[](p4db::table_t id) {
        return table_ids[id];
    }
    bool has_table(std::string name) {
        return table_names.find(name) != table_names.end();
    }

    template <typename T>
    void get_casted(std::string key, T*& dest) {
        dest = dynamic_cast<T*>((*this)[key]);
        if (!dest) {
            throw error::TableCastFailed();
        }
    }
};
