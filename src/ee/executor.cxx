
// copy of scheduling interface. I probably need some scheduling, just not this much.

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
	tb.send_get_txn_sched();

	TxnIterator txn_iter(tb);
	std::optional<in_sched_entry_t> e_res;
	while (1) {
		e_res = txn_iter.next_entry();
		if (!e_res.has_value()) {
			break;
		}
		Txn& txn = txn_iter.entry_to_txn(e_res.value());
		fprintf(stderr, "Got txn\n");
	}

	// TODO: maybe add a barrier here, just to make sure everyone starts off at same time.
	// batching loop.
	batch_num += 1;
}

/*
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
			if (ops[i]->last_acq.epoch_id == arg.id.epoch_id) {
				dep = ops[i]->last_acq;
				depend_size += 1;
			}
			x->value = op.value;
			ops[i]->last_acq = arg.id;
		} else if (op.mode == AccessMode::READ) {
			const auto x = ops[i]->get();
			if (!x) {
				leftover.push_back(arg);
				return rollback();
			}
			// checking less than can be vulnerable to epoch wrap-around.
			if (ops[i]->last_acq.epoch_id == arg.id.epoch_id) {
				dep = ops[i]->last_acq;
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
*/

RC TxnExecutor::execute_mini_batch(TxnIterator& iter) {
	/*
	// acquire all locks first, ex and shared. Can rollback within loop
	while (1) {
		std::optional<in_sched_entry_t> e = iter.next_entry();
		if (!e.has_value()) {
			break;
		}
	}
	
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

	*/
	// locks automatically released
	return commit();
}


// XXX: check before 3/1/2023 to see execute_for_batch- or use git bisect?
