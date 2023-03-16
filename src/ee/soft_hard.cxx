
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
        if (txn_num == mini_batch_tgt) {
            auto& q = sched.mb_queues[(tb.mini_batch_num-1) % sched.n_queues];
            if (!q_mask.test((tb.mini_batch_num-1) % sched.n_queues) 
                && q.size() < MIN_MINI_BATCH_THR_SIZE) {
                // drain the queue, to avoid coming back again.
                while (!q.empty()) {
                    tb.non_accel_txns.push_back(sched.entry_to_txn(q.front()));
                    q.pop();
                }
                q_mask.set((tb.mini_batch_num-1) % sched.n_queues);
                if (q_mask == all_q_mask) {
                    //	TODO do I need sequential consistency here, is relaxed sufficient?
                    __atomic_add_fetch(&db.thr_batch_done_ct, 1, __ATOMIC_SEQ_CST);
                }
            }

            fprintf(stderr, "mini_batch_num: %u, q.size(): %lu, thr_batch_done_ct: %u\n", tb.mini_batch_num, sched.mb_queues[(tb.mini_batch_num-1) % sched.n_queues].size(), db.thr_batch_done_ct);

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
            assert(tb.mini_batch_num < (1ULL << TxnId::MINI_BATCH_ID_WIDTH));
        }

        if (q_mask != all_q_mask) {
            auto& q = sched.mb_queues[(tb.mini_batch_num-1) % sched.n_queues];
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
            } else if (!q_mask.test((tb.mini_batch_num-1) % sched.n_queues)) {
                q_mask.set((tb.mini_batch_num-1) % sched.n_queues);
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
		// db.msg_handler->barrier.wait_workers_hard(&tb.mini_batch_num, reset_db_batch, &db);
