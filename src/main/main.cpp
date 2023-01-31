
#include "main/config.hpp"
#include "ee/database.hpp"
#include "ee/transaction.hpp"
#include "ee/table.hpp"

int main(int argc, char** argv) {
    auto& config = Config::instance();
    config.parse_cli(argc, argv);
	// config should get the txns from the trace.

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

	// TODO: feed the txns.
	std::vector<Txn> txns;

    for (uint32_t i = 0; i < config.num_txn_workers; ++i) {
        workers.emplace_back(std::thread([&, i]() {
            const WorkerContext::guard worker_ctx;
            pin_worker(i);
			db.msg_handler->barrier.wait_workers();
			txn_executor(db, txns);
        }));
    }

    for (auto& w : workers) {
        w.join();
    }
    db.msg_handler->barrier.wait_nodes();
}
