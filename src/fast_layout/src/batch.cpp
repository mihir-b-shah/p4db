
#include "sim.h"

#include <algorithm>
#include <map>
#include <fstream>
#include <cassert>
#include <cstdlib>
#include <sstream>
#include <string>

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

batch_iter_t::batch_iter_t(std::vector<txn_t> all_txns) : pos_(0) {
    std::vector<std::pair<db_key_t, size_t>> key_cts_v = get_key_cts(all_txns);
    key_cts_v.resize(static_cast<size_t>(key_cts_v.size() * FRAC_HOT));
    std::unordered_set<db_key_t> is_hot;
    for (const auto& pr : key_cts_v) {
        is_hot.insert(pr.first);
    }

    for (const txn_t& raw_txn : all_txns) {
        txn_t hot_txn;
        hot_txn.ops.reserve(raw_txn.ops.size());
        txn_t cold_txn;
        cold_txn.ops.reserve(raw_txn.ops.size());

        for (size_t i = 0; i<raw_txn.ops.size(); ++i) {
            db_key_t k = raw_txn.ops[i];
            if (is_hot.find(k) != is_hot.end()) {
                hot_txn.ops.push_back(k);
            } else {
                cold_txn.ops.push_back(k);
            }
        }

        hot_txns_comps_.push_back(hot_txn);
        cold_txns_comps_.push_back(cold_txn);
    }
}

std::vector<txn_t> batch_iter_t::next_batch() {
    std::vector<txn_t> ret;
    size_t start_pos = pos_;
    std::unordered_set<db_key_t> locks;

    assert(cold_txns_comps_.size() == hot_txns_comps_.size());
    while (pos_ - start_pos < MAX_BATCH && pos_ < cold_txns_comps_.size()) {
        const txn_t& hot_txn = hot_txns_comps_[pos_];
        const txn_t& cold_txn = cold_txns_comps_[pos_];

        for (size_t i = 0; i<cold_txn.ops.size(); ++i) {
            // no need to rollback acquire locks- if we stop, we're done anyway, 
            if (locks.find(cold_txn.ops[i]) == locks.end()) {
                locks.insert(cold_txn.ops[i]);
            } else {
                return ret;
            }
        }

        ret.push_back(hot_txn);
        pos_ += 1;
    }
    return ret;
}

static std::vector<txn_t> get_instacart_txns() {
    std::ifstream fin("../../instacart/orders.csv");
    std::string buf;
    size_t line = 0;
    std::vector<txn_t> raw_txns;
    long long last_order_id = -1;

    while (!fin.eof()) {
        std::getline(fin, buf);
        if (line++ == 0 || buf.size() == 0) {
            continue;
        }
        size_t split = buf.find(',');
        assert(split != std::string::npos);
        long long order_id = std::stoll(buf.substr(0, split));
        if (last_order_id != order_id) {
            raw_txns.emplace_back();
            last_order_id = order_id;
        }

        db_key_t k = std::stoull(buf.substr(split+1, buf.size()-split-1));
        raw_txns.back().ops.push_back(k);
    }
    return raw_txns;
}

static std::vector<txn_t> get_ycsb_txns() {
    std::ifstream fin("../../ycsb/zipfian_1B.csv");
    std::string buf;
    std::vector<txn_t> raw_txns;

    while (!fin.eof()) {
        std::getline(fin, buf);
        if (buf.size() == 0) {
            continue;
        }
        std::string access;
        std::istringstream ss(buf);
        raw_txns.emplace_back();
        while (std::getline(ss, access, ',')) {
            raw_txns.back().ops.push_back(std::stoull(access));
        }
    }
    return raw_txns;
}

static std::vector<txn_t> get_syn_unif_txns() {
    std::vector<txn_t> raw_txns;

    for (size_t i = 0; i<100000; ++i) {
        txn_t txn;
        for (size_t j = 0; j<8; ++j) {
            txn.ops.push_back(rand() % 100000);
        }
        raw_txns.push_back(txn);
    }
    return raw_txns;
}

static std::vector<txn_t> get_syn_hot_8_txns() {
    std::vector<txn_t> raw_txns;

    for (size_t i = 0; i<100000; ++i) {
        txn_t txn;
        for (size_t j = 0; j<8; ++j) {
            txn.ops.push_back(rand() % 100);
        }
        txn.ops.push_back(rand() % 1000000000);
        raw_txns.push_back(txn);
    }
    return raw_txns;
}

static std::vector<txn_t> get_syn_adversarial_txns() {
    std::vector<txn_t> raw_txns;

    for (size_t i = 0; i<100000; ++i) {
        txn_t txn;
        // ensure double pass
        txn.ops.push_back(0);
        txn.ops.push_back(0);

        for (size_t j = 0; j<6; ++j) {
            txn.ops.push_back(rand() % 1000000000);
        }
        raw_txns.push_back(txn);
    }
    return raw_txns;
}

batch_iter_t get_batch_iter(workload_e wtype) {
    std::vector<txn_t> txns;
    switch (wtype) {
    case workload_e::INSTACART:
        txns = get_instacart_txns();
        break;
    case workload_e::YCSB:
        txns = get_ycsb_txns();
        break;
    case workload_e::SYN_UNIF:
        txns = get_syn_unif_txns();
        break;
    case workload_e::SYN_HOT_8:
        txns = get_syn_hot_8_txns();
        break;
    case workload_e::SYN_ADVERSARIAL:
        txns = get_syn_adversarial_txns();
        break;
    }
    return batch_iter_t(txns);
}
