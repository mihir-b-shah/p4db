
#pragma once

#include <comm/msg.hpp>

struct LocationInfo {
    bool is_local;
    msg::node_t target;
};
