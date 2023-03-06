
#include <ee/executor.hpp>

// TxnIterator code to simplify execution
TxnExecutor::TxnIterator::TxnIterator(TxnExecutor& txn_exec) : exec(txn_exec), read_p(0), write_p(0) {
	orig_sched = reinterpret_cast<in_sched_entry_t*>(txn_exec.raw_buf);
}

std::optional<in_sched_entry_t> TxnExecutor::TxnIterator::next_entry() {
	in_sched_entry_t entry = orig_sched[read_p];
	if (entry.idx == SENTINEL_POS) {
		orig_sched[write_p].idx = SENTINEL_POS;
		read_p = 0;
		write_p = 0;
		entry = orig_sched[read_p];
	}
	read_p += 1;
	if (orig_sched[0].idx == SENTINEL_POS) {
		return std::nullopt;
	} else {
		return entry;
	}
}

Txn& TxnExecutor::TxnIterator::entry_to_txn(in_sched_entry_t entry) {
	return (*exec.db.per_core_txns[entry.thr_id])[entry.idx];
}

void TxnExecutor::TxnIterator::retry_txn(in_sched_entry_t entry) {
	// fprintf(stderr, "write_p: %lu\n", write_p);
	orig_sched[write_p++] = entry;
}
