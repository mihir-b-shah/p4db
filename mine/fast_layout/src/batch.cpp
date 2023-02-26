
#include "sim.h"

#include <algorithm>
#include <map>
#include <iostream>
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

batch_iter_t::batch_iter_t(std::vector<txn_t> all_txns) {
    std::vector<std::pair<db_key_t, size_t>> key_cts_v = get_key_cts(all_txns);
	size_t n_keys = key_cts_v.size();
    key_cts_v.resize(static_cast<size_t>(key_cts_v.size() * FRAC_HOT));
    printf("n_keys: %lu, key_cts cutoff: %lu\n", n_keys, key_cts_v.back().second);

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

        txn_comps_.push_back({hot_txn, cold_txn});
    }
}

std::vector<txn_t> batch_iter_t::next_batch() {
    static constexpr size_t LOOKAHEAD = 40;

    std::vector<txn_t> ret;
    std::unordered_set<db_key_t> locks;

    for (auto it = txn_comps_.begin(); it != txn_comps_.end(); ++it) {
        const txn_t& hot_txn = it->first;
		const txn_t& cold_txn = it->second;
		size_t n_conflicts = 0;
		for (size_t i = 0; i<cold_txn.ops.size(); ++i) {
			n_conflicts += locks.find(cold_txn.ops[i]) != locks.end();
		}
		for (size_t i = 0; i<cold_txn.ops.size(); ++i) {
			locks.insert(cold_txn.ops[i]);
		}
		if (hot_txn.ops.size() > 0 && hot_txn.ops.size() < N_MAX_HOT_OPS) {
			ret.push_back(hot_txn);
		}
	}
	txn_comps_.clear();

	/*
    while (ret.size() < MAX_BATCH && txn_comps_.size() > 0) {
        size_t p = 0;
        size_t bef_considered_size = ret.size();
		size_t considered_size = ret.size();

        for (auto it = txn_comps_.begin(); it != txn_comps_.end(); ++it) {
            if (p++ >= LOOKAHEAD) {
                break;
            }

            const txn_t& hot_txn = it->first;
            const txn_t& cold_txn = it->second;
            
			size_t n_conflicts = 0;
            for (size_t i = 0; i<cold_txn.ops.size(); ++i) {
                n_conflicts += locks.find(cold_txn.ops[i]) != locks.end();
            }
			// printf("n_conflicts: %lu\n", n_conflicts);
            if (n_conflicts == 0) {
                for (size_t i = 0; i<cold_txn.ops.size(); ++i) {
                    locks.insert(cold_txn.ops[i]);
                }
                if (hot_txn.ops.size() > 0 && hot_txn.ops.size() < N_MAX_HOT_OPS) {
					ret.push_back(hot_txn);
                }
				considered_size += 1;
                txn_comps_.erase(it);
                break;
            }
        }

        
        if (bef_considered_size == considered_size) {
            // nothing happened, exit.
            break;
        }
    }
	
	if (ret.size() == 0 && txn_comps_.size() > 0) {
		// any more txns to run?
		return next_batch();
	}
	*/
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

static std::vector<txn_t> get_ycsb_txns(size_t key_ct, size_t zipf_const, size_t txn_size) {
	char fname_buf[100];
	std::sprintf(fname_buf, "../../ycsb/zipfian_k%luB_z%lu_n%lu.csv", key_ct, zipf_const, txn_size);
    printf("fname: %s\n", const_cast<const char*>(fname_buf));

    std::ifstream fin(const_cast<const char*>(fname_buf));
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
			db_key_t k = std::stoull(access);
			bool dupl = false;
			for (db_key_t prev : raw_txns.back().ops) {
				if (k == prev) {
					dupl = true;
				}
			}
			if (!dupl) {
				raw_txns.back().ops.push_back(k);
			}
        }
    }

	printf("raw_txns built.\n");
    return raw_txns;
}

template <typename RandF, size_t N>
void gen_unique_keys(const RandF& rand_key_func, txn_t& txn) {
    db_key_t touched[N];
    for (size_t i = 0; i<N; ++i) {
        while (1) {
            touched[i] = rand_key_func(i);
            bool unique = true;
            for (size_t j = 0; j<i; ++j) {
                unique &= touched[i] != touched[j];
            }
            if (unique) {
                break;
            }
        }
        txn.ops.push_back(touched[i]);
    }
}

static std::vector<txn_t> get_syn_unif_txns() {
    std::vector<txn_t> raw_txns;

    auto key_gen = [](size_t j){
		if (rand() % 2 == 0) {
			return rand() % 800;
		} else {
			return rand() % 1000000;
		}
    };
    for (size_t i = 0; i<100000; ++i) {
        txn_t txn;
        gen_unique_keys<decltype(key_gen), 12>(key_gen, txn);
        raw_txns.push_back(txn);
    }
    return raw_txns;
}

static std::vector<txn_t> get_syn_hot_8_txns() {
    std::vector<txn_t> raw_txns;

    auto key_gen = [](size_t j) {
        if (j < 6) {
            return rand() % 1000;
        } else {
            return rand() % 1000000000;
        }
    };
    for (size_t i = 0; i<100000; ++i) {
        txn_t txn;
        gen_unique_keys<decltype(key_gen), 12>(key_gen, txn);
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
    case workload_e::YCSB_80_8:
        txns = get_ycsb_txns(1, 80, 8);
        break;
    case workload_e::YCSB_99_8:
        txns = get_ycsb_txns(1, 99, 8);
        break;
    case workload_e::YCSB_99_16:
        txns = get_ycsb_txns(1, 99, 16);
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
