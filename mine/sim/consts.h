
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

		40 nodes is max supported by the Barefoot switch p4db uses (they use 10G NICs).
		The switch has 80 10G ports. As per a non-blocking topology, we connect 40 ports to
		aggregation block above the ToR switch, and 40 below- thus, support 40 nodes.
		Cross-numa partitions multiply this by 2. The other latencies are mostly same (since network cost
		isn't the main dominating factor, it's starting up other txns as part of 2pc), or doing turn-around.

		We prob won't touch more than 2 hot keys per txn, so this is the max I'm willing to keep.

		N_Keys: 1.2MB per stage. We don't want to use more than necessary # of stages. Assuming we do not
		subdivide tuples (and selectively offload columns)- and maybe allow a little slack there should
		we want to, let's say 100 B. So we can store 12000 records per stage. There are 20 stages .
*/

static constexpr size_t TXN_SIZE = 2;
static constexpr size_t N_STEPS = 10000;
static constexpr size_t N_NODES = 80;
static constexpr size_t N_THREADS = 130;
static constexpr size_t N_KEYS = 10000;
static constexpr size_t TXNS_PER_STEP = 2;
static constexpr size_t COORD_DELAY = 180;
static constexpr size_t PARTIC_DELAY = 150;
static constexpr size_t ABORT_DELAY = 250;
static constexpr size_t MAX_QUEUE_SIZE = 10;
static constexpr bool WAIT_LOCK = false;

#endif
