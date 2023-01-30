
#include "sim.h"

#include <cstdio>

/*
In instacart, there are 100k txns.
We consider batches of 10k.
Frequencies range from 1.4k occurrences to 28 occurrences.
But 28 is still a lot!
*/
int main() {
    batch_iter_t iter = get_batch_iter(workload_e::YCSB_99_16);
    std::vector<txn_t> batch;
    while ((batch = iter.next_batch()).size() != 0) {
        printf("Batch size: %lu\n", batch.size());
        // analyze batch key dist to overall
        /*
        std::vector<std::pair<db_key_t, size_t>> very_hot_keys = get_key_cts(batch);
        for (size_t i = 0; i<very_hot_keys.size(); ++i) {
            printf("%lu,%lu\n", very_hot_keys[i].first, very_hot_keys[i].second);
        }
        */
    }
    return 0;
}
