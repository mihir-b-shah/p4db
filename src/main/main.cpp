
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
    config.db = &db;

    // setup switch connection
    config.sw_intf.setup();

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

    size_t* n_remote = new size_t[config.num_txn_workers];
    for (size_t i = 0; i<config.num_txn_workers; ++i) {
        n_remote[i] = 0;
    }


	for (size_t i = 0; i<config.num_txns; ++i) {
        Txn& txn = config.trace_txns[i];
        size_t n_rem = 0;
        for (size_t j = 0; j<N_OPS; ++j) {
            n_rem += !table->part_info.location(txn.cold_ops[j].id).is_local;
        }
        size_t arg_min = 0;
        for (size_t j = 0; j<config.num_txn_workers; ++j) {
            if (n_remote[arg_min] > n_remote[j]) {
                arg_min = j;
            }
        }
        n_remote[arg_min] += n_rem;
		per_core_txns[arg_min].push_back(config.trace_txns[i]);
	}

    std::vector<Txn> redist;
    for (size_t t = 0; t < 2; ++t) {
        for (size_t i = 0; i<config.num_txn_workers; ++i) {
            while (per_core_txns[i].size() > config.num_txns / config.num_txn_workers) {
                redist.push_back(per_core_txns[i].back());
                per_core_txns[i].pop_back();
            }
            while (redist.size() > 0 && per_core_txns[i].size() < config.num_txns / config.num_txn_workers) {
                per_core_txns[i].push_back(redist.back());
                redist.pop_back();
            }
        }
        printf("per_core_txns[%d].size() = %d\n", i, per_core_txns[i].size());
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
