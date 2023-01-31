
#include "stats/context.hpp"
#include "main/config.hpp"
#include "utils/ts_factory.hpp"

UniqueClockTimestampFactory::UniqueClockTimestampFactory() {
	auto& config = Config::instance();
	mask = (config.node_id << 8) | WorkerContext::get().tid;

	// std::stringstream ss;
	// ss << "start_ts=" << get() << " mask: " << mask << '\n';
	// std::cout << ss.str();
}
