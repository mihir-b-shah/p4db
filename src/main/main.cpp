
#include "main/config.hpp"
#include "ee/database.hpp"
#include "ee/executor.hpp"
#include "ee/table.hpp"

#include <cassert>
#include <fstream>
#include <string>
#include <sstream>
#include <cstring>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include <unistd.h>
#include <execinfo.h>

void print_backtrace(int sig) {
	printf("signal: %d\n", sig);
	void* array[1024];
	size_t size = backtrace(array, 1024);
	char** strings = backtrace_symbols(array, size);
	for (size_t i = 0; i < size; i++) {
		printf("\t%s\n", strings[i]);
	}
	puts("");
	free(strings);
	exit(EXIT_FAILURE);
}

int main(int argc, char** argv) {
	// signal(SIGSEGV, print_backtrace);
	// signal(SIGABRT, print_backtrace);

    auto& config = Config::instance();
    config.parse_cli(argc, argv);

	// config should get the txns from the trace.
	load_txns(config);

    Database db(config.num_txn_workers);
	auto table = db.make_table<StructTable>(KV::TABLE_NAME, config.table_size);
	for (uint64_t i = 0; i < config.table_size; i++) {
		db_key_t index;
		auto& tuple = table->insert(index);
		tuple.id = i;
	}

    fprintf(stderr, "Before wait-nodes.\n");
    db.msg_handler->barrier.wait_nodes();

    // get my initial allocation.
    fprintf(stderr, "Before update_alloc.\n");
    db.update_alloc(0);
    fprintf(stderr, "After update_alloc.\n");

    std::vector<std::thread> workers;
    workers.reserve(config.num_txn_workers);

	std::vector<std::vector<Txn>> per_core_txns(config.num_txn_workers);
	for (size_t i = 0; i<config.num_txn_workers; ++i) {
		per_core_txns[i].reserve(config.trace_txns.size() / config.num_txn_workers);
	}
	for (size_t i = 0; i<config.num_txns; ++i) {
		per_core_txns[i % config.num_txn_workers].push_back(config.trace_txns[i]);
	}
    for (uint32_t i = 0; i<config.num_txn_workers; ++i) {
        workers.emplace_back(std::thread([&, i]() {
            const WorkerContext::guard worker_ctx;
			// TODO: change in production- right now, running on single machine.
            uint32_t core = i;
            printf("Pinning worker %u on core %u\n", i, core);
            pin_worker(core);
			db.msg_handler->barrier.wait_workers();

            if (ORIG_MODE) {
                orig_txn_executor(db, per_core_txns[i]);
            } else {
                txn_executor(db, per_core_txns[i]);
            }
        }));
    }

    for (auto& w : workers) {
        w.join();
    }
    db.msg_handler->barrier.wait_nodes();
    printf("Completed.\n");
    exit(0);
}
