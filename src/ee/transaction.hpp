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

#include <iostream>
#include <vector>
#include <cassert>

enum RC {
	COMMIT,
	ROLLBACK,
};

#define check(x)               \
    do {                       \
        if (!x) [[unlikely]] { \
            return rollback(); \
        }                      \
    } while (0)

struct TransactionBase {
    StructTable* kvs;
    SwitchInfo p4_switch;
    Database& db;
    Undolog log;
    StackPool<8192> mempool;
    uint32_t tid;

    TimestampFactory ts_factory;
    timestamp_t ts;

    TransactionBase(Database& db)
        : db(db), log(db.comm.get()), tid(WorkerContext::get().tid) {
        db.get_casted(KV::TABLE_NAME, kvs);
	}

    RC execute(Txn& arg) {
        ts = ts_factory.get();
        // std::stringstream ss;
        // ss << "Starting txn tid=" << tid << " ts=" << ts << '\n';
        // std::cout << ss.str();

        WorkerContext::get().cycl.reset(stats::Cycles::commit_latency);
        WorkerContext::get().cycl.reset(stats::Cycles::latch_contention);
        WorkerContext::get().cycl.reset(stats::Cycles::remote_latency);
        WorkerContext::get().cycl.reset(stats::Cycles::local_latency);
        WorkerContext::get().cycl.reset(stats::Cycles::switch_txn_latency);

        WorkerContext::get().cycl.start(stats::Cycles::commit_latency);
		if (arg.on_switch) {
			WorkerContext::get().cycl.reset(stats::Cycles::switch_txn_latency);
			WorkerContext::get().cycl.start(stats::Cycles::switch_txn_latency);
			SwitchFuture<SwitchInfo>* multi_f = atomic(p4_switch, SwitchInfo::MultiOp{arg});
			const auto values = multi_f->get().values;
			do_not_optimize(values);

			WorkerContext::get().cycl.stop(stats::Cycles::switch_txn_latency);
			WorkerContext::get().cycl.save(stats::Cycles::switch_txn_latency);
			return commit();
		}

		// acquire all locks first, ex and shared. Can rollback within loop
		TupleFuture<KV>* ops[NUM_OPS];
		for (size_t i = 0; auto& op : arg.ops) {
			if (op.mode == AccessMode::WRITE) {
				ops[i] = write(kvs, KV::pk(op.id));
			} else if (op.mode == AccessMode::READ) {
				ops[i] = read(kvs, KV::pk(op.id));
			} else {
				assert(op.mode == AccessMode::INVALID);
				ops[i] = nullptr;
			}
			check(ops[i]);
			++i;
		}

		// Use obtained write-locks to write values
		for (size_t i = 0; auto& op : arg.ops) {
			if (op.mode == AccessMode::WRITE) {
				auto x = ops[i]->get();
				check(x);
				x->value = op.value;
			} else if (op.mode == AccessMode::READ) {
				const auto x = ops[i]->get();
				check(x);
				const auto value = x->value;
				do_not_optimize(value);
			} else {
				break;
			}
			++i;
		}

		// locks automatically released
		return commit();
    }

    RC commit() {
        if constexpr (CC_SCHEME != CC_Scheme::NONE) {
            log.commit(ts);
        }
        mempool.clear();
        WorkerContext::get().cycl.stop(stats::Cycles::commit_latency);

        WorkerContext::get().cycl.save(stats::Cycles::commit_latency);
        WorkerContext::get().cycl.save(stats::Cycles::latch_contention);
        WorkerContext::get().cycl.save(stats::Cycles::remote_latency);
        WorkerContext::get().cycl.save(stats::Cycles::local_latency);
        WorkerContext::get().cycl.save(stats::Cycles::switch_txn_latency);

        WorkerContext::get().pcntr.incr(stats::Periodic::commits);
        return RC::COMMIT;
    }

    RC rollback() {
        if constexpr (CC_SCHEME != CC_Scheme::NONE) {
            log.rollback(ts);
        }
        mempool.clear();

        // for (int i = 0; i < 128; ++i) { // abort backoff
        //     __builtin_ia32_pause();
        // }

        return RC::ROLLBACK;
    }

    TupleFuture<KV>* read(StructTable* table, p4db::key_t key) {
        using Future_t = TupleFuture<KV>; // TODO return with const KV

        auto loc_info = table->part_info.location(key);

        if constexpr (error::LOG_TABLE) {
            std::stringstream ss;
            ss << "read to " << table->name << " key=" << key << " is_local=" << loc_info.is_local
               << " target=" << loc_info.target << " is_hot=" << loc_info.is_hot << " switch_idx=" << loc_info.abs_hot_index << '\n';
            std::cout << ss.str();
        }

        if (loc_info.is_local) {
            WorkerContext::get().cycl.start(stats::Cycles::local_latency);
            auto future = mempool.allocate<Future_t>();
            if (!table->get(key, AccessMode::READ, future, ts)) [[unlikely]] {
                return nullptr;
            }
            if constexpr (CC_SCHEME != CC_Scheme::NONE) {
                log.add_read(table, key, future); // TODO passing future necessary?
            }
            if (!future->get()) [[unlikely]] {
                return nullptr;
            } // make optional for NO_WAIT
            WorkerContext::get().cycl.stop(stats::Cycles::local_latency);
            return future;
        }

        AccessMode mode = AccessMode::READ;
        WorkerContext::get().cycl.start(stats::Cycles::remote_latency);
        auto pkt = db.comm->make_pkt();
        auto req = pkt->ctor<msg::TupleGetReq>(ts, table->id, key, mode);
        req->sender = db.comm->node_id;

        auto future = mempool.allocate<Future_t>();
        auto msg_id = db.msg_handler->set_new_id(req);
        //printf("LINE:%d Inserting for msg_id=%lu, future=%p\n", __LINE__, msg_id.value, future);
        db.msg_handler->add_future(msg_id, future);

        db.comm->send(loc_info.target, pkt, tid);
        if constexpr (CC_SCHEME != CC_Scheme::NONE) {
            log.add_remote_read(future, loc_info.target);
        }
        if (!future->get()) [[unlikely]] {
            return nullptr;
        }
        WorkerContext::get().cycl.stop(stats::Cycles::remote_latency);
        return future;
    }

