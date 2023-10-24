
#include <comm/comm.hpp>
#include <ee/executor.hpp>

#include <bitset>
#include <utility>
#include <algorithm>
#include <mutex>
#include <thread>
#include <pthread.h>
#include <ctime>
#include <limits>
#include <optional>
#include <ctime>

static uint64_t micros_diff(struct timespec* t_start, struct timespec* t_end) {
    uint64_t s_micros = ((((uint64_t) t_start->tv_sec) * 1000000000) + t_start->tv_nsec) / 1000;
    uint64_t e_micros = ((((uint64_t) t_end->tv_sec) * 1000000000) + t_end->tv_nsec) / 1000;
    return e_micros-s_micros;
}
	
RC TxnExecutor::my_execute(Txn& arg, void** packet_fill) {
	int rc = clock_gettime(CLOCK_MONOTONIC, &ts_txn_begin);
	assert(rc == 0);

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

        /*
        int prio = -1;
        if (arg.hottest_cold_i1.has_value()) {
            if (op.id == arg.cold_ops[arg.hottest_cold_i1.value()].id) {
                prio = 1;
            }
        }
        if (arg.hottest_cold_i2.has_value()) {
            if (op.id == arg.cold_ops[arg.hottest_cold_i2.value()].id) {
                prio = 2;
            }
        }
        */

		if (!ops[i]) {
            // fprintf(stderr, "R mb=%u thr=%u id=%lu k=%lu(%d)\n", mini_batch_num, WorkerContext::get().tid, arg.loader_id, op.id, prio);

			struct timespec ts_curr;
			rc = clock_gettime(CLOCK_MONOTONIC, &ts_curr);
			assert(rc == 0);
			t_abort += micros_diff(&ts_txn_begin, &ts_curr);
			return rollback();
		} else {
            // fprintf(stderr, "C mb=%u thr=%u id=%lu k=%lu(%d)\n", mini_batch_num, WorkerContext::get().tid, arg.loader_id, op.id, prio);
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

	*packet_fill = db.hot_send_q.alloc_slot(mini_batch_num, &arg);
	// locks automatically released
	RC ret = commit();

	struct timespec ts_curr;
	rc = clock_gettime(CLOCK_MONOTONIC, &ts_curr);
	assert(rc == 0);
	t_commit += micros_diff(&ts_txn_begin, &ts_curr);

	return ret;
}

RC TxnExecutor::execute(Txn& arg) {
	arg.id.field.valid = false;
	ts = ts_factory.get();
	// std::stringstream ss;
	// ss << "Starting txn tid=" << tid << " ts=" << ts << '\n';
	// std::cout << ss.str();

	// acquire all locks first, ex and shared. Can rollback within loop
	TupleFuture<KV>* ops[N_OPS];
	for (size_t i = 0; auto& op : arg.cold_ops) {
		if (op.mode == AccessMode::WRITE) {
			ops[i] = write(kvs, op, arg.id);
		} else if (op.mode == AccessMode::READ) {
			ops[i] = read(kvs, op, arg.id);
		} else {
			// fprintf(stderr, "i: %lu, id: %lu\n", i, arg.loader_id);
			assert(ORIG_MODE && op.mode == AccessMode::INVALID);
			break;
		}

		if (!ops[i]) {
            this->n_aborts += 1;
			return rollback();
		}
		++i;
	}

	// Use obtained write-locks to write values
	for (size_t i = 0; auto& op : arg.cold_ops) {
		if (op.mode == AccessMode::WRITE) {
			auto x = ops[i]->get();
			if (!x) {
                this->n_aborts += 1;
				return rollback();
			}
			x->value = op.value;
		} else if (op.mode == AccessMode::READ) {
			const auto x = ops[i]->get();
			if (!x) {
                this->n_aborts += 1;
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
        /*  TODO remember we are truncating 3+ pass txns, maybe emulate some additional
            latency here? (as well as contention on-switch, but I think 2-pass txns will
            do that trick for me. */
		atomic(p4_switch, arg);
	}

	// locks automatically released
    this->n_commits += 1;
	return commit();
}

static constexpr size_t DELAY_US = 0;
RC TxnExecutor::commit() {
	// TODO: log should not clear until the end of a batch.
	// TODO: now, the undolog has in the future both the value,last_acq fields to be written.

	// do logging
	struct timespec ts_now, ts_curr;
	int rc = clock_gettime(CLOCK_MONOTONIC, &ts_now);
	assert(rc == 0);

	do {
		int rc = clock_gettime(CLOCK_MONOTONIC, &ts_curr);
		assert(rc == 0);
	} while (micros_diff(&ts_now, &ts_curr) < DELAY_US);

	log.commit(ts);
	mempool.clear();
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
		return future;
	}

	AccessMode mode = AccessMode::READ;
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
		int rc;
		struct timespec ts_send_s, ts_send_f;
		rc = clock_gettime(CLOCK_MONOTONIC, &ts_send_s);
		assert(rc == 0);

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

		rc = clock_gettime(CLOCK_MONOTONIC, &ts_send_f);
		assert(rc == 0);
		t_local += micros_diff(&ts_send_s, &ts_send_f);

		return future;
	}

	AccessMode mode = AccessMode::WRITE;
	auto pkt = db.comm->make_pkt();
	auto req = pkt->ctor<msg::TupleGetReq>(ts, table->id, op.id, mode, id);
	req->sender = db.comm->node_id;
	req->me_pack = id.get_packed();

	auto future = mempool.allocate<Future_t>();
	auto msg_id = db.msg_handler->set_new_id(req);
	//printf("LINE:%d Inserting for msg_id=%lu, future=%p\n", __LINE__, msg_id.value, future);
	db.msg_handler->add_future(msg_id, future);

	int rc;
	struct timespec ts_send_s, ts_send_f;
	rc = clock_gettime(CLOCK_MONOTONIC, &ts_send_s);
	assert(rc == 0);

	db.comm->send(loc_info.target, pkt, tid);
	log.add_remote_write(future, loc_info.target);
	if (!future->get()) [[unlikely]] {
		return nullptr;
	}

	rc = clock_gettime(CLOCK_MONOTONIC, &ts_send_f);
	assert(rc == 0);
	t_comm += micros_diff(&ts_send_s, &ts_send_f);
	
	return future;
}

TupleFuture<KV>* TxnExecutor::insert(StructTable* table) {
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
	return future;
}

void TxnExecutor::atomic(SwitchInfo& p4_switch, const Txn& arg) {
    char buf[HOT_TXN_PKT_BYTES];
    p4_switch.make_txn(arg, &buf[0]);

    struct iovec ivec = {&buf[0], HOT_TXN_PKT_BYTES};
    struct msghdr msg_hdr;
    sw_intf.prepare_msghdr(&msg_hdr, &ivec);

    struct timespec ts_bef;
    int rc = clock_gettime(CLOCK_REALTIME, &ts_bef);
    assert(rc == 0);

    ssize_t sent = sendmsg(sw_intf.sockfd, &msg_hdr, 0);
    assert(sent == HOT_TXN_PKT_BYTES);
    ssize_t received = recvmsg(sw_intf.sockfd, &msg_hdr, 0);

    struct timespec ts_aft;
    rc = clock_gettime(CLOCK_REALTIME, &ts_aft);
    assert(rc == 0);

    t_send += micros_diff(&ts_bef, &ts_aft);

    if (received == -1) {
        assert(errno == EAGAIN || errno == EWOULDBLOCK);
        this->n_dropped += 1;
    } else {
        assert(received == HOT_TXN_PKT_BYTES);
    }
}

static void reset_db_batch(Database* db) {
	// __atomic_store_n(&db->thr_batch_done_ct, 0, __ATOMIC_SEQ_CST);
    // db->hot_send_q.done_sending();
}

static size_t accel_time = 0;

void TxnExecutor::run_txn(scheduler_t& sched, bool enqueue_aborts, std::queue<txn_pos_t>& q) {
    assert(q.empty() == false);
    txn_pos_t e = q.front();
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

	struct timespec ts_exec_s, ts_exec_f;
	clock_gettime(CLOCK_MONOTONIC, &ts_exec_s);
        RC res = my_execute(txn, &pkt_buf);
	clock_gettime(CLOCK_MONOTONIC, &ts_exec_f);
	accel_time += micros_diff(&ts_exec_s, &ts_exec_f);

        if (res == ROLLBACK) {
            this->n_aborts += 1;
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
                // assert(txn.cold_ops[N_OPS-1].mode != AccessMode::INVALID);
                assert(txn.cold_ops[cold_p].mode != AccessMode::INVALID);
                txn.do_accel = false;
                this->n_cold_fallbacks += 1;

                // printf("Txn %lu leftover\n", txn.loader_id);
                leftover_txns.push(e);
            }
        } else {
            this->n_commits += 1;
            if (CHECK_DISJOINT_KEYS) {
                for (size_t p = 0; p<N_OPS && txn.cold_ops[p].mode != AccessMode::INVALID; ++p) {
                    sched.touched.insert(txn.cold_ops[p].id);
                }
            }

            // fprintf(stderr, "Txn %lu committed\n", txn.loader_id);
            // fprintf(stderr, "Called make_txn from executor.\n");
            p4_switch.make_txn(txn, pkt_buf);
        }
    } else {
        // printf("Txn %lu leftover\n", txn.loader_id);
        this->n_cold_fallbacks += 1;
        leftover_txns.push(e);
    }
}

