
#include "main/config.hpp"
#include "ee/database.hpp"
#include "ee/executor.hpp"
#include "ee/table.hpp"

#include <cassert>
#include <fstream>
#include <string>
#include <sstream>
#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    auto& config = Config::instance();
    config.parse_cli(argc, argv);

	// config should get the txns from the trace.
	load_txns(config);

    Database db;
	auto table = db.make_table<StructTable>(KV::TABLE_NAME, config.table_size);
	for (uint64_t i = 0; i < config.table_size; i++) {
		p4db::key_t index;
		auto& tuple = table->insert(index);
		tuple.id = i;
	}

    db.msg_handler->barrier.wait_nodes();

    std::vector<std::thread> workers;
    workers.reserve(config.num_txn_workers);

	std::vector<std::vector<Txn>> per_core_txns(config.num_txn_workers);
	for (size_t i = 0; i < config.num_txn_workers; ++i) {
		per_core_txns[i].reserve(config.trace_txns.size() / config.num_txn_workers);
	}
	for (size_t j = 0; j < config.num_txns; ++j) {
		for (size_t i = 0; i < config.num_txn_workers; ++i) {
			const Txn& txn = config.trace_txns[(j*config.num_txn_workers+i) % config.trace_txns.size()];
			per_core_txns[i].push_back(txn);
		}
	}

    for (uint32_t i = 0; i < config.num_txn_workers; ++i) {
        workers.emplace_back(std::thread([&, i]() {
            const WorkerContext::guard worker_ctx;
            pin_worker(i);
			db.msg_handler->barrier.wait_workers();
			txn_executor(db, per_core_txns[i]);
        }));
    }

    for (auto& w : workers) {
        w.join();
    }
    db.msg_handler->barrier.wait_nodes();
}