    TupleFuture<KV>* write(StructTable* table, p4db::key_t key) {
        using Future_t = TupleFuture<KV>;

        auto loc_info = table->part_info.location(key);

        if constexpr (error::LOG_TABLE) {
            std::stringstream ss;
            ss << "write to " << table->name << " key=" << key << " is_local=" << loc_info.is_local
               << " target=" << loc_info.target << " is_hot=" << loc_info.is_hot << " switch_idx=" << loc_info.abs_hot_index << '\n';
            std::cout << ss.str();
        }

        if (loc_info.is_local) {
            WorkerContext::get().cycl.start(stats::Cycles::local_latency);
            auto future = mempool.allocate<Future_t>();
            future->tuple = nullptr;
            if (!table->get(key, AccessMode::WRITE, future, ts)) [[unlikely]] {
                return nullptr;
            }
            if constexpr (CC_SCHEME != CC_Scheme::NONE) {
                log.add_write(table, key, future);
            }
            if (!future->get()) [[unlikely]] {
                return nullptr;
            }
            WorkerContext::get().cycl.stop(stats::Cycles::local_latency);
            return future;
        }

        AccessMode mode = AccessMode::WRITE;
        WorkerContext::get().cycl.start(stats::Cycles::remote_latency);
        auto pkt = db.comm->make_pkt();
        auto req = pkt->ctor<msg::TupleGetReq>(ts, table->id, key, mode);
        req->sender = db.comm->node_id;

        auto future = mempool.allocate<Future_t>();
        auto msg_id = db.msg_handler->set_new_id(req);
        //printf("LINE:%d Inserting for msg_id=%lu, future=%p\n", __LINE__, msg_id.value, future);
        db.msg_handler->add_future(msg_id, future);

        db.comm->send(loc_info.target, pkt, tid);
        if constexpr (CC_SCHEME != CC_Scheme::NONE) {
            log.add_remote_write(future, loc_info.target);
        }
        if (!future->get()) [[unlikely]] {
            return nullptr;
        }
        WorkerContext::get().cycl.stop(stats::Cycles::remote_latency);
        return future;
    }

    TupleFuture<KV>* insert(StructTable* table) {
        WorkerContext::get().cycl.start(stats::Cycles::local_latency);
        using Future_t = TupleFuture<KV>;
        p4db::key_t key;
        table->insert(key);

        auto future = mempool.allocate<Future_t>();
        if (!table->get(key, AccessMode::WRITE, future, ts)) [[unlikely]] {
            return nullptr;
        }
        if constexpr (CC_SCHEME != CC_Scheme::NONE) {
            log.add_write(table, key, future);
        }
        if (!future->get()) [[unlikely]] {
            return nullptr;
        }
        WorkerContext::get().cycl.stop(stats::Cycles::local_latency);
        return future;
    }

	SwitchFuture<SwitchInfo>* atomic(SwitchInfo& p4_switch, const SwitchInfo::MultiOp& arg) {
        auto& comm = db.comm;

        auto pkt = comm->make_pkt();
        auto txn = pkt->ctor<msg::SwitchTxn>();
        txn->sender = comm->node_id;

        BufferWriter bw{txn->data};
        p4_switch.make_txn(arg, bw);

        auto size = msg::SwitchTxn::size(bw.size);
        pkt->resize(size);

        using Future_t = SwitchFuture<SwitchInfo>;
        auto future = mempool.allocate<Future_t>(p4_switch, arg);
        auto msg_id = comm->handler->set_new_id(txn);
        //printf("LINE:%d Inserting for msg_id=%lu, future=%p\n", __LINE__, msg_id.value, future);
        comm->handler->add_future(msg_id, future);
        comm->send(comm->switch_id, pkt, tid);

        return future;
    }
};

void txn_executor(Database& db, std::vector<Txn> txns) {
    TransactionBase tb{db};

    for (auto& arg : txns) {
        auto rc = tb.execute(arg);
        switch (rc) {
            case COMMIT:
				/*	TODO: this is stupid. Just saying we committed XXX portion of txns
					is not sufficient- we need to queue the aborted ones, and finish. */
                break;
            case ROLLBACK:
                break;
        }
    }
}
