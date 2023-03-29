
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>

#include <arpa/inet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/if_packet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/uio.h>

//	IEEE 802 marks this as an ether_type reserved for experimental/private use.
static constexpr uint16_t P4DB_ETHER_TYPE = 0x88b5;
static constexpr size_t MAC_ADDR_SIZE = 6;

struct __attribute__((packed)) packet_t {
	uint8_t dst_addr[MAC_ADDR_SIZE];
	uint8_t src_addr[MAC_ADDR_SIZE];
	uint16_t ether_type;
	char body[40];
};

// whatever interface is connected to p4 switch
static int get_iface_id(int sock, const char* intf_name) {
	struct ifreq ifr;
	size_t ifr_name_len = strlen(intf_name);
	// its not going to overrun...
	strcpy(ifr.ifr_name, intf_name);
	assert(ioctl(sock, SIOCGIFINDEX, &ifr) == 0);
	return ifr.ifr_ifindex;
}

static void set_rx_promisc(int iface_id, int sock) {
	struct packet_mreq mreq;
	memset(&mreq, 0, sizeof(packet_mreq));
	mreq.mr_ifindex = iface_id;
	mreq.mr_type = PACKET_MR_PROMISC;
	assert(setsockopt(sock, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == 0);
}

static void setup_sockaddr_ll(int iface_id, struct sockaddr_ll* switch_addr) {
	uint8_t addr[MAC_ADDR_SIZE] = {};
	memset(switch_addr, 0, sizeof(*switch_addr));
	switch_addr->sll_family = AF_PACKET;
	switch_addr->sll_protocol = htons(P4DB_ETHER_TYPE);
	switch_addr->sll_ifindex = iface_id;
	switch_addr->sll_halen = MAC_ADDR_SIZE;
	memcpy(&switch_addr->sll_addr, &addr[0], MAC_ADDR_SIZE);
}

int main() {
	int sock = socket(AF_PACKET, SOCK_RAW, htons(P4DB_ETHER_TYPE));
	assert(sock >= 0);

	int iface_id = get_iface_id(sock, "lo");
	struct sockaddr_ll switch_addr;
	setup_sockaddr_ll(iface_id, &switch_addr);
	set_rx_promisc(iface_id, sock);
	assert(bind(sock, (struct sockaddr*) &switch_addr, sizeof(sockaddr_ll)) == 0);

	packet_t pkt;
	memset(&pkt, 0, sizeof(pkt));
	pkt.ether_type = htons(P4DB_ETHER_TYPE);

	const char* msg = "Rats are nice pets!\n";
	strcpy(&pkt.body[0], msg);

	struct iovec iov;
	iov.iov_base = &pkt;
	iov.iov_len = sizeof(packet_t);

    struct msghdr msg_hdr;
    msg_hdr.msg_name = NULL;
    msg_hdr.msg_namelen = 0;
    msg_hdr.msg_iov = &iov;
    msg_hdr.msg_iovlen = 1;
    msg_hdr.msg_control = NULL; // no ancilliary data
    msg_hdr.msg_controllen = 0;
    msg_hdr.msg_flags = 0;

	printf("Sent: %ld\n", sendmsg(sock, &msg_hdr, 0));

	packet_t pkt_recv;
	iov.iov_base = &pkt_recv;
	printf("Received: %ld\n", recvmsg(sock, &msg_hdr, 0));

	close(sock);
	return 0;
}
