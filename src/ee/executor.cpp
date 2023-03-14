
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
	
RC TxnExecutor::my_execute(Txn& arg, Communicator::Pkt_t** packet_fill) {
	arg.id.field.valid = true;
	assert(arg.id.field.mini_batch_id == mini_batch_num);
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
			ops[i]->last_acq = arg.id;
		} else {
			break;
		}
		++i;
	}

	Communicator::Pkt_t** ptr_slot = db.hot_send_q.alloc_slot(mini_batch_num);
	Communicator::Pkt_t* new_pkt_buf = db.comm->make_pkt();
	*ptr_slot = new_pkt_buf;
	*packet_fill = new_pkt_buf;

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
	/*
	if (false) { // TODO: arg.on_switch) {
		WorkerContext::get().cycl.reset(stats::Cycles::switch_txn_latency);
		WorkerContext::get().cycl.start(stats::Cycles::switch_txn_latency);
		SwitchFuture<SwitchInfo>* multi_f = atomic(p4_switch, arg);
		const auto values = multi_f->get().values;
		do_not_optimize(values);

		WorkerContext::get().cycl.stop(stats::Cycles::switch_txn_latency);
		WorkerContext::get().cycl.save(stats::Cycles::switch_txn_latency);
		return commit();
	}
	*/

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
		assert(future->last_acq.field.mini_batch_id == mini_batch_num);
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
		assert(future->last_acq.field.mini_batch_id == mini_batch_num);
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

/*
SwitchFuture<SwitchInfo>* TxnExecutor::atomic(SwitchInfo& p4_switch, const Txn& arg) {
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
*/

static void reset_db_batch(Database* db) {
	__atomic_store_n(&db->thr_batch_done_ct, 0, __ATOMIC_SEQ_CST);
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
	std::vector<Txn> aborted;
	assert(txns.size() % batch_tgt == 0);
	tb.mini_batch_num = 1;

	scheduler_t sched(&tb);

	// input of scheduler, maybe?
	for (size_t i = 0; i<txns.size(); i+=batch_tgt) {
		size_t batch_num = i/batch_tgt;
		sched.sched_batch(txns, i, i+batch_tgt);

		// execute a batch, break into mini-batches as we see fit.
		size_t txn_num = 0;
		size_t committed = 0;
		std::bitset<64> q_mask;
		std::bitset<64> all_q_mask = (1ULL << sched.n_queues)-1;
		assert(sched.n_queues < 64);
		//	TODO this breaks with multiple batches, need to fix this.

		while (1) {
			// TODO: policy choice- do we consider MINI_BATCH_TGT, or commit MINI_BATCH_TGT?
			// TODO: note, we need to keep the mini_batch_num consistent across nodes during execution.
			if (txn_num == MINI_BATCH_SIZE_THR_TGT) {
				auto& q = sched.mb_queues[tb.mini_batch_num % sched.n_queues];
				if (!q_mask.test(tb.mini_batch_num % sched.n_queues) 
					&& q.size() < MIN_MINI_BATCH_THR_SIZE) {
					// drain the queue, to avoid coming back again.
					while (!q.empty()) {
						tb.non_accel_txns.push_back(sched.entry_to_txn(q.front()));
						q.pop();
					}
					q_mask.set(tb.mini_batch_num % sched.n_queues);
					if (q_mask == all_q_mask) {
						//	TODO do I need sequential consistency here, is relaxed sufficient?
						__atomic_add_fetch(&db.thr_batch_done_ct, 1, __ATOMIC_SEQ_CST);
					}
				}

				fprintf(stderr, "mini_batch_num: %u, q.size(): %lu, thr_batch_done_ct: %u\n", tb.mini_batch_num, sched.mb_queues[tb.mini_batch_num % sched.n_queues].size(), db.thr_batch_done_ct);

				// TODO do I need sequential consistency here, is relaxed sufficient?
				if (__atomic_load_n(&db.thr_batch_done_ct, __ATOMIC_SEQ_CST) == n_threads) {
					fprintf(stderr, "Done with batch.\n");
					break;
				}
				
				// guaranteed that all local threads call this.
				db.msg_handler->barrier.wait_workers_soft();
				committed = 0;
				txn_num = 0;
				tb.mini_batch_num += 1;
			}

			if (q_mask != all_q_mask) {
				auto& q = sched.mb_queues[tb.mini_batch_num % sched.n_queues];
				if (!q.empty()) {
					in_sched_entry_t e = q.front();
					Txn& txn = sched.entry_to_txn(e);
					q.pop();

					txn.id = TxnId(true, node_id, tb.mini_batch_num);
					assert(txn.id.field.valid == true && txn.id.field.node_id == node_id && txn.id.field.mini_batch_id == tb.mini_batch_num);
					if (txn.do_accel) {
						//	TODO note this buffer is malloc-ed, seems excessive.
						Communicator::Pkt_t* pkt;
						RC res = tb.my_execute(txn, &pkt);
						if (res == ROLLBACK) {
							txn.n_aborts += 1;
							if (txn.n_aborts <= MAX_TIMES_ACCEL_ABORT) {
								q.push(e);
							} else {
								tb.non_accel_txns.push_back(txn);
							}
						} else {
							committed += 1;
							// now, time to fill out the packet buffer.
							auto txn_pkt = pkt->ctor<msg::SwitchTxn>();
							txn_pkt->sender = db.comm->node_id;
							size_t txn_size = tb.p4_switch.make_txn(txn, txn_pkt->data);
							pkt->resize(msg::SwitchTxn::size(txn_size));
						}
					} else {
						tb.non_accel_txns.push_back(txn);
					}
				} else if (!q_mask.test(tb.mini_batch_num % sched.n_queues)) {
					q_mask.set(tb.mini_batch_num % sched.n_queues);
					if (q_mask == all_q_mask) {
						//	TODO do I need sequential consistency here, is relaxed sufficient?
						__atomic_add_fetch(&db.thr_batch_done_ct, 1, __ATOMIC_SEQ_CST);
					}
				}
			}
			// we use this as a dummy to maintain same # of mini-batches across all threads.
			txn_num += 1;

			assert(tb.mini_batch_num < (1ULL << TxnId::MINI_BATCH_ID_WIDTH));
		}
		db.msg_handler->barrier.wait_workers_hard(&tb.mini_batch_num, reset_db_batch, &db);
		// call db.hot_send_q.done_sending()- needs to happen from one thread.
	}
}
