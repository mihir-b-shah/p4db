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

public:
    Database(size_t n_threads) : n_threads(n_threads), thr_batch_done_ct(0), txn_sched_bar(n_threads) {
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
