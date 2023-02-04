
#include "main/config.hpp"

#include <cassert>
#include <fstream>
#include <string>
#include <sstream>
#include <cstring>

void load_txns(Config& config) {
	// read txns in from trace.	
	std::cout << "Fname: " << config.trace_fname << '\n';
    std::ifstream fin(config.trace_fname);
    // std::ifstream fin(trace_fname);
	assert(fin.is_open());
    std::string buf;

	size_t ctr = 0;
    while (1) {
        std::getline(fin, buf);
		if (fin.eof()) {
			break;
		}
		assert(fin);
        std::string access;
        std::istringstream ss(buf);
        config.trace_txns.emplace_back();
		Txn& txn = config.trace_txns.back();
		size_t i = 0;
        while (std::getline(ss, access, ',')) {
			if (txn.ops[NUM_OPS-1].mode != AccessMode::INVALID) {
				assert(false && "Txn is already full- error.");
			}
			Txn::OP op;	
			/*	We decide the mode based on rw percentage, from the config.
				The value is just a txn number, so we can do easy serializability
				checking */
			op.id = std::stoull(access);
			if ((rand() % 100) < config.write_prob) {
				op.mode = AccessMode::WRITE;
			} else {
				op.mode = AccessMode::READ;
			}
			op.value = static_cast<uint32_t>(1 + config.trace_txns.size());
			txn.ops[i++] = op;
        }
    }
}
