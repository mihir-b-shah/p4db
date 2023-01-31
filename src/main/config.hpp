#pragma once


#include "comm/comm.hpp"
#include "comm/msg.hpp"
#include "comm/server.hpp"
#include "ee/defs.hpp"
#include "utils/util.hpp"

#include <vector>


class Config : public HeapSingleton<Config> {
    friend class HeapSingleton<Config>;


protected:
    Config() = default;

public:
    void parse_cli(int argc, char** argv);

    std::vector<Server> servers = {};

    msg::node_t node_id;
    uint32_t num_nodes;
    uint32_t num_txn_workers;
    msg::node_t switch_id;
    uint64_t switch_entries;

    uint64_t num_txns;
    bool use_switch;
    bool verify;
    std::string csv_file_cycles{"cycles.csv"};

	int write_prob;
	uint64_t table_size;
	uint64_t hot_size;

	std::string trace_fname;
};
