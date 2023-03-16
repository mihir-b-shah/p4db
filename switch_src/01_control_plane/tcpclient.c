/* 
 * tcpclient.c - A simple TCP client
 * usage: tcpclient <host> <port>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 

#define BUFSIZE 1024

/* 
 * error - wrapper for perror
 */
void error(char *msg) {
    perror(msg);
    exit(0);
}

struct __attribute__((packed)) alloc_req_t {
    uint64_t start_delay_ns;
    uint64_t duration_ns;
    uint32_t blk_to_free;
    uint32_t tenant_id;
    uint32_t tenant_num_nodes;
    uint32_t batch_num; // start at 1
};

struct __attribute__((packed)) alloc_resp_t {
    uint32_t batch_num;
    uint32_t alloced_blk_id;
};

struct __attribute__((packed)) alloc_ready_msg_t {
    uint32_t dummy;
};


int main(int argc, char **argv) {
    int sockfd, portno, n;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    char *hostname;
    char buf[BUFSIZE];

    /* check command line arguments */
    if (argc != 4) {
       fprintf(stderr,"usage: %s <hostname> <port>\n", argv[0]);
       exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    int tid = atoi(argv[3]);

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* gethostbyname: get the server's DNS entry */
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
	  (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(portno);

    /* connect: create a connection with the server */
    if (connect(sockfd, &serveraddr, sizeof(serveraddr)) < 0) 
      error("ERROR connecting");

    struct alloc_req_t* req = (struct alloc_req_t*) buf;
    req->start_delay_ns = 1000000ULL;
    req->duration_ns = 1000000ULL;
    req->blk_to_free = 0xffffffff;
    req->tenant_id = tid;
    req->tenant_num_nodes = tid == 1 ? 3 : 1;
    req->batch_num = 1;

    // send the message line to the server
    n = write(sockfd, buf, sizeof(struct alloc_req_t));
    if (n < 0) 
      error("ERROR writing to socket");

    n = read(sockfd, buf, sizeof(struct alloc_resp_t));
    if (n < 0) 
      error("ERROR reading from socket");
    
    struct alloc_resp_t* resp = (struct alloc_resp_t*) buf;
    printf("bn: %u, alloced_blk: %u\n", resp->batch_num, resp->alloced_blk_id);

    n = read(sockfd, buf, sizeof(struct alloc_ready_msg_t));
    if (n < 0) 
      error("ERROR reading from socket");
    
    struct alloc_ready_msg_t* ready_msg = (struct alloc_ready_msg_t*) buf;
    printf("received ready.\n");

    while (1) {}
    return 0;
}
