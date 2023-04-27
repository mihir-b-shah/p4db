#pragma once

#include "comm/comm.hpp"
#include "comm/msg.hpp"
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

#include <sys/uio.h>

#if defined(RAW_PACKETS)
static constexpr size_t HOT_TXN_PKT_BYTES = USE_1PASS_PKTS ? 440 : 144;
#else
static constexpr size_t HOT_TXN_PKT_BYTES = USE_1PASS_PKTS ? 398 : 102;
#endif

struct hot_send_q_t {
	struct hot_txn_entry_t {
        Txn* txn;
		uint32_t mini_batch_num;
        // just a pointer + length.
        struct iovec iov;
	};

	//	TODO false sharing on this cache line?
	uint32_t send_q_tail;
	const size_t send_q_capacity;
	hot_txn_entry_t* send_q;
    LockedStackPool buf_pool;

	hot_send_q_t(size_t cap) : send_q_tail(0), send_q_capacity(cap), buf_pool(cap*HOT_TXN_PKT_BYTES) {
		send_q = new hot_txn_entry_t[send_q_capacity];
	}

	~hot_send_q_t() {
		delete[] send_q;
	}

    void* alloc_slot(uint32_t mb_num, Txn* txn) {
		//	TODO almost certain this can be relaxed mem order
        void* buf = buf_pool.allocate(HOT_TXN_PKT_BYTES);
		hot_txn_entry_t& entry = send_q[__atomic_fetch_add(&send_q_tail, 1, __ATOMIC_SEQ_CST)];
        entry.txn = txn;
		entry.mini_batch_num = mb_num;
        entry.iov.iov_base = buf;
        entry.iov.iov_len = HOT_TXN_PKT_BYTES;
		return buf;
	}

	void done_sending() {
		// TODO	does this need to be sequentially consistent? or atomic at all?
        buf_pool.clear();
		__atomic_store_n(&send_q_tail, 0, __ATOMIC_SEQ_CST);
	}
};

extern void single_db_section(void* arg);

class Database {
    std::vector<Table*> table_ids;
    std::unordered_map<std::string, Table*> table_names;

public:
    std::unique_ptr<MessageHandler> msg_handler;
    std::unique_ptr<Communicator> comm;

	size_t n_threads;
	uint32_t thr_batch_done_ct;
	std::vector<Txn>** per_core_txns;
	hot_send_q_t hot_send_q;
    int sched_sockfd;
    reusable_barrier_t batch_bar;

    void setup_sched_sock();
    void update_alloc(uint32_t batch_num);
    void wait_sched_ready();

public:
    Database(size_t n_threads) : n_threads(n_threads), thr_batch_done_ct(0), hot_send_q(BATCH_SIZE_TGT), batch_bar(n_threads, single_db_section, false) {
        comm = std::make_unique<Communicator>();
        msg_handler = std::make_unique<MessageHandler>(*this, comm.get());
        msg_handler->init.wait();
		per_core_txns = (std::vector<Txn>**) malloc(sizeof(per_core_txns[0])*n_threads);
        setup_sched_sock();
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
