
#pragma once

/*  RAW_PACKETS=false uses udp, RAW_PACKETS=true uses af_packet 
    There's only two options, so no need to use inheritance and make it nice. */

#include <arpa/inet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/if_packet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>

static constexpr uint16_t P4DB_ETHER_TYPE = 0x88b5;
static constexpr size_t MAC_ADDR_BYTES = 6;

struct switch_intf_t {
    int sockfd;
    union {
        struct sockaddr_in ip_addr;
        struct sockaddr_ll mac_addr;
    } addr;

    switch_intf_t();
    void setup();
    void prepare_msghdr(struct msghdr* mh, struct iovec* ivec);
};