void TxnExecutor::run_leftover_txns() {
    /*  TODO potential livelock problems, what if two txns on different nodes keep aborting each
        other, and the leftover queues on both are very small, so they have no chance to separate? */
    size_t n_orig = leftover_txns.size();
    while (leftover_txns.size() > n_orig/100) {
        txn_pos_t e = leftover_txns.front();
        Txn& txn = entry_to_txn(this, e);
        RC result = execute(txn);
        if (result == ROLLBACK) {
            leftover_txns.push(e);
	}
        leftover_txns.pop();
    }
    while (leftover_txns.size() > 0) leftover_txns.pop();
}

void single_db_section(void* arg) {
    TxnExecutor* tb = (TxnExecutor*) arg;

    struct timespec ts_begin;
    int rc = clock_gettime(CLOCK_MONOTONIC, &ts_begin);
    assert(rc == 0);

    tb->run_leftover_txns();
    assert(tb->db.hot_send_q.send_q_tail == 0);

    struct timespec ts_end;
    rc = clock_gettime(CLOCK_MONOTONIC, &ts_end);
    assert(rc == 0);
    tb->t_leftover += micros_diff(&ts_begin, &ts_end);
}

extern std::vector<uint64_t> wait_workers_times[32];
extern uint64_t wait_workers_time[32];
extern uint64_t wait_nodes_time[32];
extern uint64_t crit_wait_time[32];
extern uint64_t log_wait_time[32];

