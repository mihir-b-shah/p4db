
#include "sim.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <utility>

layout_t get_layout(const std::vector<txn_t>& txns) {
    // strawman
    layout_t layout;
    if (txns.size() == 0) {
        return layout;
    }
    
    size_t txn_id = 0;
    size_t op_id = 0;

    for (size_t i = 0; i<SLOTS_PER_REG; ++i) {
        for (size_t s = 0; s<N_STAGES; ++s) {
            for (size_t r = 0; r<REGS_PER_STAGE; ++r) {
                assert(txn_id < txns.size() && op_id < txns[txn_id].ops.size());
                tuple_loc_t tl = {s, r, i};
                layout.insert({txns[txn_id].ops[op_id], tl});
                if (txn_id == txns.size()-1 && op_id == txns[txn_id].ops.size()-1) {
                    goto end;
                } else if (op_id == txns[txn_id].ops.size()-1) {
                    txn_id += 1;
                    op_id = 0;
                } else {
                    op_id += 1;
                }
            }
        }
    }

    end:
    return layout;
}

sw_txn_t::sw_txn_t(size_t port, const layout_t& layout, const txn_t& txn) : port(port), id(0), pass_ct(0) {
    std::array<tuple_loc_t, 100> tmp;
    size_t num_ops = txn.ops.size();
    assert(num_ops <= 100);

    for (size_t i = 0; i<txn.ops.size(); ++i) {
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
