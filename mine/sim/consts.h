
#ifndef CONSTS_H
#define CONSTS_H

#include <cstddef>

/*	One step is latency of a CAS op (100 ns)

		Intra-rack RTT is around 3 us, network stack delays + other things = 2 us maybe?
		So one-sided op is 2.5 us. P4DB says txn engine/other non-locking components require
		~20 us (so in 2pc, after coordinator sends to cold node, that cold node has to kick off
		processing and go until it is sure it will commit). But for the cold nodes, they must
		wait for ssd write, etc. before the final commit is sent, which is ~10 us according to Assise,
		plus the regular network stack overheads. For simplicity, let us say the first one is ~18 us
		on first guess, second is ~15 us. This is conservative (the higher it goes the better for us)-
		stragglers, congestion, host processing times, etc. can all push these numbers higher.

		32 nodes is max supported by the Barefoot switch BF 6064X-T switch.
		The switch has 64 100GbE ports. As per a non-blocking topology, we connect 32 ports to
		aggregation block above the ToR switch, and 32 below- thus, support 32 nodes.
		Cross-numa partitions multiply this by 2. The other latencies are mostly same (since network cost
		isn't the main dominating factor, it's starting up other txns as part of 2pc), or doing turn-around.

		We prob won't touch more than 2 hot keys per txn, so this is the max I'm willing to keep.
		If I run w/ 1M keys, 2 hot keys, commit rate is 93%. 1 hot key, it is 99.7%. Hence, the contention
		problem is only worth solving for 2+ keys.

		N_Keys: 1.2MB per stage. We don't want to use more than necessary # of stages. Assuming we use 3
		stages, that is 3.6MB, which is 16 B/record is 225000 records.

		Let us reconstruct the setup. In lab (and in TACC) they are using dual-socket 28-core machines,
		so 56 cores in total, with 2-way hyperthreading. Let us assume a dual-port NIC is used for each,
		so effectively each 28-core machine gets a switch slot on ToR switch.

		We have 2-way hyperthreading. However, if we can time network latencies, we can have a cooperative
		multi-threading scheme as well per node, maybe get 3x threading that way.
*/

static constexpr size_t TXN_HOT_RECORDS = 1;
static constexpr size_t TXN_SIZE = 4;
static constexpr size_t N_STEPS = 50000;
static constexpr size_t N_NODES = 64;
static constexpr size_t N_THREADS = 56;
static constexpr size_t N_HOT_KEYS = 1000000;
static constexpr size_t COORD_DELAY = 100;
static constexpr size_t PARTIC_DELAY = 100;
static constexpr size_t ABORT_DELAY = 200;
static constexpr size_t MAX_QUEUE_SIZE = 100;
static constexpr bool WAIT_LOCK = false;

// assumes TXN_SIZE is much smaller than N_NODES.
static constexpr size_t TXNS_PER_STEP_BASE_ = (N_NODES * N_THREADS) / (3*COORD_DELAY + 2*(TXN_SIZE-1)*PARTIC_DELAY);
static constexpr size_t TXNS_PER_STEP = 5*TXNS_PER_STEP_BASE_/4;

#endif
