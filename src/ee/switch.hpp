#pragma once

#include "ee/args.hpp"
#include "comm/eth_hdr.hpp"
#include "utils/buffers.hpp"
#include "layout/declustered_layout.hpp"
#include "main/config.hpp"

#include <iostream>
#include <stdexcept>

struct SwitchInfo {
    DeclusteredLayout* declustered_layout;

    struct MultiOpOut {
        std::array<uint32_t, N_OPS> values;
    };

	SwitchInfo() {
		declustered_layout = Config::instance().decl_layout;
	}

    size_t make_txn(const Txn& txn, uint8_t* buf);
    MultiOpOut parse_txn(BufferReader& br);
};
