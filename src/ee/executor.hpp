#pragma once

#include "comm/msg.hpp"
#include "comm/msg_handler.hpp"
#include "utils/buffers.hpp"
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
#include <cassert>

enum RC {
	COMMIT,
	ROLLBACK,
};

struct TxnExecutor {
    StructTable* kvs;
    SwitchInfo p4_switch;
    Database& db;
    Undolog log;
    StackPool<8192> mempool;
    uint32_t tid;
	std::vector<Txn> leftover;

    TimestampFactory ts_factory;
    timestamp_t ts;

    TxnExecutor(Database& db)
        : db(db), log(db.comm.get()), tid(WorkerContext::get().tid) {
        db.get_casted(KV::TABLE_NAME, kvs);
		leftover.reserve(BATCH_SIZE_TGT * 0.01/Config::instance().num_txn_workers);
	}

    RC execute_for_batch(Txn& arg);
    RC execute(Txn& arg);
    RC commit();
    RC rollback();
    TupleFuture<KV>* read(StructTable* table, const Txn::OP& op);
    TupleFuture<KV>* write(StructTable* table, const Txn::OP& op);
    TupleFuture<KV>* insert(StructTable* table);
	SwitchFuture<SwitchInfo>* atomic(SwitchInfo& p4_switch, const SwitchInfo::MultiOp& arg);
};

void txn_executor(Database& db, std::vector<Txn>& txns);
