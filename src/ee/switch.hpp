#pragma once

#include "ee/args.hpp"
#include "comm/comm.hpp"
#include "layout/declustered_layout.hpp"
#include "main/config.hpp"
#include "utils/context.hpp"

#include <iostream>
#include <stdexcept>

struct StructTable;

struct SwitchInfo {
    size_t node_id;
    DeclusteredLayout* declustered_layout;
    // not initialized via constructor.
    StructTable* table;
    FILE* packets;

	SwitchInfo(size_t node_id) : node_id(node_id) {
		declustered_layout = Config::instance().decl_layout;
	/*
        char fname[50];
        sprintf(&fname[0], "/tmp/packets_%d", WorkerContext::get().tid);
        packets = fopen((const char*) fname, "wb");
	*/
	}
    
    void make_txn(const Txn& txn, void* pkt);
    void process_reply_txn(const Txn* txn, void* in_pkt, bool write);
};
