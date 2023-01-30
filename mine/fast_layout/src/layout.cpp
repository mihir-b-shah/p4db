
#include "sim.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <optional>

namespace batch_help {
    class uniq_op_iter_t {
    public:
        uniq_op_iter_t(const std::vector<txn_t>& txns) : txns_(txns), txn_it_(0), op_it_(0) {
            while (txn_it_ < txns_.size() && txns_[txn_it_].ops.size() == 0) {
                txn_it_ += 1;
            }
            assert(txns_.size() == 0 || valid());
        }

        db_key_t get() const {
            return txns_[txn_it_].ops[op_it_];
        }

        // guaranteed to be monotonic.
        bool valid() const {
            return in_bounds() && visited_.find(get()) == visited_.end();
        }

        void advance() {
            assert(valid());
            visited_.insert(get());
            while (in_bounds() && !valid()) {
                if (op_it_ == txns_[txn_it_].ops.size()-1) {
                    do {
                        txn_it_ += 1;
                        op_it_ = 0;
                    } while (txn_it_ < txns_.size() && txns_[txn_it_].ops.size() == 0);
                } else {
                    op_it_ += 1;
                }
            }
        }

    private:
        const std::vector<txn_t>& txns_;
        size_t txn_it_;
        size_t op_it_;
        /* technically wasteful, the layout down below does the same thing,
            but keep simple for now. */
        std::unordered_set<db_key_t> visited_;

        bool in_bounds() const {
            return txn_it_ < txns_.size() && op_it_ < txns_[txn_it_].ops.size();
        }
    };
}

void layout_t::naive_spray_impl(const std::vector<txn_t>& txns) {
    batch_help::uniq_op_iter_t op_iter(txns);

    for (size_t i = 0; i<SLOTS_PER_REG; ++i) {
        for (size_t s = 0; s<N_STAGES; ++s) {
            for (size_t r = 0; r<REGS_PER_STAGE; ++r) {
                if (!op_iter.valid()) {
                    // done
                    return;
                }

                db_key_t key = op_iter.get();
                tuple_loc_t tl = {s, r, i};
                forward_[key] = tl;
                backward_per_reg_[tl.stage][tl.reg].insert({tl.idx, key});
                op_iter.advance();
            }
        }
    }
}

void layout_t::freq_heuristic_impl(const std::vector<txn_t>& txns) { 
	typedef std::unordered_map<db_key_t, std::unordered_map<db_key_t, size_t>> adj_mat_t;
	adj_mat_t adj_mat;
    for (const txn_t& txn : txns) {
        for (db_key_t op : txn.ops) {
            if (adj_mat.find(op) == adj_mat.end()) {
                adj_mat.insert({op, {}});
            }
            for (db_key_t op2 : txn.ops) {
                if (op != op2) {
                    if (adj_mat[op].find(op2) == adj_mat[op].end()) {
                        adj_mat[op].insert({op2, 0});
                    }
                    adj_mat[op][op2] += 1;
                }
            }
        }
    }

    size_t idx_to_alloc[N_STAGES][REGS_PER_STAGE] = {{0}};
    for (const auto& pr : keys_sorted_) {
        db_key_t k = pr.first;
        const std::unordered_map<db_key_t, size_t>& adj_freqs = adj_mat.find(k)->second;
        // printf("k: %lu, adj_mat.size(): %lu\n", k, adj_freqs.size());

        size_t reg_freqs[N_STAGES][REGS_PER_STAGE] = {{0}};

        for (const auto& adj : adj_freqs) {
            if (forward_.find(adj.first) != forward_.end()) {
                tuple_loc_t tl = forward_[adj.first];
                // printf("\t\ts=%lu, r=%lu, i=%lu\n", tl.stage, tl.reg, tl.idx);
                reg_freqs[tl.stage][tl.reg] += 1;
            }
        }

        size_t s_low = 0;
        size_t r_low = 0;
        for (size_t s = 0; s<N_STAGES; ++s) {
            for (size_t r = 0; r<REGS_PER_STAGE; ++r) {
                if (reg_freqs[s][r] < reg_freqs[s_low][r_low]) {
                    s_low = s;
                    r_low = r;
                }
            }
        }

        tuple_loc_t loc = {s_low, r_low, idx_to_alloc[s_low][r_low]++};
        // printf("\tassigned: s=%lu, r=%lu, i=%lu: freq=%lu\n", loc.stage, loc.reg, loc.idx, reg_freqs[s_low][r_low]);
        forward_[k] = loc;
        backward_per_reg_[loc.stage][loc.reg].insert({loc.idx, k});
    }
}

