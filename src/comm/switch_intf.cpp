
#include <comm/comm.hpp>
#include <comm/switch_intf.hpp>
#include <ee/args.hpp>
#include <ee/defs.hpp>
#include <ee/database.hpp>
#include <ee/executor.hpp>
#include <layout/declustered_layout.hpp>
#include <main/config.hpp>

#include <array>
#include <utility>
#include <errno.h>

#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>

/*  The switch sockfd right now is just to talk to the simulated switch. In the real setup,
    I want to send to anyone connected by the switch (the switch will then send a reply...)

    TODO not using MSG_ZEROCOPY here- we have very small packets, and Linux documentation
    says the page mgmt overhead for zero-copy is only worth it for sends bigger than 10 kB
    Unless I'm misunderstanding- maybe revisit this? */

//	IEEE 802 marks this as an ether_type reserved for experimental/private use.
static const char* intf_name = "XXX";

// whatever interface is connected to p4 switch
static int get_iface_id(int sock, const char* intf_name) {
	struct ifreq ifr;
	// its not going to overrun...
	strcpy(ifr.ifr_name, intf_name);
    int rc = ioctl(sock, SIOCGIFINDEX, &ifr);
    assert(rc == 0);
	return ifr.ifr_ifindex;
}

static void set_rx_promisc(int iface_id, int sock) {
	struct packet_mreq mreq;
	memset(&mreq, 0, sizeof(packet_mreq));
	mreq.mr_ifindex = iface_id;
	mreq.mr_type = PACKET_MR_PROMISC;
	int rc = setsockopt(sock, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    assert(rc == 0);
}

static void setup_sockaddr_ll(int iface_id, struct sockaddr_ll* switch_addr) {
	uint8_t addr[MAC_ADDR_BYTES] = {};
	memset(switch_addr, 0, sizeof(*switch_addr));
	switch_addr->sll_family = AF_PACKET;
	switch_addr->sll_protocol = htons(P4DB_ETHER_TYPE);
	switch_addr->sll_ifindex = iface_id;
	switch_addr->sll_halen = MAC_ADDR_BYTES;
	memcpy(&switch_addr->sll_addr, &addr[0], MAC_ADDR_BYTES);
}

switch_intf_t::switch_intf_t() : sockfd(0) {
    memset(&addr, 0, sizeof(addr));
}

void switch_intf_t::setup() {
    auto& conf = Config::instance();
    auto& switch_server = conf.servers[conf.switch_id];

    if constexpr (RAW_PACKETS) {
        if (geteuid() != 0) {
            assert(false && "Run with root privileges.\n");
        }

	    sockfd = socket(AF_PACKET, SOCK_RAW, htons(P4DB_ETHER_TYPE));
        assert(sockfd >= 0);

        int iface_id = get_iface_id(sockfd, intf_name);
        setup_sockaddr_ll(iface_id, &addr.mac_addr);
        set_rx_promisc(iface_id, sockfd);
        int rc = bind(sockfd, (struct sockaddr*) &addr.mac_addr, sizeof(sockaddr_ll));
        assert(rc == 0);
    } else {
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        assert(sockfd >= 0);

        addr.ip_addr.sin_family = AF_INET;
        addr.ip_addr.sin_port = htons(switch_server.port);
        inet_aton((const char*) switch_server.ip.c_str(), &addr.ip_addr.sin_addr);
    }
}

void switch_intf_t::prepare_msghdr(struct msghdr* msg_hdr, struct iovec* ivec) {
    msg_hdr->msg_iov = ivec;
    msg_hdr->msg_iovlen = 1;
    msg_hdr->msg_control = NULL; // no ancilliary data
    msg_hdr->msg_controllen = 0;
    msg_hdr->msg_flags = 0;

    if constexpr (!RAW_PACKETS) {
        msg_hdr->msg_name = &addr.ip_addr;
        msg_hdr->msg_namelen = sizeof(addr.ip_addr);
    }
}
