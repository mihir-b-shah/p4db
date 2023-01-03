#include "declustered_layout.hpp"
#include "switch_simulator.hpp"
#include "transaction.hpp"

int main() {
    using namespace declustered_layout;

    DeclusteredLayout dcl;
    std::vector<Transaction> txns;

    for (int i = 0; i < 10000; ++i) {
        Transaction t;
        t.generate_n(8);
        t.rerun(1);
        t.generate_chain_dep();
        dcl.add_sample(t);
        txns.emplace_back(t);
    }
    dcl.compute_layout(false, false);
    printf("# covered records: %lu\n", dcl.get_layout_coverage());

    {
        printf("---- ONLY SAMPLES ----\n");
        // just directly on sample
        SwitchSimulator sim{dcl};
        sim.include_deps = false;
        sim.process(txns);
    }

    {
        printf("---- ALL TXNS ----\n");
        // total
        SwitchSimulator sim{dcl};
        sim.include_deps = false;
        txns.clear();
        for (int i = 0; i < 10000; ++i) {
            Transaction t;
            t.generate_n(8);
            t.rerun(1);
            t.generate_chain_dep();
            txns.emplace_back(t);
        }
        sim.process(txns);
    }



    return 0;
}
