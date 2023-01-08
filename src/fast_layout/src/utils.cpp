
#include "utils.h"

#include <algorithm>
#include <map>

std::vector<std::pair<db_key_t, size_t>> get_key_cts(const std::vector<txn_t>& txns) {
    std::map<db_key_t, size_t> cts;
    for (const txn_t& txn : txns) {
        for (db_key_t op : txn.ops) {
            cts[op] += 1;
        }
    }
    std::vector<std::pair<db_key_t, size_t>> vec(cts.begin(), cts.end());
    std::sort(vec.begin(), vec.end(), [](const auto& p1, const auto& p2){
        return p2.second < p1.second;
    });
    return vec;
}
