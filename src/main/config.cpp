#include "config.hpp"

#include <cxxopts.hpp>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <unistd.h>
#include <netdb.h> 
#include <arpa/inet.h>
#include <errno.h>
#include <fstream>
#include <cassert>
#include <cstdlib>
#include <sstream>
#include <string>

class BetterParseResult : public cxxopts::ParseResult {
public:
    BetterParseResult() = delete;
    BetterParseResult(cxxopts::ParseResult&& result)
        : cxxopts::ParseResult(std::move(result)) {}

    // adds check with meaningfull error message for required options which is
    // missing in library

    template <typename T>
    const T& as(const std::string& option) const {
        if (!count(option)) {
            std::cerr << "Error: Option \"" << option << "\" is required\n";
            std::exit(EXIT_FAILURE);
        }

        const auto& value = (*this)[option];
        return value.as<T>();
    }
};

void Config::parse_cli(int argc, char** argv) {
    cxxopts::Options options("P4DB", "Database for P4 Burning Switch Project");

    // clang-format off
    options.add_options()
        ("node_id", "Server identifier, 0 indexed < num_servers", cxxopts::value<uint32_t>())
        ("tenant_id", "Tenant identifier, 0 indexed", cxxopts::value<size_t>())
        ("num_nodes", "Number of servers to use", cxxopts::value<uint32_t>())
        ("num_txn_workers", "", cxxopts::value<uint32_t>())
        ("csv_file_cycles", "", cxxopts::value<std::string>())
        ("csv_file_periodic", "", cxxopts::value<std::string>())

        ("use_switch", "Whether to use switch for txn processing", cxxopts::value<bool>())
        ("verify", "Run verification, like table consistency checks for TPC-C ", cxxopts::value<bool>()->default_value("false"))
        ("num_txns", "", cxxopts::value<uint64_t>())
        ("write_prob", "", cxxopts::value<int>())
        ("table_size", "", cxxopts::value<uint64_t>())
		("trace_fname", "", cxxopts::value<std::string>())
		("dist_fname", "", cxxopts::value<std::string>())
        ("h,help", "Print usage")
    ;

    BetterParseResult result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << '\n';
        std::exit(0);
    }

    node_id = result.as<uint32_t>("node_id");
    tenant_id = result.as<size_t>("tenant_id");
    num_nodes = result.as<uint32_t>("num_nodes");
    num_txn_workers = result.as<uint32_t>("num_txn_workers");

	if constexpr (DYNAMIC_IPS) {
		int coord_sockfd = socket(AF_INET, SOCK_STREAM, 0);
		struct hostent* coord = gethostbyname("candyland.cs.utexas.edu");
		struct sockaddr_in coord_addr; 
		memset(&coord_addr, 0, sizeof(coord_addr));
		coord_addr.sin_family = AF_INET;
		memcpy(&(coord_addr.sin_addr.s_addr), coord->h_addr, coord->h_length);
		coord_addr.sin_port = htons(5001);
		char coord_buf[201] = {};

		// does zero-length packet screw up tcp/congestion control?
		connect(coord_sockfd, (struct sockaddr*) &coord_addr, (socklen_t) sizeof(struct sockaddr_in));
		send(coord_sockfd, coord_buf, 1, 0);
		recvfrom(coord_sockfd, coord_buf, 200, 0, NULL, 0);

		char* ip_token = strtok(coord_buf, " ");
		while (ip_token != NULL) {
			// note, we need the mac address of the SWITCH, to send stuff.
			// we dont need it for the servers.
			servers.emplace_back(ip_token, 4001, (eth_addr_t) {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
			ip_token = strtok(NULL, " ");
		}
	} else {
		sched_server = Server("128.83.144.8", 4001, (eth_addr_t) {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
		servers.emplace_back("192.168.0.8", 4002, (eth_addr_t) {0xE8, 0xEB, 0xD3, 0xF7, 0x6C, 0x26});
		servers.emplace_back("192.168.0.10", 4003, (eth_addr_t) {0xE8, 0xEB, 0xD3, 0xF7, 0x79, 0x5E});
	}

    if (servers.size() < num_nodes) {
        throw std::runtime_error("Insufficient servers specified");
    }
    servers.resize(num_nodes);

	trace_fname = result.as<std::string>("trace_fname");
	dist_fname = result.as<std::string>("dist_fname");

    use_switch = result.as<bool>("use_switch");
    if (result.count("verify")) {
        verify = result.as<bool>("verify");
    }
    switch_id = servers.size();
    servers.push_back(servers[node_id]);

    num_txns = result.as<uint64_t>("num_txns");
	table_size = result.as<uint64_t>("table_size");
	write_prob = result.as<int>("write_prob");
}
