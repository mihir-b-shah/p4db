
#include "consts.h"
#include "txn.h"
#include "node.h"
#include <cassert>

size_t get_coord(const txn_t& txn) {
	unsigned ctrs[N_NODES] = {};
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

txn_wrap_t::txn_wrap_t(txn_t t) : t(t) {
	coord = get_coord(t);
	for (size_t i = 0; i<TXN_SIZE; ++i) {
		node_mask.set(node_for_key(t.ops[i]));
	}
}

void nthread_step(size_t s, nthread_t& nthr, system_t& sys) {
	if (sys.aborted.find(nthr.work.t.tid) != sys.aborted.end()) {
		nthr.reset();
		return;
	}

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
				nthr.wait_time = nthr.work.node_mask.count() > 1 ? COORD_DELAY : 0;
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
					printf("TXN %lu at step %lu acquired lock for %lu on %s %lu.\n", nthr.work.t.tid, s, nthr.work.t.ops[nthr.lock_acq_prog], nthr.state == STG_COORD_ACQ ? "coord" : "peer", nthr.node->id);

					nthr.work.thrs[nthr.node->id] = nthr.id;
					nthr.node->locks[k] = nthr.work.t.tid;
					nthr.lock_acq_prog += 1;
				} else {
					if (WAIT_LOCK && nthr.node->locks[k] > nthr.work.t.tid) {
						// WAIT_DIE CC protocol, where the holder is younger than me.
						printf("TXN %lu at step %lu contended for lock for %lu on %s %lu.\n", nthr.work.t.tid, s, nthr.work.t.ops[nthr.lock_acq_prog], nthr.state == STG_COORD_ACQ ? "coord" : "peer", nthr.node->id);
					} else {
						printf("TXN %lu at step %lu aborted trying for lock for %lu on %s %lu.\n", nthr.work.t.tid, s, nthr.work.t.ops[nthr.lock_acq_prog], nthr.state == STG_COORD_ACQ ? "coord" : "peer", nthr.node->id);
						/*
						Abort.
						1) Reset all participating threads' state.
							(a) this is hard, since I don't know the threads due to the dynamic pulling from queue.
									hence, just mark it as aborted. before entering step(), check. If we are, just kill
									state. all locks are guaranteed to have been freed beforehand.
						2) Release all locks (across all nodes)
						3) Re-queue the txn with a delay.
						*/
						for (size_t i = 0; i<TXN_SIZE; ++i) {
							db_key_t k = nthr.work.t.ops[i];
							auto it = nthr.node->locks.find(k);
							if (it != nthr.node->locks.end() && it->second == nthr.work.t.tid) {
								nthr.node->locks.erase(k);
							}
						}
						sys.aborted.insert(nthr.work.t.tid);
						txn_wrap_t to_append = nthr.work;
						for (size_t i = 0; i<N_NODES; ++i) {
							to_append.thrs[i] = 0;
						}
						to_append.t.tid = new_tid();
						sys.retry.emplace(s + ABORT_DELAY, to_append);
					}
				}
			}
			break;
		}
		case STG_PREPARE: {
			if (nthr.wait_time > 0 && nthr.ready_ct == 0) {
				nthr.wait_time -= 1;
			} else if (nthr.ready_ct == 0) {
				if (nthr.work.node_mask.count() > 1) {
					printf("TXN %lu at step %lu sent PREPARE to %lu peers, waiting.\n", nthr.work.t.tid, s, nthr.work.node_mask.count()-1);
				}
				// get a thread responding to my msg, asap.
				for (size_t i = 0; i<N_NODES; ++i) {
					if (i != nthr.work.coord && nthr.work.node_mask.test(i)) {
						sys.nodes[i].tq.push_back(nthr.work);
					}
				}
				nthr.ready_ct += 1;
			} else if (nthr.ready_ct == nthr.work.node_mask.count()) {
				if (nthr.wait_time != 0) {
					printf("node_id: %lu, thr_id: %lu, nthr.wait_time: %lu\n", nthr.node->id, nthr.id, nthr.wait_time);
				}
				assert(nthr.wait_time == 0);
				nthr.wait_time = nthr.work.node_mask.count() > 1 ? PARTIC_DELAY : 0;
				nthr.ready_ct += 1; // signal that phase is done
			} else if (nthr.wait_time > 0 && nthr.ready_ct == 1+nthr.work.node_mask.count()) {
				nthr.wait_time -= 1;
			} else if (nthr.wait_time == 0 && nthr.ready_ct == 1+nthr.work.node_mask.count()) {
				// done- send the commit!
				printf("TXN %lu at step %lu sent COMMIT to peers.\n", nthr.work.t.tid, s);
				for (size_t i = 0; i<N_NODES; ++i) {
					if (i != nthr.work.coord && nthr.work.node_mask.test(i)) {
						sys.nodes[i].thrs[nthr.work.thrs[i]].commit = true;
					}
				}
				
				sys.committed += 1;
				sys.tid_diffs.push_back(nthr.work.t.tid - nthr.work.t.tid_orig);
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
				printf("TXN %lu (with old tid=%lu) at step %lu sent READY to coord at node %lu, thread %lu. I am node %lu, thread %lu\n", nthr.work.t.tid, nthr.work.t.tid_orig, s, nthr.work.coord, nthr.work.thrs[nthr.work.coord], nthr.node->id, nthr.id);
				// respond back to my coordinator.
				sys.nodes[nthr.work.coord].thrs[nthr.work.thrs[nthr.work.coord]].work.thrs[nthr.node->id] = nthr.id;
				assert(sys.nodes[nthr.work.coord].thrs[nthr.work.thrs[nthr.work.coord]].work.t.tid == nthr.work.t.tid);
				sys.nodes[nthr.work.coord].thrs[nthr.work.thrs[nthr.work.coord]].ready_ct += 1;
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

