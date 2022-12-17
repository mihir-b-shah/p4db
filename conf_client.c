
/* adapted from https://www.cs.cmu.edu/afs/cs/academic/class/15213-f99/www/class26/udpclient.c */

#include "conf_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <pthread.h>
#include <limits.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

#define BUFSIZE 1024

#define hostname "candyland.cs.utexas.edu"
#define portno 37673
#define rdma_intf "ib0"

static void fetch_intf_ip(char* host)
{
	struct ifaddrs *ifaddr, *ifa;
	int family, s;
	socklen_t addr_len;

	if (getifaddrs(&ifaddr) == -1) {
    printf("error in getifaddrs\n");
    exit(0);
  }

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
			continue;  

		s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

		if ((strcmp(ifa->ifa_name, rdma_intf) == 0) && (ifa->ifa_addr->sa_family==AF_INET)) {
			if (s != 0) {
				printf("error returned: %s\n", gai_strerror(s));
        exit(0);
			}
			else {
				freeifaddrs(ifaddr);
        return;
			}
		}
	}

	freeifaddrs(ifaddr);
}

static void setup_conn(int* sockfd, struct sockaddr_in* server_addr)
{
  *sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (*sockfd < 0) {
    perror("ERROR opening socket");
    exit(0);
  }
  
  struct hostent* server = gethostbyname(hostname);
  if (server == NULL) {
    fprintf(stderr,"ERROR, no such host as %s\n", hostname);
    exit(0);
  }

  memset(server_addr, 0, sizeof(server_addr));
  server_addr->sin_family = AF_INET;
  memcpy(&(server_addr->sin_addr.s_addr), server->h_addr, server->h_length);
  server_addr->sin_port = htons(portno);
}

void init_conf(config_t* conf)
{
  conf->ips = NULL;
  conf->n = 0;
  conf->version = 0;
  // ignore calling destroy...
  pthread_mutex_init(&(conf->mutex), NULL);
}

void init_cmd(conf_cmd_t* cmd)
{
  cmd->cmd.type = 0;
  pthread_mutex_init(&(cmd->mutex), NULL);
}

static int check(int code)
{
  if (code < 0) {
    perror("error in socket operation.\n");
    exit(0);
  }
  return code;
}

static void* run_appl_client(void* arg)
{
  // ok, only used in one thread.
  static int init = 0;

  config_t* conf = (config_t*) arg;

  int sockfd;
  char buf[BUFSIZE];
  struct sockaddr_in server_addr;
  
  printf("Started application conf monitoring thread.\n");
  setup_conn(&sockfd, &server_addr);
  printf("Setup conn for appl_client.\n");
  
  while (1) {
    /* build the server's Internet address */
    buf[0] = 'A'; buf[1] = '\0';
    check(sendto(sockfd, buf, strlen(buf), 0, (struct sockaddr*) &server_addr, sizeof(struct sockaddr_in)));
    int n = check(recvfrom(sockfd, buf, BUFSIZE-1, 0, NULL, 0));

    // update conf
    if (!init) {
      __atomic_store_n(&(conf->version), 1, __ATOMIC_SEQ_CST);
      init = 1;
    }
    // __atomic_add_fetch(&(conf->version), 1, __ATOMIC_SEQ_CST);
    pthread_mutex_lock(&(conf->mutex));

    if (conf->ips != NULL) {
      free(conf->ips);
    }
    // so we can place a NIL-style element at end
    conf->ips = calloc(sizeof(struct in_addr), 1+n/sizeof(uint32_t));
    conf->n = n/sizeof(uint32_t);
    memcpy(conf->ips, buf, n);

    printf("Updated configuration with conf->n:%d\n", conf->n);

    // regardless of endianness, 255.255.255.255, which is an invalid host (a broadcast addr)
    conf->ips[conf->n].s_addr = 0xffffffffUL;
    pthread_mutex_unlock(&(conf->mutex));

    sleep(1);
  }

  return NULL;
}

void start_appl_client(config_t* conf)
{
  pthread_t thr;
  pthread_create(&thr, NULL, run_appl_client, (void*) conf);
}

static void* run_cache_client(void* arg)
{
  conf_cmd_t* ccmd = (conf_cmd_t*) arg;

  int sockfd;
  struct sockaddr_in server_addr;
  char buf[BUFSIZE];

  setup_conn(&sockfd, &server_addr);

  /* build the server's Internet address */
  while (1) {
    buf[0] = 'C';
    fetch_intf_ip(&buf[1]);

    check(sendto(sockfd, buf, strlen(buf), 0, (struct sockaddr*) &server_addr, sizeof(struct sockaddr_in)));
    sleep(1);
  }
  return NULL;
}

void start_cache_client(conf_cmd_t* ccmd)
{
  pthread_t thr;
  pthread_create(&thr, NULL, run_cache_client, (void*) ccmd);
}