void layout_t::better_random_impl(const std::vector<txn_t>& txns) { 
	typedef std::unordered_map<db_key_t, std::unordered_map<db_key_t, size_t>> adj_mat_t;
	adj_mat_t adj_mat;
    for (const txn_t& txn : txns) {
        for (db_key_t op : txn.ops) {
            if (adj_mat.find(op) == adj_mat.end()) {
                adj_mat.insert({op, {}});
            }
            for (db_key_t op2 : txn.ops) {
                if (op != op2) {
                    if (adj_mat[op].find(op2) == adj_mat[op].end()) {
                        adj_mat[op].insert({op2, 0});
                    }
                    adj_mat[op][op2] += 1;
                }
            }
        }
    }

    size_t idx_to_alloc[N_STAGES][REGS_PER_STAGE] = {{0}};
    for (const auto& pr : keys_sorted_) {
        db_key_t k = pr.first;
        const std::unordered_map<db_key_t, size_t>& adj_freqs = adj_mat.find(k)->second;
        // printf("k: %lu, adj_mat.size(): %lu\n", k, adj_freqs.size());

        size_t reg_freqs[N_STAGES][REGS_PER_STAGE] = {{0}};

        for (const auto& adj : adj_freqs) {
            if (forward_.find(adj.first) != forward_.end()) {
                tuple_loc_t tl = forward_[adj.first];
                // printf("\t\ts=%lu, r=%lu, i=%lu\n", tl.stage, tl.reg, tl.idx);
                reg_freqs[tl.stage][tl.reg] += 1;
            }
        }

        size_t s_low = 0;
        size_t r_low = 0;
        for (size_t s = 0; s<N_STAGES; ++s) {
            for (size_t r = 0; r<REGS_PER_STAGE; ++r) {
                if (reg_freqs[s][r] < reg_freqs[s_low][r_low]) {
                    s_low = s;
                    r_low = r;
                }
            }
        }

        tuple_loc_t loc = {s_low, r_low, idx_to_alloc[s_low][r_low]++};
        // printf("\tassigned: s=%lu, r=%lu, i=%lu: freq=%lu\n", loc.stage, loc.reg, loc.idx, reg_freqs[s_low][r_low]);
        forward_[k] = loc;
        backward_per_reg_[loc.stage][loc.reg].insert({loc.idx, k});
    }
}

void layout_t::random_spray_impl(const std::vector<txn_t>& txns) {
    size_t idx_to_alloc[N_STAGES][REGS_PER_STAGE] = {{0}};
	for (const auto& pr : keys_sorted_) {
		size_t rnd = rand() % (N_STAGES*REGS_PER_STAGE);

		size_t stage = rnd / REGS_PER_STAGE;
		size_t reg = rnd % REGS_PER_STAGE;
		size_t idx = idx_to_alloc[stage][reg]++;

		db_key_t k = pr.first;
		tuple_loc_t tl = {stage, reg, idx};
		forward_.insert({k, tl});
		backward_per_reg_[stage][reg].insert({idx, k});
	}
}

layout_t::layout_t(const std::vector<txn_t>& txns)
    :	keys_sorted_(get_key_cts(txns)), 
		key_cts_(keys_sorted_.begin(), keys_sorted_.end()) {

    // naive_spray_impl(txns);
    freq_heuristic_impl(txns);
	// better_random_impl(txns);
	// random_spray_impl(txns);
}

size_t layout_t::get_key_ct(db_key_t key) const {
    return key_cts_.find(key)->second;
}

std::optional<tuple_loc_t> layout_t::lookup(db_key_t key) const {
    auto it = forward_.find(key);
    if (it == forward_.end()) {
        return std::nullopt;
    } else {
        return it->second;
    }
}

db_key_t layout_t::rev_lookup(size_t stage, size_t reg, size_t idx) const {
    auto it = backward_per_reg_[stage][reg].find(idx);
    assert(it != backward_per_reg_[stage][reg].end());
    return it->second;
}

