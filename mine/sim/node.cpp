
#include "consts.h"
#include "txn.h"
#include "node.h"
#include <cassert>

size_t get_coord(const txn_t& txn) {
	unsigned ctrs[N_NODES] = {0};
	for (size_t i = 0; i<TXN_SIZE; ++i) {
		ctrs[node_for_key(txn.ops[i])] += 1;
	}
	size_t best = 0;
	for (size_t i = 1; i<N_NODES; ++i) {
		if (ctrs[i] > ctrs[best]) {
			best = i;
		}
	}
	return best;
}

txn_wrap_t::txn_wrap_t(txn_t t) : t(t), node_mask(0ULL) {
	coord = get_coord(t);
	for (size_t i = 0; i<TXN_SIZE; ++i) {
		node_mask |= 1ULL << node_for_key(t.ops[i]);
	}
}

void nthread_step(size_t s, nthread_t& nthr, std::vector<node_t>& nodes) {
	switch (nthr.state) {
		// idle is stepped outside, by providing a txn.
		case STG_IDLE:
			return;
		case STG_COORD_ACQ: 
		case STG_PARTIC_ACQ: {
			while (nthr.lock_acq_prog < TXN_SIZE && 
						 node_for_key(nthr.work.t.ops[nthr.lock_acq_prog]) != nthr.node->id) {	
				nthr.lock_acq_prog += 1;
			}
			if (nthr.lock_acq_prog == TXN_SIZE) {
				// change state
				printf("TXN %lu at step %lu finished lock acquisition on %s.\n", nthr.work.t.tid, s, nthr.state == STG_COORD_ACQ ? "coord" : "peer");
				nthr.wait_time = NETWORK_DELAY;
				switch (nthr.state) {
					case STG_COORD_ACQ:
						nthr.state = STG_PREPARE;
						break;
					case STG_PARTIC_ACQ:
						nthr.state = STG_READY;
						break;
					default:
						break;
				}
			} else {
				// acquire next lock
				db_key_t k = nthr.work.t.ops[nthr.lock_acq_prog];
				if (nthr.node->locks.find(k) == nthr.node->locks.end()) {
					printf("TXN %lu at step %lu acquired lock for %lu on %s %lu, p=%lu.\n", nthr.work.t.tid, s, nthr.work.t.ops[nthr.lock_acq_prog], nthr.state == STG_COORD_ACQ ? "coord" : "peer", nthr.node->id, nthr.lock_acq_prog);

					nthr.work.thrs[nthr.node->id] = nthr.id;
					nthr.node->locks.insert(k);
					nthr.lock_acq_prog += 1;
				} else {
					printf("TXN %lu at step %lu contended for lock for %lu on %s %lu, p=%lu.\n", nthr.work.t.tid, s, nthr.work.t.ops[nthr.lock_acq_prog], nthr.state == STG_COORD_ACQ ? "coord" : "peer", nthr.node->id, nthr.lock_acq_prog);
				}
			}
			break;
		}
		case STG_PREPARE: {
			if (nthr.wait_time > 0 && nthr.ready_ct == 0) {
				nthr.wait_time -= 1;
			} else if (nthr.ready_ct == 0) {
				printf("TXN %lu at step %lu sent PREPARE to %d peers, waiting.\n", nthr.work.t.tid, s, __builtin_popcountll(nthr.work.node_mask)-1);
				// get a thread responding to my msg, asap.
				size_t mask = nthr.work.node_mask;
				for (size_t i = 0; mask>0; ++i, mask >>= 1) {
					if ((mask & 1) && i != nthr.work.coord) {
						printf("TXN %lu at step %lu pushed to queue %lu.\n", nthr.work.t.tid, s, i);
						nodes[i].tq.push(nthr.work);
					}
				}
				nthr.ready_ct += 1;
			} else if (nthr.ready_ct == (unsigned) __builtin_popcountll(nthr.work.node_mask)) {
				assert(nthr.wait_time == 0);
				nthr.wait_time = NETWORK_DELAY;
				nthr.ready_ct += 1; // signal that phase is done
			} else if (nthr.wait_time > 0 && nthr.ready_ct == 1+ ((unsigned) __builtin_popcountll(nthr.work.node_mask))) {
				nthr.wait_time -= 1;
			} else if (nthr.wait_time == 0 && nthr.ready_ct == 1+ ((unsigned) __builtin_popcountll(nthr.work.node_mask))) {
				// done- send the commit!
				printf("TXN %lu at step %lu sent COMMIT to peers.\n", nthr.work.t.tid, s);
				size_t mask = nthr.work.node_mask;
				for (size_t i = 0; mask>0; ++i, mask >>= 1) {
					if ((mask & 1) && i != nthr.work.coord) {
						printf("TXN %lu at step %lu sent commit to peer %lu at thread %lu.\n", nthr.work.t.tid, s, i, nthr.work.thrs[i]);
						nodes[i].thrs[nthr.work.thrs[i]].commit = true;
					}
				}
				nthr.reset();
				for (size_t i = 0; i<TXN_SIZE; ++i) {
					db_key_t k = nthr.work.t.ops[i];
					if (node_for_key(k) == nthr.node->id) {
						nthr.node->locks.erase(k);
					}
				}

			} else {
				// keep waiting for acks.
			}
			break;
		}
		case STG_READY: {
			if (nthr.wait_time > 0) {
				nthr.wait_time -= 1;
			} else {
				printf("TXN %lu at step %lu sent READY to coord at node %lu, thread %lu. I am node %lu, thread %lu\n", nthr.work.t.tid, s, nthr.work.coord, nthr.work.thrs[nthr.work.coord], nthr.node->id, nthr.id);
				// respond back to my coordinator.
				nodes[nthr.work.coord].thrs[nthr.work.thrs[nthr.work.coord]].ready_ct += 1;
				nthr.state = STG_COMMIT;
			}
			break;
		}
		case STG_COMMIT: {
			if (nthr.commit) {
				printf("TXN %lu at step %lu received COMMIT at peer.\n", nthr.work.t.tid, s);
				// done!
				nthr.reset();
				for (size_t i = 0; i<TXN_SIZE; ++i) {
					db_key_t k = nthr.work.t.ops[i];
					if (node_for_key(k) == nthr.node->id) {
						nthr.node->locks.erase(k);
					}
				}
			}
			break;
		}
	}

}

