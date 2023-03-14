
#include <ee/executor.hpp>

#include <utility>
#include <algorithm>
#include <optional>
#include <bitset>

void extract_hot_cold(StructTable* table, Txn& txn, DeclusteredLayout* layout) {
	static_assert(MAX_PASSES_ACCEL == 1 || MAX_PASSES_ACCEL == 2);

	/*	Keep track of the hottest 2 values.
		Kind of sketchy, since how do we know the frequency of every key? Just say in a real system,
		we would know for some fraction of keys, and for the others I don't care. */
	size_t cold1_i, cold2_i;
	size_t cold1_v = 0, cold2_v = 0;
	size_t hot_p1 = 0, hot_p2 = 0, cold_p = 0;
	
	bool cold_all_local = true;
	size_t i = 0;

	std::bitset<DeclusteredLayout::NUM_REGS> reg_usage;
	std::bitset<DeclusteredLayout::NUM_REGS> reg_usage_2;
	/*	useful since we want to make sure the least hot conflicts are on the second pass, 
		so re-order them on the fly */
	size_t hot_reorder_buf[DeclusteredLayout::NUM_REGS];
	size_t n_pass2_ops = 0;
	txn.do_accel = true;

	while (i < NUM_OPS && txn.cold_ops[i].mode != AccessMode::INVALID) {
		txn.cold_ops[i].loc_info = table->part_info.location(txn.cold_ops[i].id);
		std::pair<bool, TupleLocation> hot_info = layout->get_location(txn.cold_ops[i].id);
		if (hot_info.first) {
			if (!reg_usage.test(hot_info.second.reg_array_id)) {
				reg_usage.set(hot_info.second.reg_array_id);
				if (MAX_PASSES_ACCEL > 1) {
					hot_reorder_buf[hot_info.second.reg_array_id] = hot_p1; 
				}
				txn.hot_ops_pass1[hot_p1++] = {txn.cold_ops[i], hot_info.second};
				if (hot_info.second.lock_pos != DeclusteredLayout::NO_LOCK) {
					txn.locks_check.set(hot_info.second.lock_pos);
				}
			} else if (MAX_PASSES_ACCEL == 2 && !reg_usage_2.test(hot_info.second.reg_array_id) && n_pass2_ops < MAX_OPS_PASS2_ACCEL) {
				reg_usage_2.set(hot_info.second.reg_array_id);
				n_pass2_ops += 1;
				// does it make sense to swap the keys?
				size_t swap_idx = hot_reorder_buf[hot_info.second.reg_array_id];
				if (TupleLocation::total_order_gt(
						txn.hot_ops_pass1[swap_idx].second.dist_freq, txn.hot_ops_pass1[swap_idx].first.id,
						hot_info.second.dist_freq, txn.cold_ops[i].id)) {
					txn.hot_ops_pass2[hot_p2++] = {txn.cold_ops[i], hot_info.second};
				} else {
					txn.hot_ops_pass2[hot_p2++] = txn.hot_ops_pass1[swap_idx];
					txn.hot_ops_pass1[swap_idx] = {txn.cold_ops[i], hot_info.second};
				}
				if (hot_info.second.lock_pos != DeclusteredLayout::NO_LOCK) {
					txn.locks_check.set(hot_info.second.lock_pos);
					txn.locks_acquire.set(hot_info.second.lock_pos);
				}
			} else {
				txn.do_accel = false;
				// going to restore anyway, this path is very rare.
				txn.hot_ops_pass1[hot_p1++] = {txn.cold_ops[i], hot_info.second};
			}
		} else {
			if (hot_info.second.dist_freq > cold1_v) {
				cold1_i = cold_p;
				cold1_v = hot_info.second.dist_freq;
			} else if (hot_info.second.dist_freq > cold2_v) {
				cold2_i = cold_p;
				cold2_v = hot_info.second.dist_freq;
			}
			txn.cold_ops[cold_p++] = txn.cold_ops[i];
		}
		i += 1;
	}

	if (!txn.do_accel) {
		// rollback, just copy all the hot ops to the end of the cold ops.
		for (size_t i = 0; i<hot_p1; ++i) {
			txn.cold_ops[i+cold_p] = txn.hot_ops_pass1[i].first;
		}
		for (size_t i = 0; i<hot_p2; ++i) {
			txn.cold_ops[i+cold_p+hot_p1] = txn.hot_ops_pass2[i].first;
		}
		txn.hot_ops_pass1[0].first.mode = AccessMode::INVALID;
		txn.hot_ops_pass2[0].first.mode = AccessMode::INVALID;
	} else {
		// null-terminate all the op-lists.
		if (cold_p < NUM_OPS) {
			txn.cold_ops[cold_p].mode = AccessMode::INVALID;
		}
		if (hot_p1 < NUM_OPS) {
			txn.hot_ops_pass1[hot_p1].first.mode = AccessMode::INVALID;
		}
		if (MAX_PASSES_ACCEL == 2 && hot_p2 < MAX_OPS_PASS2_ACCEL) {
			txn.hot_ops_pass2[hot_p2].first.mode = AccessMode::INVALID;
		}
		if (cold1_v > 0) {
			txn.hottest_cold_i1 = cold1_i;
		}
		if (cold2_v > 0) {
			txn.hottest_cold_i2 = cold2_i;
		}
		assert(cold_p + hot_p1 + hot_p2 == NUM_OPS);
	}
	txn.init_done = true;
}