sw_txn_t::sw_txn_t(size_t port, const layout_t& layout, const txn_t& txn) 
    : port(port), id(0), pass_ct(0), orig_txn(txn), valid(true) {

    std::array<tuple_loc_t, 100> tmp;
    size_t num_ops = txn.ops.size();
    assert(num_ops <= 100);

    for (size_t i = 0; i<num_ops; ++i) {
        tmp[i] = layout.lookup(txn.ops[i]).value();
    }
    std::sort(tmp.begin(), tmp.begin() + num_ops, [&layout](const tuple_loc_t& p1, const tuple_loc_t& p2){
        if (p1.stage != p2.stage) {
            return p1.stage < p2.stage;
        } else if (p1.reg != p2.reg) {
            return p1.reg < p2.reg;
        } else {
            db_key_t k1 = layout.rev_lookup(p1.stage, p1.reg, p1.idx);
            db_key_t k2 = layout.rev_lookup(p2.stage, p2.reg, p2.idx);
            return layout.get_key_ct(k2) < layout.get_key_ct(k1);
        }
    });
    for (size_t i = 0; i<num_ops; ++i) {
        locs.push_back(tmp[i]);
    }

    // pass 1
    size_t i = 0;
    size_t n_passes = 0;
    while (i < num_ops) {
        tuple_loc_t tl = tmp[i];
        size_t start = i++;
        while (i < num_ops && tmp[i] == tl) {
            i += 1;
        }
        if (i-start > n_passes) {
            n_passes = i-start;
        }
    }

    // pass 2- assign.
    passes.resize(n_passes);
    i = 0;
    while (i < num_ops) {
        tuple_loc_t tl = tmp[i];
        passes[0].grid[tmp[i].stage][tmp[i].reg] = tmp[i].idx;
        size_t start = i++;
        while (i < num_ops && tmp[i] == tl) {
            // just choose the first one we lock.
            if (!one_lock.has_value()) {
                one_lock = tmp[i];
            }
            passes[i-start].grid[tmp[i].stage][tmp[i].reg] = tmp[i].idx;
            i += 1;
        }
    }
}

std::vector<sw_txn_t> prepare_txns_sw(size_t port, const std::vector<txn_t>& txns, const layout_t& lay) {
	std::vector<sw_txn_t> sw_txns;
	std::vector<std::vector<tuple_loc_t>> to_lock;
	std::unordered_map<db_key_t, size_t> lock2_by_freq;
	std::unordered_map<db_key_t, size_t> lock_idxs;
	size_t total_freq = 0;

	for (const txn_t& txn : txns) {
		to_lock.emplace_back();
		sw_txns.emplace_back(port, lay, txn);
		const sw_txn_t& sw_txn = sw_txns.back();

		size_t pass2m_freqs[N_STAGES*REGS_PER_STAGE] = {};
		for (const tuple_loc_t& tl : sw_txn.locs) {
			size_t freq_idx = tl.stage * REGS_PER_STAGE + tl.reg;
			size_t k = lay.rev_lookup(tl.stage, tl.reg, tl.idx);
			size_t freq = lay.get_key_ct(k);
			if (pass2m_freqs[freq_idx] > 0) {
				if (lock2_by_freq.find(k) == lock2_by_freq.end()) {
					lock2_by_freq.insert({k, 0});
				}
				to_lock.back().push_back(tl);
				lock2_by_freq[k] += 1;
				total_freq += 1;
			}
			pass2m_freqs[freq_idx] = freq;
        }
	}

	std::vector<std::pair<db_key_t, size_t>> lock2_v(lock2_by_freq.begin(), lock2_by_freq.end());
	std::sort(lock2_v.begin(), lock2_v.end(), [](const auto& pr1, const auto& pr2){
		return pr2.second < pr1.second;
	});
	size_t lock_quantum = (total_freq+N_LOCKS-1)/N_LOCKS;
	size_t l2_p = 0;
	size_t lock_num = 0;

	while (l2_p < lock2_v.size()) {
		size_t fsum = lock2_v[l2_p].second;
		lock_idxs.insert({lock2_v[l2_p].first, lock_num});
		l2_p += 1;

		while (l2_p < lock2_v.size() && fsum < lock_quantum) {
			lock_idxs.insert({lock2_v[l2_p].first, lock_num});
			fsum += lock2_v[l2_p].second;
			l2_p += 1;
		}
		lock_num += 1;
	}
	printf("lock_num: %lu\n", lock_num);

	// i.e. every key that will need a lock has one.
	assert(lock_idxs.size() == lock2_by_freq.size());

	// now fill the locks.
	size_t sw_txn_num = 0;
	for (sw_txn_t& sw_txn : sw_txns) {
		std::vector<tuple_loc_t> to_lock_tls = to_lock[sw_txn_num];

		for (const tuple_loc_t& tl : sw_txn.locs) {
			size_t lock_idx = lock_idxs[lay.rev_lookup(tl.stage, tl.reg, tl.idx)];
			sw_txn.locks_check.set(lock_idx);
		}
		for (const tuple_loc_t& tl : to_lock_tls) {
			size_t lock_idx = lock_idxs[lay.rev_lookup(tl.stage, tl.reg, tl.idx)];
			sw_txn.locks_wanted.set(lock_idx);
		}
		sw_txn_num += 1;
	}

	return sw_txns;
}
