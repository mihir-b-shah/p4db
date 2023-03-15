#pragma once

#include "comm/comm.hpp"
#include "comm/msg_handler.hpp"
#include "ee/table.hpp"
#include "utils/rbarrier.hpp"

#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <cstdio>

struct hot_send_q_t {
	struct hot_txn_entry_t {
		uint32_t mini_batch_num;
		Communicator::Pkt_t* buf;
	};

	//	TODO false sharing on this cache line?
	uint32_t send_q_tail;
	const size_t send_q_capacity;
	hot_txn_entry_t* send_q;

	hot_send_q_t(size_t cap) : send_q_tail(0), send_q_capacity(cap) {
		send_q = new hot_txn_entry_t[send_q_capacity];
	}

	~hot_send_q_t() {
		delete[] send_q;
	}

	Communicator::Pkt_t** alloc_slot(uint32_t mb_num) {
		//	TODO almost certain this can be relaxed mem order
		uint32_t slot_loc = __atomic_fetch_add(&send_q_tail, 1, __ATOMIC_SEQ_CST);
		send_q[slot_loc].mini_batch_num = mb_num;
		return &send_q[slot_loc].buf;
	}

	void done_sending() {
		// TODO	does this need to be sequentially consistent? or atomic at all?
		__atomic_store_n(&send_q_tail, 0, __ATOMIC_SEQ_CST);
	}
};

class Database {
    std::vector<Table*> table_ids;
    std::unordered_map<std::string, Table*> table_names;

public:
    std::unique_ptr<MessageHandler> msg_handler;
    std::unique_ptr<Communicator> comm;

	size_t n_threads;
	uint32_t thr_batch_done_ct;
	reusable_barrier_t txn_sched_bar;
	std::vector<Txn>** per_core_txns;
	hot_send_q_t hot_send_q;

public:
    Database(size_t n_threads) : n_threads(n_threads), thr_batch_done_ct(0), txn_sched_bar(n_threads), hot_send_q(BATCH_SIZE_TGT * n_threads) {
        comm = std::make_unique<Communicator>();
        msg_handler = std::make_unique<MessageHandler>(*this, comm.get());
        msg_handler->init.wait();
		per_core_txns = (std::vector<Txn>**) malloc(sizeof(per_core_txns[0])*n_threads);
    }

    Database(Database&&) = default;
    Database(const Database&) = delete;

    ~Database() {
        for (auto& table : table_ids) {
            delete table;
        }
    }

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
