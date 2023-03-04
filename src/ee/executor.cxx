
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
