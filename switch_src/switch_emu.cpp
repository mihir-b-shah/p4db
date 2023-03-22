
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static constexpr size_t BUF_SIZE = 200;

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    assert(sockfd >= 0);

    int optval = 1;
    assert(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int)) == 0);

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons((unsigned short) 4004);

    assert(bind(sockfd, (struct sockaddr*) &server_addr, sizeof(server_addr)) >= 0);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    char buf[BUF_SIZE];
    int recv_ct = 0;
    while (1) {
        int nr = recvfrom(sockfd, buf, BUF_SIZE, 0, (struct sockaddr*) &client_addr, &client_len);
        assert(nr > 0);
        int ns = sendto(sockfd, buf, BUF_SIZE, 0, (struct sockaddr*) &client_addr, client_len);
        assert(ns > 0);
    }
}
