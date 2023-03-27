#pragma once

#include "comm/comm.hpp"
#include "comm/msg.hpp"
#include "comm/msg_handler.hpp"
#include "ee/args.hpp"
#include "ee/database.hpp"
#include "ee/defs.hpp"
#include "ee/errors.hpp"
#include "ee/future.hpp"
#include "ee/switch.hpp"
#include "ee/table.hpp"
#include "utils/mempools.hpp"
#include "utils/ts_factory.hpp"
#include "ee/types.hpp"
#include "ee/undolog.hpp"
#include "utils/util.hpp"
#include "stats/context.hpp"
#include "main/config.hpp"

#include <iostream>
#include <vector>
#include <queue>
#include <cassert>
#include <unordered_set>
#include <pthread.h>

enum RC {
	COMMIT,
	ROLLBACK,
};

typedef uint32_t txn_pos_t;
struct __attribute__((packed)) in_sched_entry_t {
	txn_pos_t idx : 24;
	uint8_t thr_id : 8;
};

struct TxnExecutor;
struct scheduler_t {
	size_t node_id;
	TxnExecutor* exec;
	DeclusteredLayout* layout;
	std::vector<std::vector<std::vector<size_t>>> schedules;
	size_t n_schedules;
	size_t n_queues;
	size_t schedule_len;
	//	TODO: is this efficient enough? Replace with a vector+pointer.
	std::queue<in_sched_entry_t>* mb_queues;
    // just for debugging.
    std::unordered_set<db_key_t> touched;

	scheduler_t(TxnExecutor* exec);
	~scheduler_t(){ delete[] mb_queues; }
	void sched_batch(std::vector<Txn>& txns, size_t s, size_t e);
	void print_schedules(size_t node);
    void process_touched(size_t mb_num);
};

struct TxnExecutor {
    StructTable* kvs;
    SwitchInfo p4_switch;
    Database& db;
    Undolog log;
    StackPool<8192> mempool;
    uint32_t tid;
	uint32_t mini_batch_num;

	std::queue<in_sched_entry_t> leftover_txns;

    TimestampFactory ts_factory;
    timestamp_t ts;

    TxnExecutor(Database& db)
        : p4_switch(db.comm->node_id), db(db), log(db.comm.get()), tid(WorkerContext::get().tid), mini_batch_num(1) {
        db.get_casted(KV::TABLE_NAME, kvs);
        p4_switch.table = kvs;
	}

    void run_leftover_txns();
    void run_txn(scheduler_t& sched, bool enqueue_aborts, std::queue<in_sched_entry_t>& q);
	RC my_execute(Txn& arg, void** packet_fill);
    RC execute(Txn& arg);
    RC commit();
    RC rollback();
    SwitchFuture<SwitchInfo>* atomic(SwitchInfo& p4_switch, const Txn& arg);
    TupleFuture<KV>* read(StructTable* table, const Txn::OP& op, TxnId id);
    TupleFuture<KV>* write(StructTable* table, const Txn::OP& op, TxnId id);
    TupleFuture<KV>* insert(StructTable* table);
};

Txn& entry_to_txn(TxnExecutor* exec, in_sched_entry_t entry);

void run_hot_period(TxnExecutor& exec, DeclusteredLayout* layout);
void extract_hot_cold(StructTable* table, Txn& txn, DeclusteredLayout* layout);

void txn_executor(Database& db, std::vector<Txn>& txns);
void orig_txn_executor(Database& db, std::vector<Txn>& txns);
