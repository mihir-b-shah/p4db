
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

layout_t get_layout(const std::vector<txn_t>& txns) {
    // a simple spraying solution.
    layout_t layout;
    batch_help::uniq_op_iter_t op_iter(txns);

    for (size_t i = 0; i<SLOTS_PER_REG; ++i) {
        for (size_t s = 0; s<N_STAGES; ++s) {
            for (size_t r = 0; r<REGS_PER_STAGE; ++r) {
                if (!op_iter.valid()) {
                    goto end;
                }

                db_key_t key = op_iter.get();
                tuple_loc_t tl = {s, r, i};
                layout.insert({key, tl});
                op_iter.advance();
            }
        }
    }

    end:
    return layout;
}

sw_txn_t::sw_txn_t(size_t port, const layout_t& layout, const txn_t& txn) 
    : port(port), id(0), pass_ct(0), orig_txn(txn), valid(true) {

    std::array<tuple_loc_t, 100> tmp;
    size_t num_ops = txn.ops.size();
    assert(num_ops <= 100);

    for (size_t i = 0; i<num_ops; ++i) {
        tmp[i] = layout.find(txn.ops[i])->second;
    }
    std::sort(tmp.begin(), tmp.begin() + num_ops, [](const tuple_loc_t& p1, const tuple_loc_t& p2){
        if (p1.stage == p2.stage) {
            return p1.reg < p2.reg;
        } else {
            return p1.stage < p2.stage;
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
        passes[0].grid[tl.stage][tl.reg] = tl.idx;
        size_t start = i++;
        while (i < num_ops && tmp[i] == tl) {
            passes[i-start].grid[tl.stage][tl.reg] = tl.idx;
            i += 1;
        }
    }
}
