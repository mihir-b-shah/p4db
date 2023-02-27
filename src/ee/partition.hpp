#pragma once


#include "main/config.hpp"
#include "ee/types.hpp"

#include <cstdint>
#include <tuple>
#include <utility>

struct LocationInfo {
    bool is_local;
    msg::node_t target;
    uint16_t abs_hot_index;
};

// hashed.
struct PartitionInfo {
    const uint64_t total_size;
    uint64_t partition_size;
    uint64_t offset;
    uint64_t hot_size;
    msg::node_t my_id;
    msg::node_t switch_id;

    PartitionInfo(const uint64_t total_size)
        : total_size(total_size) {
        auto& config = Config::instance();

        if (total_size % config.num_nodes != 0) {
            throw std::runtime_error("total_size % num_nodes != 0");
        }

        my_id = config.node_id;
        partition_size = total_size / config.num_nodes;
        offset = partition_size * config.node_id;
        switch_id = config.switch_id;

        std::stringstream ss;
        ss << "partinfo_total_size=" << total_size << '\n';
        ss << "partinfo_partition_size=" << partition_size << '\n';
        ss << "partinfo_offset=" << offset << '\n';
        ss << "partinfo_my_id=" << my_id << '\n';
        std::cout << ss.str();
    }

    auto location(db_key_t index) {
        LocationInfo loc_info;
        loc_info.target = msg::node_t{static_cast<uint32_t>(index / partition_size)};
        loc_info.is_local = loc_info.target == my_id;

        return loc_info;
    }

    db_key_t translate(db_key_t index) {
        // return db_key_t{index - offset};
        return index;
    }
};
