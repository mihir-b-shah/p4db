
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include "bf_switch/bf_switch_types.h"
#include "s3/switch_packet.h"
#include "../switch_utils.h"
#include "test_packet.h"

// TODO: I don't know what to do with the handlers.
static void pkt_cb(char* pkt, int pkt_size, uint64_t port_lag_handle, uint64_t hostif_trap_handle) {
	/*	parse the packet- throw away eth, ipv4, dpdk-added headers.
		types of packets: new_alloc request, lock info.
		new_alloc can be responded right away.
		lock_info needs to hear from ALL nodes before replying to all.
		switch_utils.h defines routines to alloc,free, and switch_pktdriver_pkt_tx() send a packet. */

	
}

static void exit_handler() {
	switch_status_t status = stop_bf_switch_api_packet_driver();
	assert(status == SWITCH_STATUS_SUCCESS);
	printf("*** stop pktdrv ***\n");

	status = switch_packet_clean();
	assert(status == SWITCH_STATUS_SUCCESS);
	printf("*** pkt clean ***\n");
}

/* main function*/
int main(void) {
	atexit(exit_handler);
	switch_status_t status = SWITCH_STATUS_SUCCESS;

	const char* cpu_port = NULL;
	status = switch_packet_init(cpu_port, true);
	assert(status == SWITCH_STATUS_SUCCESS);
	printf("*** packet init ***\n");

	status = start_bf_switch_api_packet_driver();
	assert(status == SWITCH_STATUS_SUCCESS);
	printf("*** packet driver start ***\n");

	switch_register_callback_rx(pkt_cb);
	return 0;
}
