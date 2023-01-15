
#include "sim.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <utility>
#include <optional>

namespace batch_help {
    class uniq_op_iter_t {
    public:
        uniq_op_iter_t(const std::vector<txn_t>& txns) : txns_(txns), txn_it_(0), op_it_(0) {
            assert(txns_.size() == 0 || valid());
        }

        db_key_t get() const {
            return txns_[txn_it_].ops[op_it_];
        }

        // guaranteed to be monotonic.
        bool valid() const {
            static bool past = true;
            bool ret = in_bounds() && visited_.find(get()) == visited_.end();
            assert(past || !ret);
            return ret;
        }

        void advance() {
            assert(valid());
            visited_.insert(get());
            while (in_bounds() && !valid()) {
                if (op_it_ == txns_[txn_it_].ops.size()-1) {
                    txn_it_ += 1;
                    op_it_ = 0;
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
    std::unordered_map<db_key_t, std::unordered_map<db_key_t, size_t>> adj_mat;
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
        const std::unordered_map<db_key_t, size_t>& adj_freqs = adj_mat[k];
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

layout_t::layout_t(const std::vector<txn_t>& txns)
    : keys_sorted_(get_key_cts(txns)), key_cts_(keys_sorted_.begin(), keys_sorted_.end()) {

    // naive_spray_impl(txns);
    freq_heuristic_impl(txns);
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
        locs.push_back(tmp[i]);
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
