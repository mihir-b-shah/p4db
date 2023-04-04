
#include <comm/comm.hpp>
#include <ee/args.hpp>
#include <ee/defs.hpp>
#include <ee/database.hpp>
#include <ee/executor.hpp>
#include <layout/declustered_layout.hpp>
#include <main/config.hpp>

#include <array>
#include <utility>
#include <errno.h>

switch_intf_t::switch_intf_t() : sockfd(0) {
    memset(&addr, 0, sizeof(addr));

    auto& conf = Config::instance();
    auto& switch_server = conf.servers[conf.switch_id];

    if constexpr (RAW_PACKETS) {
    } else {
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        addr.sin_family = AF_INET;
        addr.sin_port = htons(switch_server.port);
        inet_aton((const char*) switch_server.ip.c_str(), &server_addr.sin_addr);
    }
    assert(sockfd >= 0);
}

void switch_intf_t::prepare_msghdr(struct msghdr* msg_hdr, struct iovec* ivec) {
    msg_hdr->msg_iov = ivec;
    msg_hdr->msg_iovlen = 1;
    msg_hdr->msg_control = NULL; // no ancilliary data
    msg_hdr->msg_controllen = 0;
    msg_hdr->msg_flags = 0;

    if constexpr (!RAW_PACKETS) {
        msg_hdr->msg_name = &addr;
        msg_hdr->msg_namelen = sizeof(addr);
    }
}
