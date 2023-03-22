
#include <comm/comm.hpp>
#include <ee/executor.hpp>

#include <bitset>
#include <utility>
#include <algorithm>
#include <mutex>
#include <thread>
#include <pthread.h>
#include <limits>
#include <optional>
	
RC TxnExecutor::my_execute(Txn& arg, void** packet_fill) {
	arg.id.field.valid = true;
	assert(arg.id.field.mini_batch_id == mini_batch_num);
	// acquire all locks first, ex and shared. Can rollback within loop

	TupleFuture<KV>* ops[N_OPS];
	for (size_t i = 0; auto& op : arg.cold_ops) {
		if (op.mode == AccessMode::WRITE) {
			ops[i] = write(kvs, op, arg.id);
		} else if (op.mode == AccessMode::READ) {
			ops[i] = read(kvs, op, arg.id);
		} else {
			assert(op.mode == AccessMode::INVALID);
			break;
		}
		if (!ops[i]) {
			return rollback();
		}
		++i;
	}

	// Use obtained write-locks to write values
	for (size_t i = 0; auto& op : arg.cold_ops) {
		if (op.mode == AccessMode::WRITE) {
			auto x = ops[i]->get();
			if (!x) {
				return rollback();
			}
			x->value = op.value;
			ops[i]->last_acq = arg.id;
		} else if (op.mode == AccessMode::READ) {
			const auto x = ops[i]->get();
			if (!x) {
				return rollback();
			}
			const auto value = x->value;
			do_not_optimize(value);
			ops[i]->last_acq = arg.id;
		} else {
			break;
		}
		++i;
	}

	*packet_fill = db.hot_send_q.alloc_slot(mini_batch_num);
	// locks automatically released
	return commit();
}

RC TxnExecutor::execute(Txn& arg) {
	arg.id.field.valid = false;
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

	// acquire all locks first, ex and shared. Can rollback within loop
	TupleFuture<KV>* ops[N_OPS];
	for (size_t i = 0; auto& op : arg.cold_ops) {
		if (op.mode == AccessMode::WRITE) {
			ops[i] = write(kvs, op, arg.id);
		} else if (op.mode == AccessMode::READ) {
			ops[i] = read(kvs, op, arg.id);
		} else {
			assert(ORIG_MODE && op.mode == AccessMode::INVALID);
			break;
		}

		if (!ops[i]) {
			return rollback();
		}
		++i;
	}

	// Use obtained write-locks to write values
	for (size_t i = 0; auto& op : arg.cold_ops) {
		if (op.mode == AccessMode::WRITE) {
			auto x = ops[i]->get();
			if (!x) {
				return rollback();
			}
			x->value = op.value;
		} else if (op.mode == AccessMode::READ) {
			const auto x = ops[i]->get();
			if (!x) {
				return rollback();
			}
			const auto value = x->value;
			do_not_optimize(value);
		} else {
			break;
		}
		++i;
	}

    if (arg.do_accel) {
		WorkerContext::get().cycl.reset(stats::Cycles::switch_txn_latency);
		WorkerContext::get().cycl.start(stats::Cycles::switch_txn_latency);
        /*  TODO remember we are truncating 3+ pass txns, maybe emulate some additional
            latency here? (as well as contention on-switch, but I think 2-pass txns will
            do that trick for me. */
		SwitchFuture<SwitchInfo>* multi_f = atomic(p4_switch, arg);
		const size_t n_results = multi_f->get().n_results;
		do_not_optimize(n_results);

		WorkerContext::get().cycl.stop(stats::Cycles::switch_txn_latency);
		WorkerContext::get().cycl.save(stats::Cycles::switch_txn_latency);
	}

	// locks automatically released
	return commit();
}

