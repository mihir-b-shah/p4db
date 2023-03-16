#pragma once

#include "ee/args.hpp"
#include "comm/comm.hpp"
#include "comm/eth_hdr.hpp"
#include "utils/buffers.hpp"
#include "layout/declustered_layout.hpp"
#include "main/config.hpp"

#include <iostream>
#include <stdexcept>

struct SwitchInfo {
    size_t node_id;
    DeclusteredLayout* declustered_layout;

    struct MultiOpOut {
        std::array<uint32_t, N_OPS> values;
    };

	SwitchInfo(size_t node_id) : node_id(node_id) {
		declustered_layout = Config::instance().decl_layout;
	}
    
    void make_txn(const Txn& txn, Communicator::Pkt_t* pkt);
    MultiOpOut parse_txn(BufferReader& br);
};
