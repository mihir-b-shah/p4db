#pragma once


#include "comm/comm.hpp"
#include "comm/switch_intf.hpp"
#include "comm/msg.hpp"
#include "comm/server.hpp"
#include "ee/defs.hpp"
#include "ee/args.hpp"
#include "layout/declustered_layout.hpp"

#include <vector>

class Database;

class Config : public HeapSingleton<Config> {
    friend class HeapSingleton<Config>;


protected:
    Config() = default;

public:
    void parse_cli(int argc, char** argv);

    Database* db;
    std::vector<Server> servers = {};
    Server sched_server;
    switch_intf_t sw_intf;

    msg::node_t node_id;
    uint32_t num_nodes;
    uint32_t num_txn_workers;
    msg::node_t switch_id;
    size_t tenant_id;

    uint64_t num_txns;
    bool use_switch;
    bool verify;
    std::string csv_file_cycles{"cycles.csv"};

	int write_prob;
	uint64_t table_size;

	std::string trace_fname;
	std::vector<Txn> trace_txns;
	
	std::string dist_fname;
	DeclusteredLayout* decl_layout;
};

void load_txns(Config& config);
