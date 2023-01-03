#include "transaction.hpp"
#include "declustered_layout.hpp"

#include <vector>

using namespace declustered_layout;

#define OPS_PER_TXN 8

void compute_layout(std::vector<Transaction>& txns) {

    size_t n_very_hot = txns.size() * OPS_PER_TXN * 0.1;
    std::map<uint64_t, size_t> cts;
    for (const Transaction& txn : txns) {
        for (uint64_t access : txn.accesses) {
            cts[access] += 1;
        }
    }

    for (auto it = cts.rbegin(); it != cts.rend(); it++) {
        it->first
        it->second
    }
}

int main() {
    using namespace declustered_layout;

    std::vector<Transaction> txns;
    for (int i = 0; i < 10000; ++i) {
        Transaction t;
        t.generate_n(OPS_PER_TXN);
        t.rerun(1);
        t.generate_chain_dep();
        txns.emplace_back(t);
    }
    compute_layout(txns);

    return 0;
}
