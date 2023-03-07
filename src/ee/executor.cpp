
#include <ee/executor.hpp>

#include <utility>
#include <algorithm>
#include <mutex>
#include <thread>
#include <pthread.h>
#include <limits>
#include <optional>
	
RC TxnExecutor::my_execute(Txn& arg) {
	arg.id.field.valid = true;
	// acquire all locks first, ex and shared. Can rollback within loop

	TupleFuture<KV>* ops[NUM_OPS];
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
		} else {
			break;
		}
		++i;
	}

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
	if (false) { // TODO: arg.on_switch) {
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
	for (size_t i = 0; auto& op : arg.cold_ops) {
		if (op.mode == AccessMode::WRITE) {
			ops[i] = write(kvs, op, arg.id);
		} else if (op.mode == AccessMode::READ) {
			ops[i] = read(kvs, op, arg.id);
		} else {
			assert(op.mode == AccessMode::INVALID);
			ops[i] = nullptr;
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

SwitchFuture<SwitchInfo>* TxnExecutor::atomic(SwitchInfo& p4_switch, const SwitchInfo::MultiOp& arg) {
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

static void extract_hot_cold(StructTable* table, Txn& txn, DeclusteredLayout* layout) {
	// Keep track of the hottest local/global value.
	db_key_t cold1_lk, cold1_gk;
	size_t cold1_lv = 0, cold1_gv = 0;
	size_t hot_p = 0, cold_p = 0;
	
	bool cold_all_local = true;
	size_t i = 0;
	while (i < NUM_OPS && txn.cold_ops[i].mode != AccessMode::INVALID) {
		txn.cold_ops[i].loc_info = table->part_info.location(txn.cold_ops[i].id);
		std::pair<bool, size_t> hot_info = layout->is_hot(txn.cold_ops[i].id);
		if (hot_info.first) {
			txn.hot_ops[hot_p++] = txn.cold_ops[i];
		} else {
			if (txn.cold_ops[i].loc_info.is_local) {
				if (hot_info.second > cold1_lv) {
					cold1_lk = txn.cold_ops[i].id;
					cold1_lv = hot_info.second;
				}
			} else {
				cold_all_local = false;
			}
			if (hot_info.second > cold1_gv) {
				cold1_gk = txn.cold_ops[i].id;
				cold1_gv = hot_info.second;
			}
			txn.cold_ops[cold_p++] = txn.cold_ops[i];
		}
		i += 1;
	}
	if (cold_p < NUM_OPS) {
		txn.cold_ops[cold_p].mode = AccessMode::INVALID;
	}
	txn.cold_all_local = cold_all_local;
	if (cold1_lv > 0) {
		txn.hottest_local_cold_k = cold1_lk;
	}
	if (cold1_gv > 0) {
		txn.hottest_any_cold_k = cold1_gk;
	}
	assert(cold_p + hot_p == NUM_OPS);
	txn.init_done = true;
}

void txn_executor(Database& db, std::vector<Txn>& txns) {
	auto& config = Config::instance();
    TxnExecutor tb{db};
	size_t node_id = config.node_id;
	DeclusteredLayout* layout = config.decl_layout;
	size_t thread_id = WorkerContext::get().tid;
	const size_t n_threads = config.num_txn_workers;
	const size_t mini_batch_tgt = MINI_BATCH_SIZE_THR_TGT;
	const size_t batch_tgt = BATCH_SIZE_THR_TGT;
	size_t txn_num = 0;
	std::vector<Txn> aborted;
	assert(txns.size() % batch_tgt == 0);

	// input of scheduler, maybe?
	for (size_t i = 0; i<txns.size(); i+=batch_tgt) {
		size_t batch_num = i/batch_tgt;
		for (size_t j = 0; j<batch_tgt; ++j) {
			in_sched_entry_t& entry = ((in_sched_entry_t*) tb.raw_buf)[j];
			entry.thr_id = thread_id;
			entry.idx = i+j;
		}
		((in_sched_entry_t*) tb.raw_buf)[batch_tgt].idx = TxnExecutor::TxnIterator::SENTINEL_POS;

		// execute a batch, break into mini-batches as we see fit.
		// XXX: measure from here.
		TxnExecutor::TxnIterator txn_iter(tb);
		size_t txn_num = 0;
		size_t committed = 0;
		tb.mini_batch_num = 1;

		while (1) {
			if (txn_num == MINI_BATCH_SIZE_THR_TGT) {
				db.msg_handler->barrier.wait_workers();
				fprintf(stderr, "Committed: %lu\n", committed);
				committed = 0;
				txn_num = 0;
				/*	TODO: what happens when different nodes end up with different amts of txns
					due to aborts- we need a way to handle this with minimum sync. */
				tb.mini_batch_num += 1;
			}

			std::optional<in_sched_entry_t> e = txn_iter.next_entry();
			if (!e.has_value()) {
				break;
			}

			fprintf(stderr, "Running txn %u from thread %u's queue.\n", e.value().idx, e.value().thr_id);
			Txn& txn = txn_iter.entry_to_txn(e.value());
			txn.id = TxnId(true, node_id, tb.mini_batch_num);
			assert(txn.id.field.valid == true && txn.id.field.node_id == node_id && txn.id.field.mini_batch_id == tb.mini_batch_num);
			// Make sure extract_hot_cold is run only once.
			if (!txn.init_done) {
				extract_hot_cold(tb.kvs, txn, layout);
			}
			RC res = tb.my_execute(txn);
			if (res == ROLLBACK) {
				fprintf(stderr, "Rollback.\n");
				txn_iter.retry_txn(e.value());
			} else {
				committed += 1;
			}
			txn_num += 1;
		}
		db.msg_handler->barrier.wait_workers();
	}
}