void txn_executor(Database& db, std::vector<Txn>& txns) {
    int rc;
    struct timespec ts_begin;
    rc = clock_gettime(CLOCK_REALTIME, &ts_begin);
    assert(rc == 0);

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
    tb.my_txns = &txns;

	scheduler_t sched(&tb);

	for (size_t i = 0; i<txns.size(); i+=batch_tgt) {
		size_t batch_num = i/batch_tgt;
		sched.sched_batch(txns, i, i+batch_tgt);

        // first run stuff easily- everyone hits soft batches- equivalent to hard.
        size_t orig_mb_num = tb.mini_batch_num;
        while (tb.mini_batch_num - orig_mb_num < BATCH_SIZE_TGT/MINI_BATCH_SIZE_TGT) {
            auto& q = sched.mb_queues[(tb.mini_batch_num-1) % sched.n_queues];
            size_t txn_num = 0;
            if (CHECK_DISJOINT_KEYS) {
                sched.process_touched(tb.mini_batch_num);
            }

	    struct timespec ts_bef_bar;
	    rc = clock_gettime(CLOCK_MONOTONIC, &ts_bef_bar);
	    assert(rc == 0);

	    size_t old_time = accel_time;
            while (txn_num < mini_batch_tgt && !q.empty()) {
                /*
                txn_pos_t e = q.front();
                Txn& txn = entry_to_txn(sched.exec, e);
                fprintf(stderr, "Running txn=%lu from q=%lu\n", txn.loader_id, (tb.mini_batch_num-1) % sched.n_queues);
                */

                tb.run_txn(sched, true, q);
                txn_num += 1;
            }
            tb.mini_batch_num += 1;

	    struct timespec ts_aft_bar;
	    rc = clock_gettime(CLOCK_MONOTONIC, &ts_aft_bar);
	    assert(rc == 0);

	    tb.t_btwn.push_back(accel_time - old_time);
		fprintf(stderr, "T %d %d %d\n", WorkerContext::get().tid, tb.mini_batch_num, micros_diff(&ts_bef_bar, &ts_aft_bar));

			db.msg_handler->barrier.wait_workers();
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
		db.msg_handler->barrier.wait_workers();

        // thread 0 is the leader thread.
        if (thread_id == 0) {
            struct timespec ts_start;
            rc = clock_gettime(CLOCK_REALTIME, &ts_start);
            assert(rc == 0);

            db.wait_sched_ready();

            __sync_synchronize();
            run_hot_period(tb, layout);
            db.update_alloc(1+batch_num);

            db.hot_send_q.done_sending();
            __sync_synchronize();

            struct timespec ts_end;
            rc = clock_gettime(CLOCK_REALTIME, &ts_end);
            assert(rc == 0);

            fprintf(stderr, "Hot micros: %lu\n", micros_diff(&ts_start, &ts_end));
        }

        db.batch_bar.wait(&tb);
	}

    struct timespec ts_final;
    rc = clock_gettime(CLOCK_REALTIME, &ts_final);
    assert(rc == 0);

    if (WorkerContext::get().tid == 0) {

	    printf("worker %u, n_(accel)_commits: %lu\n", WorkerContext::get().tid, tb.n_commits);
	    printf("worker %u, n_(accel)_aborts: %lu\n", WorkerContext::get().tid, tb.n_aborts);
	    printf("worker %u, n_(accel)_packet_drops: %lu\n", WorkerContext::get().tid, tb.n_dropped);
	    printf("worker %u, n_cold_fallbacks: %lu\n", WorkerContext::get().tid, tb.n_cold_fallbacks);
	    printf("worker %u, barrier_wait_micros: %lu\n", WorkerContext::get().tid, crit_wait_time[WorkerContext::get().tid]);
	    printf("worker %u, ww_micros: %lu\n", WorkerContext::get().tid, wait_workers_time[WorkerContext::get().tid]);
	   
	    std::vector<uint64_t>& wwts = wait_workers_times[WorkerContext::get().tid];
	    std::sort(wwts.begin(), wwts.end());

	    std::vector<uint64_t>& t_btwn = tb.t_btwn;
	    std::sort(t_btwn.begin(), t_btwn.end());
	    
	    printf("worker %u, n_btwn: %lu, min: %lu, 10%%: %lu, 50%%: %lu, 60%%: %lu, 70%%: %lu, 80%%: %lu, 90%%: %lu, 99%%: %lu, max: %lu\n", WorkerContext::get().tid, t_btwn.size(), t_btwn[0], t_btwn[t_btwn.size()/10], t_btwn[t_btwn.size()/2], t_btwn[3*t_btwn.size()/5], t_btwn[7*t_btwn.size()/10], t_btwn[4*t_btwn.size()/5], t_btwn[9*t_btwn.size()/10], t_btwn[99*t_btwn.size()/100], t_btwn[t_btwn.size()-1]);
	    printf("worker %u, n_ww_micros: %lu, min: %lu, 10%%: %lu, 50%%: %lu, 60%%: %lu, 70%%: %lu, 80%%: %lu, 90%%: %lu, 99%%: %lu, max: %lu\n", WorkerContext::get().tid, wwts.size(), wwts[0], wwts[wwts.size()/10], wwts[wwts.size()/2], wwts[3*wwts.size()/5], wwts[7*wwts.size()/10], wwts[4*wwts.size()/5], wwts[9*wwts.size()/10], wwts[99*wwts.size()/100], wwts[wwts.size()-1]);
	    printf("worker %u, wn_micros: %lu\n", WorkerContext::get().tid, wait_nodes_time[WorkerContext::get().tid]);
	    printf("Total micros: %lu\n", micros_diff(&ts_begin, &ts_final));
	    printf("t_commit: %lu\n", tb.t_commit);
	    printf("t_abort: %lu\n", tb.t_abort);
	    printf("t_comm: %lu\n", tb.t_comm);
	    printf("t_local: %lu\n", tb.t_local);
	    printf("t_leftover: %lu\n", tb.t_leftover);
	    printf("worker %u, log_wait_micros: %lu\n", WorkerContext::get().tid, log_wait_time[WorkerContext::get().tid]);
     }
}

void orig_txn_executor(Database& db, std::vector<Txn>& txns) {
    int rc;

    struct timespec ts_begin;
    rc = clock_gettime(CLOCK_REALTIME, &ts_begin);
    assert(rc == 0);


	auto& config = Config::instance();
    TxnExecutor tb{db};

    // not necessary to make it aligned, but for compatability, to execute same # of txns.
	const size_t n_threads = config.num_txn_workers;
	const size_t batch_tgt = BATCH_SIZE_TGT/n_threads;
	size_t thread_id = WorkerContext::get().tid;

    txns.resize((txns.size() / batch_tgt) * batch_tgt);
	assert(txns.size() % batch_tgt == 0);
    tb.my_txns = &txns;

    fprintf(stderr, "Starting main txns.\n");
    for (size_t i = 0; i<txns.size(); ++i) {
        extract_hot_cold(tb.kvs, txns[i], config.decl_layout);
        assert(txns[i].init_done);
        RC result = tb.execute(txns[i]);
        if (result == ROLLBACK) {
            tb.leftover_txns.push(i);
        }
    }

    struct timespec ts_mid;
    rc = clock_gettime(CLOCK_REALTIME, &ts_mid);
    assert(rc == 0);

    fprintf(stderr, "Finished main txns.\n");

    struct timespec ts_mid2;
    rc = clock_gettime(CLOCK_REALTIME, &ts_mid2);
    assert(rc == 0);

    tb.run_leftover_txns();
    fprintf(stderr, "Finished leftover txns.\n");

    struct timespec ts_final;
    rc = clock_gettime(CLOCK_REALTIME, &ts_final);
    assert(rc == 0);

    if (WorkerContext::get().tid == 0) {
    	fprintf(stderr, "worker %u, Total micros: %lu\n", WorkerContext::get().tid, micros_diff(&ts_begin, &ts_final));
	fprintf(stderr, "worker %u, t_send: %lu\n", WorkerContext::get().tid, tb.t_send);
    printf("worker %u, n_(accel)_commits: %lu\n", WorkerContext::get().tid, tb.n_commits);
    printf("worker %u, n_(accel)_aborts: %lu\n", WorkerContext::get().tid, tb.n_aborts);
    printf("worker %u, barrier_wait_micros: %lu\n", WorkerContext::get().tid, crit_wait_time[WorkerContext::get().tid]);
    printf("worker %u, ww_micros: %lu\n", WorkerContext::get().tid, wait_workers_time[WorkerContext::get().tid]);
    printf("worker %u, wn_micros: %lu\n", WorkerContext::get().tid, wait_nodes_time[WorkerContext::get().tid]);
	    printf("t_comm: %lu\n", tb.t_comm);
	    printf("t_local: %lu\n", tb.t_local);
	    printf("t_mid: %lu\n", micros_diff(&ts_begin, &ts_mid));
	    printf("t_mid2: %lu\n", micros_diff(&ts_begin, &ts_mid2));
	    printf("t_leftover: %lu\n", tb.t_leftover);
	    printf("worker %u, log_wait_micros: %lu\n", WorkerContext::get().tid, log_wait_time[WorkerContext::get().tid]);
    }
}
