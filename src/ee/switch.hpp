#pragma once

#include "ee/args.hpp"
#include "comm/comm.hpp"
#include "comm/eth_hdr.hpp"
#include "layout/declustered_layout.hpp"
#include "main/config.hpp"

#include <iostream>
#include <stdexcept>

struct SwitchInfo {
    size_t node_id;
    DeclusteredLayout* declustered_layout;

    struct read_info_t {
        db_key_t k;
        uint32_t reg_val;
    };

    struct SwResult {
        size_t n_results;
        std::array<read_info_t, N_OPS> results;
    };

	SwitchInfo(size_t node_id) : node_id(node_id) {
		declustered_layout = Config::instance().decl_layout;
	}
    
    void make_txn(const Txn& txn, void* pkt);
    SwResult parse_txn(void* out_pkt, void* in_pkt);
};
