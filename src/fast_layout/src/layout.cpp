
#include "sim.h"

#include <array>
#include <utility>

layout_t get_layout(const std::vector<txn_t>& txns) {
     
}

sw_txn_t::sw_txn_t(size_t port, const layout_t& layout, const txn_t& txn) : port(port), pass_ct(0) {
    std::array<std::pair<db_key_t, tuple_loc_t>> tmp[100];

    size_t num_ops = txn.ops.size();
    assert(num_ops <= 100);

    for (size_t i = 0; i<txn.ops.size(); ++i) {
        tmp[i] = {txn.ops[i], layout[txn.ops[i]]};
    }
    std::sort(tmp.begin(), tmp.begin() + num_ops, (const auto& p1, const auto& p2){
        if (p1.second.stage == p2.second.stage) {
            return p1.second.reg < p2.second.reg;
        } else {
            return p1.second.stage < p2.second.stage;
        }
    });

    // pass 1
    size_t i = 0;
    size_t n_passes = 0;
    while (i < num_ops) {
        tuple_loc_t tl = tmp[tl];
        size_t start = i++;
        while (i < num_ops && tmp[i].second == tl) {
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
        tuple_loc_t tl = tmp[i].second;
        passes[0][tl.stage][tl.reg] = tmp[i].first;
        size_t start = i++;
        while (i < num_ops && tmp[i].second == tl) {
            passes[i-start][tl.stage][tl.reg] = tmp[i].first;
            i += 1;
        }
    }
}
