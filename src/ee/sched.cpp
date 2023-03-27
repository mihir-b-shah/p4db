
#include "ee/defs.hpp"
#include "ee/executor.hpp"
#include "main/config.hpp"

#include <vector>
#include <cassert>
#include <cstdio>

/*	build the pool of schedules deterministically.
	handle locality by giving everyone a slice, and then adding as many slices to the local
	node as needed to fit the locality requirement. */
scheduler_t::scheduler_t(TxnExecutor* exec) : exec(exec) {
	auto& config = Config::instance();
	this->node_id = config.node_id;
	this->layout = config.decl_layout;
	/*	first give everyone a slice. then if the remote frac is R, 1-R=(1+s)/(n+s),
		where s is the amount of extra local slices. Also since we store R as a percent,
		which is R', we then get s=(n(100-R')-100)/R' */
	size_t n_extra_local = (config.num_nodes*(100-REMOTE_FRAC)-100)/REMOTE_FRAC;
	schedules.resize(config.num_nodes);

	n_schedules = n_extra_local + 1;
	schedule_len = n_extra_local + config.num_nodes;
	n_queues = schedule_len;

	for (size_t node = 0; node<config.num_nodes; ++node) {
		std::vector<std::vector<size_t>>& node_schedules = schedules[node];
		node_schedules.resize(n_schedules);
		for (size_t s = 0; s<n_schedules; ++s) {
			std::vector<size_t>& schedule = node_schedules[s];
			// property is that the range in the schedule that is all other nodes starts at s.
			// so for example, for n=2, s=0 means 0,1,0,0.. 
			schedule.resize(schedule_len);
			for (size_t i = 0; i<schedule_len; ++i) {
				schedule[i] = (i>=s && i<s+config.num_nodes) ? (i-s) : node;
			}
		}
	}
	mb_queues = new std::queue<txn_pos_t>[n_queues];
}

void scheduler_t::sched_batch(std::vector<Txn>& txns, size_t start, size_t end) {
	for (size_t i = 0; i<n_queues; ++i) {
		assert(mb_queues[i].empty() == true);
	}

	size_t zero_spray_idx = 0;
	for (size_t i = start; i<end; ++i) {
		Txn& txn = txns[i];
		assert(txn.init_done == false);
		extract_hot_cold(exec->kvs, txn, layout);
        assert(txn.init_done == true);

		if (DO_SCHED && txn.hottest_cold_i1.has_value()) {
			const Txn::OP& op1 = txn.cold_ops[txn.hottest_cold_i1.value()];
			const std::vector<size_t>& sched1 = schedules[op1.loc_info.target][op1.id % n_schedules];
			ssize_t s_best = -1;

			if (txn.hottest_cold_i2.has_value()) {
				const Txn::OP& op2 = txn.cold_ops[txn.hottest_cold_i2.value()];
				const std::vector<size_t>& sched2 = schedules[op2.loc_info.target][op2.id % n_schedules];

				for (size_t s = 0; s<schedule_len; ++s) {
					if (sched1[s] == node_id && sched2[s] == node_id &&
						(s_best == -1 || mb_queues[s_best].size() > mb_queues[s].size())) {
						s_best = s;
					}
				}
			}
			if (s_best == -1) {
				for (size_t s = 0; s<schedule_len; ++s) {
					if (sched1[s] == node_id &&
						(s_best == -1 || mb_queues[s_best].size() > mb_queues[s].size())) {
						s_best = s;
					}
				}
			}
			assert(s_best != -1);
            // fprintf(stderr, "placed txn %lu into queue %lu\n", txn.loader_id, s_best);
            assert(txn.loader_id == entry_to_txn(exec, i).loader_id);
			mb_queues[s_best].push(i);
		} else {
			//	TODO is a modulo here a bad idea?
            // fprintf(stderr, "placed txn %lu into queue %lu\n", txn.loader_id, zero_spray_idx);
			mb_queues[zero_spray_idx++ % n_queues].push(i);
		}
	}
    /*
	printf("Queues:\n");
	for (size_t i = 0; i<n_queues; ++i) {
		printf("\tqueue %lu | size %lu\n", i, mb_queues[i].size());
	}
    */
}

Txn& entry_to_txn(TxnExecutor* exec, txn_pos_t entry) {
    std::vector<Txn>& txns = (*(exec->my_txns));
    assert(entry < txns.size());
    return txns[entry];
}

void scheduler_t::print_schedules(size_t node) {
	auto& v = schedules[node];
	for (size_t i = 0; i<n_schedules; ++i) {
		for (size_t j = 0; j<schedule_len; ++j) {
			printf("%lu ", v[i][j]);
		}
		printf("\n");
	}
}

void scheduler_t::process_touched(size_t mb_num) {
    for (db_key_t k : touched) {
        fprintf(stderr, "%lu %lu %lu\n", node_id, mb_num, k);
    }
    //  TODO only when debugging.
    touched.clear();
}