RC TxnExecutor::commit() {
	// TODO: log should not clear until the end of a batch.
	// TODO: now, the undolog has in the future both the value,last_acq fields to be written.
	log.commit(ts);
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

RC TxnExecutor::rollback() {
	log.rollback(ts);
	mempool.clear();

	// for (int i = 0; i < 128; ++i) { // abort backoff
	//     __builtin_ia32_pause();
	// }

	return RC::ROLLBACK;
}

TupleFuture<KV>* TxnExecutor::read(StructTable* table, const Txn::OP& op, TxnId id) {
	// fprintf(stderr, "Running read.\n");
	using Future_t = TupleFuture<KV>;
	auto loc_info = op.loc_info;
    bool my_execute = id.field.valid;

	if constexpr (error::LOG_TABLE) {
		std::stringstream ss;
		ss << "read to " << table->name << " key=" << op.id << " is_local=" << loc_info.is_local
		   << " target=" << loc_info.target << '\n';
		std::cout << ss.str();
	}

	if (loc_info.is_local) {
		WorkerContext::get().cycl.start(stats::Cycles::local_latency);
		auto future = mempool.allocate<Future_t>();
		// XXX a hack, just to pass my id in.
		future->last_acq = id;
		// fprintf(stderr, "id: (%u,%u,%u) future->last_acq: %u\n", id.field.valid, id.field.node_id, id.field.mini_batch_id, future->last_acq.get_packed());
		assert(!my_execute || future->last_acq.field.mini_batch_id == mini_batch_num);
		if (!table->get(op.id, AccessMode::READ, future, ts)) [[unlikely]] {
			return nullptr;
		}
		log.add_read(table, op.id, future); // TODO passing future necessary?
		// this should never happen, the table->get() just set the future.
		if (!future->get()) [[unlikely]] {
			return nullptr;
		} // make optional for NO_WAIT
		WorkerContext::get().cycl.stop(stats::Cycles::local_latency);
		return future;
	}

	AccessMode mode = AccessMode::READ;
	WorkerContext::get().cycl.start(stats::Cycles::remote_latency);
	auto pkt = db.comm->make_pkt();
	auto req = pkt->ctor<msg::TupleGetReq>(ts, table->id, op.id, mode, id);
	req->sender = db.comm->node_id;
	req->me_pack = id.get_packed();

	auto future = mempool.allocate<Future_t>();
	auto msg_id = db.msg_handler->set_new_id(req);
	//printf("LINE:%d Inserting for msg_id=%lu, future=%p\n", __LINE__, msg_id.value, future);
	db.msg_handler->add_future(msg_id, future);

	db.comm->send(loc_info.target, pkt, tid);
	log.add_remote_read(future, loc_info.target);
	if (!future->get()) [[unlikely]] {
		return nullptr;
	}
	WorkerContext::get().cycl.stop(stats::Cycles::remote_latency);
	return future;
}

TupleFuture<KV>* TxnExecutor::write(StructTable* table, const Txn::OP& op, TxnId id) {
	// fprintf(stderr, "Running write.\n");
	using Future_t = TupleFuture<KV>;

	auto loc_info = op.loc_info;
    bool my_execute = id.field.valid;

	if constexpr (error::LOG_TABLE) {
		std::stringstream ss;
		ss << "write to " << table->name << " key=" << op.id << " is_local=" << loc_info.is_local
		   << " target=" << loc_info.target << '\n';
		std::cout << ss.str();
	}

	char* rv = (char*) &loc_info.is_local;
	assert(*rv == 1 || *rv == 0);
	if (loc_info.is_local) {
		WorkerContext::get().cycl.start(stats::Cycles::local_latency);
		auto future = mempool.allocate<Future_t>();
		future->last_acq = id;
		future->tuple = nullptr;
		// fprintf(stderr, "id: (%u,%u,%u) future->last_acq: %u\n", id.field.valid, id.field.node_id, id.field.mini_batch_id, future->last_acq.get_packed());
		assert(!my_execute || future->last_acq.field.mini_batch_id == mini_batch_num);
		if (!table->get(op.id, AccessMode::WRITE, future, ts)) [[unlikely]] {
			return nullptr;
		}
		log.add_write(table, op.id, future);
		if (!future->get()) [[unlikely]] {
			return nullptr;
		}
		WorkerContext::get().cycl.stop(stats::Cycles::local_latency);
		return future;
	}

	AccessMode mode = AccessMode::WRITE;
	WorkerContext::get().cycl.start(stats::Cycles::remote_latency);
	auto pkt = db.comm->make_pkt();
	auto req = pkt->ctor<msg::TupleGetReq>(ts, table->id, op.id, mode, id);
	req->sender = db.comm->node_id;
	req->me_pack = id.get_packed();

	auto future = mempool.allocate<Future_t>();
	auto msg_id = db.msg_handler->set_new_id(req);
	//printf("LINE:%d Inserting for msg_id=%lu, future=%p\n", __LINE__, msg_id.value, future);
	db.msg_handler->add_future(msg_id, future);

	db.comm->send(loc_info.target, pkt, tid);
	log.add_remote_write(future, loc_info.target);
	if (!future->get()) [[unlikely]] {
		return nullptr;
	}
	WorkerContext::get().cycl.stop(stats::Cycles::remote_latency);
	return future;
}

TupleFuture<KV>* TxnExecutor::insert(StructTable* table) {
	WorkerContext::get().cycl.start(stats::Cycles::local_latency);
	using Future_t = TupleFuture<KV>;
	db_key_t key;
	table->insert(key);

	auto future = mempool.allocate<Future_t>();
	if (!table->get(key, AccessMode::WRITE, future, ts)) [[unlikely]] {
		return nullptr;
	}
	log.add_write(table, key, future);
	if (!future->get()) [[unlikely]] {
		return nullptr;
	}
	WorkerContext::get().cycl.stop(stats::Cycles::local_latency);
	return future;
}

SwitchFuture<SwitchInfo>* TxnExecutor::atomic(SwitchInfo& p4_switch, const Txn& arg) {
	auto& comm = db.comm;

	auto pkt = comm->make_pkt();
	auto txn = pkt->ctor<msg::SwitchTxn>();
	txn->sender = comm->node_id;
	p4_switch.make_txn(arg, txn->data);

	auto size = msg::SwitchTxn::size(HOT_TXN_BYTES);
	pkt->resize(size);

	using Future_t = SwitchFuture<SwitchInfo>;
	auto future = mempool.allocate<Future_t>(p4_switch, arg, txn->data);
	auto msg_id = comm->handler->set_new_id(txn);
	//printf("LINE:%d Inserting for msg_id=%lu, future=%p\n", __LINE__, msg_id.value, future);
	comm->handler->add_future(msg_id, future);
	comm->send(comm->switch_id, pkt, tid);
	return future;
}

static void reset_db_batch(Database* db) {
	// __atomic_store_n(&db->thr_batch_done_ct, 0, __ATOMIC_SEQ_CST);
    // db->hot_send_q.done_sending();
}

void TxnExecutor::run_txn(scheduler_t& sched, bool enqueue_aborts, std::queue<in_sched_entry_t>& q) {
    assert(q.empty() == false);
    in_sched_entry_t e = q.front();
    Txn& txn = entry_to_txn(sched.exec, e);
    assert(txn.init_done == true);
    q.pop();

    assert(mini_batch_num > 0);
    txn.id = TxnId(true, sched.node_id, mini_batch_num);
    assert(txn.id.field.valid == true && txn.id.field.node_id == sched.node_id && txn.id.field.mini_batch_id == mini_batch_num);
    assert(txn.init_done == true);
    if (txn.do_accel) {
        //	TODO note this buffer is malloc-ed, seems excessive.
        void* pkt_buf;
        RC res = my_execute(txn, &pkt_buf);
        if (res == ROLLBACK) {
            txn.n_aborts += 1;
            if (enqueue_aborts && txn.n_aborts <= MAX_TIMES_ACCEL_ABORT) {
                q.push(e);
            } else {
                //  convert hot into cold ops again.
                /*  we are guaranteed this will be called for non-truncated txns, so it is safe
                    to append to the end of cold_ops- there will be no gaps when I'm done. */
                size_t cold_p = N_OPS-1;
                for (size_t p = 0; p<N_OPS && 
                        txn.hot_ops_pass1[p].first.mode != AccessMode::INVALID; ++p) {
                    txn.cold_ops[cold_p--] = txn.hot_ops_pass1[p].first;
                }
                for (size_t p = 0; p<MAX_OPS_PASS2_ACCEL && 
                        txn.hot_ops_pass2[p].first.mode != AccessMode::INVALID; ++p) {
                    txn.cold_ops[cold_p--] = txn.hot_ops_pass1[p].first;
                }
                // everything should be back.
                assert(txn.cold_ops[cold_p].mode != AccessMode::INVALID);
                txn.do_accel = false;
                leftover_txns.push(e);
            }
        } else {
            p4_switch.make_txn(txn, pkt_buf);
        }
    } else {
        leftover_txns.push(e);
    }
}

void TxnExecutor::run_leftover_txns() {
    /*  TODO potential livelock problems, what if two txns on different nodes keep aborting each
        other, and the leftover queues on both are very small, so they have no chance to separate? */
    while (!leftover_txns.empty()) {
        in_sched_entry_t e = leftover_txns.front();
        Txn& txn = entry_to_txn(this, e);
        RC result = execute(txn);
        if (result == ROLLBACK) {
            leftover_txns.push(e);
        }
        leftover_txns.pop();
    }
}

void txn_executor(Database& db, std::vector<Txn>& txns) {
	auto& config = Config::instance();
    TxnExecutor tb{db};
	size_t node_id = config.node_id;
	DeclusteredLayout* layout = config.decl_layout;
	size_t thread_id = WorkerContext::get().tid;
	std::vector<Txn> aborted;
	tb.mini_batch_num = 1;
	const size_t n_threads = config.num_txn_workers;

	const size_t batch_tgt = BATCH_SIZE_TGT/n_threads;
    const size_t mini_batch_tgt = MINI_BATCH_SIZE_TGT/n_threads;

    // just resize to make things simpler.
    txns.resize((txns.size() / batch_tgt) * batch_tgt);
	assert(txns.size() % batch_tgt == 0);

	scheduler_t sched(&tb);

	for (size_t i = 0; i<txns.size(); i+=batch_tgt) {
		size_t batch_num = i/batch_tgt;
		sched.sched_batch(txns, i, i+batch_tgt);

        // first run stuff easily- everyone hits soft batches- equivalent to hard.
        size_t orig_mb_num = tb.mini_batch_num;
        while (tb.mini_batch_num - orig_mb_num < BATCH_SIZE_TGT/MINI_BATCH_SIZE_TGT) {
            auto& q = sched.mb_queues[(tb.mini_batch_num-1) % sched.n_queues];
            size_t txn_num = 0;
            while (txn_num < mini_batch_tgt && !q.empty()) {
                tb.run_txn(sched, true, q);
                txn_num += 1;
            }
            tb.mini_batch_num += 1;
		    db.msg_handler->barrier.wait_workers_soft();
        }

        // drain the remaining queues over a SINGLE mini-batch. don't accelerate the rest.
        // does this allow aborting too many things?
        for (size_t qn = 0; qn<sched.n_queues; ++qn) {
            auto& q = sched.mb_queues[qn];
            while (!q.empty()) {
                tb.run_txn(sched, false, q);
            }
        }
        tb.mini_batch_num += 1;

        // thread 0 is the leader thread.
        // printf("Hot-batch-completed. %u. Batch_num: %lu\n", db.n_hot_batch_completed, batch_num);
        if (thread_id == 0) {
            // printf("Before wait_sched_ready.\n");
            db.wait_sched_ready();
            // printf("After wait_sched_ready.\n");
            run_hot_period(tb, layout);
            db.update_alloc(1+batch_num);

            tb.run_leftover_txns();
            __sync_synchronize();

            db.hot_send_q.done_sending();
            db.n_hot_batch_completed += 1;
        } else {
            tb.run_leftover_txns();
            while (db.n_hot_batch_completed < 1+batch_num) {
                _mm_pause();
            }
        }
	}
}

void orig_txn_executor(Database& db, std::vector<Txn>& txns) {
	auto& config = Config::instance();
    TxnExecutor tb{db};

    // not necessary to make it aligned, but for compatability, to execute same # of txns.
	const size_t n_threads = config.num_txn_workers;
	const size_t batch_tgt = BATCH_SIZE_TGT/n_threads;
	size_t thread_id = WorkerContext::get().tid;

    txns.resize((txns.size() / batch_tgt) * batch_tgt);
	assert(txns.size() % batch_tgt == 0);

    for (size_t i = 0; i<txns.size(); ++i) {
        extract_hot_cold(tb.kvs, txns[i], config.decl_layout);
        assert(txns[i].init_done);
        RC result = tb.execute(txns[i]);
        if (result == ROLLBACK) {
            in_sched_entry_t e = {i, thread_id};
            tb.leftover_txns.push(e);
        }
    }
    tb.run_leftover_txns();
}
