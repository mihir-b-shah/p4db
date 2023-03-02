
#include <ee/executor.hpp>

#include <utility>
#include <algorithm>
#include <mutex>
#include <thread>
#include <pthread.h>
#include <limits>

RC TxnExecutor::execute_for_batch(Txn& arg) {
	// acquire all locks first, ex and shared. Can rollback within loop

	TupleFuture<KV>* ops[NUM_OPS];
	for (size_t i = 0; auto& op : arg.cold_ops) {
		if (op.mode == AccessMode::WRITE) {
			ops[i] = write(kvs, op);
		} else if (op.mode == AccessMode::READ) {
			ops[i] = read(kvs, op);
		} else {
			assert(op.mode == AccessMode::INVALID);
			ops[i] = nullptr;
		}
		if (!ops[i]) {
			leftover.push_back(arg);
			return rollback();
		}
		++i;
	}

	TxnId dep;
	size_t depend_size = 0;
	// Use obtained write-locks to write values
	for (size_t i = 0; auto& op : arg.cold_ops) {
		if (op.mode == AccessMode::WRITE) {
			auto x = ops[i]->get();
			if (!x) {
				leftover.push_back(arg);
				return rollback();
			}
			if (ops[i]->last_writer.epoch_id == arg.id.epoch_id) {
				dep = ops[i]->last_writer;
				depend_size += 1;
			}
			x->value = op.value;
			ops[i]->last_writer = arg.id;
		} else if (op.mode == AccessMode::READ) {
			const auto x = ops[i]->get();
			if (!x) {
				leftover.push_back(arg);
				return rollback();
			}
			// checking less than can be vulnerable to epoch wrap-around.
			if (ops[i]->last_writer.epoch_id == arg.id.epoch_id) {
				dep = ops[i]->last_writer;
				depend_size += 1;
			}
			const auto value = x->value;
			do_not_optimize(value);
		} else {
			break;
		}
		++i;
	}

	printf("depend_size: %lu\n", depend_size);
	if (depend_size == 0) {
		// common case- no dependencies.
	} else if (depend_size == 1) {
		// I am dependent on txn {dep}.
		printf("txn %u depends on %u\n", arg.id.get_packed(), dep.get_packed()); 
	} else {
		// Too many dependencies, run via 2-phase commit.
		leftover.push_back(arg);
		return rollback();
	}

	// locks automatically released
	return commit();
}

RC TxnExecutor::execute(Txn& arg) {
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
			ops[i] = write(kvs, op);
		} else if (op.mode == AccessMode::READ) {
			ops[i] = read(kvs, op);
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
	// TODO: now, the undolog has in the future both the value,last_writer fields to be written.
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

TupleFuture<KV>* TxnExecutor::read(StructTable* table, const Txn::OP& op) {
	using Future_t = TupleFuture<KV>; // TODO return with const KV
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
	auto req = pkt->ctor<msg::TupleGetReq>(ts, table->id, op.id, mode);
	req->sender = db.comm->node_id;

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

TupleFuture<KV>* TxnExecutor::write(StructTable* table, const Txn::OP& op) {
	using Future_t = TupleFuture<KV>;

	auto loc_info = op.loc_info;

	if constexpr (error::LOG_TABLE) {
		std::stringstream ss;
		ss << "write to " << table->name << " key=" << op.id << " is_local=" << loc_info.is_local
		   << " target=" << loc_info.target << '\n';
		std::cout << ss.str();
	}

	if (loc_info.is_local) {
		WorkerContext::get().cycl.start(stats::Cycles::local_latency);
		auto future = mempool.allocate<Future_t>();
		future->tuple = nullptr;
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
	auto req = pkt->ctor<msg::TupleGetReq>(ts, table->id, op.id, mode);
	req->sender = db.comm->node_id;

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
		if (i > cold_p) {
			txn.cold_ops[i].mode = AccessMode::INVALID;
		}
	}
	txn.cold_all_local = cold_all_local;
	if (cold1_lv > 0) {
		txn.hottest_local_cold_k = cold1_lk;
	}
	if (cold1_gv > 0) {
		txn.hottest_any_cold_k = cold1_gk;
	}
}

void txn_executor(Database& db, std::vector<Txn>& txns) {
	auto& config = Config::instance();
    TxnExecutor tb{db};
	size_t node_id = config.node_id;
	DeclusteredLayout* layout = config.decl_layout;
	size_t thread_id = WorkerContext::get().tid;
	const size_t n_threads = config.num_txn_workers;
	const size_t batch_tgt = BATCH_SIZE_TGT/n_threads;
	size_t batch_num = 0;
	size_t txn_num = 0;
	std::vector<size_t> shuffle_order;
	shuffle_order.reserve(batch_tgt);
	// just a heuristic, change if allocs happening
	std::vector<size_t> rest_order;
	rest_order.reserve(batch_tgt*0.05);

	while (txn_num < txns.size()) {
		size_t orig_txn_num = txn_num;
		size_t pkt_pos = 0;
		while (txn_num < txns.size() && txn_num-orig_txn_num < batch_tgt) {
			Txn& txn = txns[txn_num];
			extract_hot_cold(tb.kvs, txn, layout);
			if (txn.hottest_any_cold_k.has_value()) {
				assert(txn.hottest_any_cold_k.value() < (1ULL<<40));
				tb.sched_packet_buf[pkt_pos].k = txn.hottest_any_cold_k.value();
				tb.sched_packet_buf[pkt_pos].idx = txn_num;
				pkt_pos += 1;
			} else {
				rest_order.push_back(txn_num);
			}
			txn_num += 1;
		}
		tb.sched_packet_buf[pkt_pos].k = 0xffffffffffULL;
		printf("Before send_sched.\n");
		tb.send_get_txn_sched();
		// TODO: maybe add a barrier here, just to make sure everyone starts off at same time.
		return;

		// batching loop.
		batch_num += 1;
	}

	/*
	// Start at epoch 1, b/c the initial states use epoch 0. We're fine to wrap-around to 0, though.
	size_t epoch_id = 1;
	while (1) {
		// TODO: remove, just for experiments.
		if (epoch_id >= 30) {
			break;
		}

		printf("epoch_id: %lu\n", epoch_id);
		// batch
		size_t batch_ct = 0;
		while (batch_ct < BATCH_SIZE_TGT/n_threads) {
			std::pair<Txn, Txn> hot_cold = get_hot_cold(txn_iter.next(), layout);
			Txn& cold = hot_cold.second;

			//	TODO: batch_ct right now increments whether the cold txn aborts or not.
			//	This is ok, but if the id's are used in packet loss detection, keep in mind
			//	what was dropped
			cold.id = TxnId(node_id, thread_id, batch_ct, epoch_id);
			RC rc = tb.execute_for_batch(cold);
			(void) rc;
			batch_ct += 1;
		}

		epoch_id = (epoch_id + 1) & ((1 << TxnId::EPOCH_BITS) - 1);
		db.msg_handler->barrier.wait_workers();
	}
	printf("Done.\n");
	*/
}
