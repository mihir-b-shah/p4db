
#ifndef _UTILS_H_
#define _UTILS_H_

#include "sim.h"

#include <vector>
#include <utility>

std::vector<std::pair<db_key_t, size_t>> get_key_cts(const std::vector<txn_t>& txns);

#endif
