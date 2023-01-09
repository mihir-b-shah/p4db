
#include "sim.h"

#include <algorithm>
#include <map>
#include <fstream>
#include <cassert>
#include <cstdlib>
#include <sstream>
#include <string>

std::vector<txn_t> batch_iter_t::next_batch() {
    if (all_txns_.size() - pos_ < MAX_BATCH) {
        return {};
    } else {
        auto ret = std::vector<txn_t>(all_txns_.begin() + pos_, all_txns_.begin() + pos_ + MAX_BATCH);
        pos_ += MAX_BATCH;
        return ret;
    }
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

static std::vector<txn_t> get_hot_txn_comps(const std::vector<txn_t>& raw_txns) {
    std::vector<std::pair<db_key_t, size_t>> key_cts = get_key_cts(raw_txns);
    key_cts.resize(static_cast<size_t>(key_cts.size() * FRAC_HOT));
    std::unordered_map<db_key_t, size_t> hot_keys(key_cts.begin(), key_cts.end());

    std::vector<txn_t> txns;
    for (const txn_t& raw_txn : raw_txns) {
        txn_t txn;
        txn.ops.resize(raw_txn.ops.size());
        auto it = std::copy_if(raw_txn.ops.begin(), raw_txn.ops.end(), txn.ops.begin(),
            [&hot_keys](db_key_t k){ return hot_keys.find(k) != hot_keys.end(); });
        txn.ops.resize(it - txn.ops.begin());
        if (txn.ops.size() > 0) {
            txns.push_back(txn);
        }
    }
    return txns;
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
    return batch_iter_t(get_hot_txn_comps(txns));
}
