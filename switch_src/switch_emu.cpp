
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <signal.h>

#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static constexpr size_t BUF_SIZE = 200;

static FILE* packet_log_f = NULL;
static size_t n_received = 0;

static void sig_int_handler(int sig) {
    assert(sig == SIGINT);
    assert(packet_log_f != NULL);
    fprintf(stderr, "Received %lu packets, closing.\n", n_received);
    fclose(packet_log_f);
    fprintf(stderr, "Close finished.\n");
    exit(0);
}

int main() {
    signal(SIGINT, sig_int_handler);

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
    
    packet_log_f = fopen("/tmp/packet_trace.raw", "w");

    char buf[BUF_SIZE];
    int prev_msg_size = -1;

    while (1) {
        int nr = recvfrom(sockfd, buf, BUF_SIZE, 0, (struct sockaddr*) &client_addr, &client_len);
        n_received += 1;
        if (prev_msg_size == -1) {
            printf("msg_size: %d\n", nr);
        }
        assert(nr > 0 && (prev_msg_size == -1 || nr == prev_msg_size));
        prev_msg_size = nr;
        assert(fwrite(buf, 1, (size_t) nr, packet_log_f) == (size_t) nr);

        int ns = sendto(sockfd, buf, nr, 0, (struct sockaddr*) &client_addr, client_len);
        assert(ns > 0);
    }
}
