
#include "benchmarks/ycsb/switch.hpp"
#include "benchmarks/ycsb/random.hpp"
#include "db/defs.hpp"
#include "db/buffers.hpp"

#include <cstdint>

int main(int argc, char** argv) {
	auto& config = Config::instance();
	config.parse_cli(argc, argv);
	config.print();

	benchmark::ycsb::YCSBSwitchInfo p4_switch;
  benchmark::ycsb::YCSBRandom rnd(65536);
	
	uint8_t* buf = new uint8_t[10000];
	BufferWriter writer(buf);

	benchmark::ycsb::YCSBArgs::Multi<NUM_OPS> multi;
	multi.on_switch = true;
	multi.is_hot = true;

	for (size_t i = 0; i<NUM_OPS; ++i) {
		multi.ops[i].id = rnd.hot_id();
		multi.ops[i].mode = i % 2 == 0 ? AccessMode::WRITE : AccessMode::READ;
		multi.ops[i].value = rnd.value<uint32_t>(); // just write some value.
	}

	benchmark::ycsb::YCSBSwitchInfo::MultiOp arg(multi);
	p4_switch.make_txn(arg, writer);
	delete[] buf;
}

