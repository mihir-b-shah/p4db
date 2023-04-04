
#pragma once

/*  RAW_PACKETS=false uses udp, RAW_PACKETS=true uses af_packet 
    There's only two options, so no need to use inheritance and make it nice. */

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

struct switch_intf_t {
    int sockfd;
    struct sockaddr_in addr;

    switch_intf_t();
    void prepare_msghdr(struct msghdr* mh, struct iovec* ivec);
};
