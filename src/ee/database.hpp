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
#include <pthread.h>

struct sched_state_t {
	size_t added;
	std::vector<size_t> buckets_skip;

	sched_state_t() : added(0) {}
};

int setup_txn_sched_sock();
void sendall(int sockfd, char* buf, int len);
void recvall(int sockfd, char* buf, int len);

class Database {
    std::vector<Table*> table_ids;
    std::unordered_map<std::string, Table*> table_names;

public:
    std::unique_ptr<MessageHandler> msg_handler;
    std::unique_ptr<Communicator> comm;

	// TODO this is indexed by modulo operation. Causes false sharing, measure + see.
	db_key_t* sched_packet_buf;
	int txn_sched_sockfd;
	int sched_packet_buf_len;
	pthread_barrier_t bar1_txn_sched;
	pthread_barrier_t bar2_txn_sched;

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
	/*	TODO: future improvements, is pq too slow?
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
		  threads 1+T/2-T operate from the low-popularity one. */

	std::pair<std::mutex, std::vector<size_t>>* per_core_pqs;
	std::mutex bucket_insert_lock;
	size_t n_threads;

	void init_sched_ds(size_t n_threads) {
		// avoid copying while holding bucket_insert_lock mutex.
		buckets.resize(0);
		buckets.reserve(BATCH_SIZE_TGT);
		for (size_t i = 0; i<n_threads; ++i) {
			per_core_pqs[i].second.resize(0);
			per_core_pqs[i].second.reserve(static_cast<int>(BATCH_SIZE_TGT*1.5/n_threads));
		}
	}

public:
    Database(size_t n_threads) : n_threads(n_threads) {
		sched_packet_buf = new db_key_t[BATCH_SIZE_TGT+n_threads];
		sched_packet_buf_len = sizeof(db_key_t)*(BATCH_SIZE_TGT+n_threads);
		txn_sched_sockfd = setup_txn_sched_sock();
		pthread_barrier_init(&bar1_txn_sched, NULL, n_threads);
		pthread_barrier_init(&bar2_txn_sched, NULL, n_threads);
		per_core_pqs = new std::pair<std::mutex, std::vector<size_t>>[n_threads];
		init_sched_ds(n_threads);
        comm = std::make_unique<Communicator>();
        msg_handler = std::make_unique<MessageHandler>(*this, comm.get());
        msg_handler->init.wait();
    }

    Database(Database&&) = default;
    Database(const Database&) = delete;

    ~Database() {
        for (auto& table : table_ids) {
            delete table;
        }
		delete[] sched_packet_buf;
		delete[] per_core_pqs;
    }

	void run_batch_txn_sched();
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
