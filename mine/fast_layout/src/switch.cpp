
#include "sim.h"

#include <cstdio>
#include <cassert>
#include <cstdlib>
#include <limits>
#include <unordered_map>
#include <algorithm>

namespace stats {
    size_t num_cycles = 0;
    size_t num_txns = 0;
    size_t num_passes = 0;
    size_t num_ops = 0;

	class serial_checker_t {
	public:
		serial_checker_t() : max_tid_(0) {}
		~serial_checker_t() {
			// a DAG is guaranteed to have a topological order, which is our serializable order.
			// this is sufficient according to google :)
			for (size_t i = 1; i<=max_tid_; ++i) {
				colors_.insert({i, UNVISITED});
			}

			bool ok = true;
			for (size_t i = 1; i<=max_tid_; ++i) {
				assert(colors_[i] == UNVISITED || colors_[i] == VISITED);
				if (colors_[i] == UNVISITED) {
					if (!walk_graph(i)) {
						ok = false;
						break;
					}
				}
			}
			
			if (ok) {
				printf("Serializable history.\n");
			} else {
				assert(false && "Unserializable history.");
			}
		}

		void add_dep(sw_txn_id_t tid, sw_txn_id_t old) {
			max_tid_ = std::max(max_tid_, tid);
			if (deps_.find(tid) == deps_.end()) {
				std::vector<sw_txn_id_t> v;
				deps_.insert({tid, v});
			}
			deps_[tid].push_back(old);
		}

	private:
		bool walk_graph(size_t u) {
			const std::vector<sw_txn_id_t>& next = deps_[u];
			colors_[u] = VISITING;

			for (sw_txn_id_t v : next) {
				if (colors_[v] == UNVISITED) {
					if (!walk_graph(v)) {
						return false;
					}
				} else if (colors_[v] == VISITING) {
					// cycle detected.
					return false;
				}
			}

			colors_[u] = VISITED;
			return true;
		}

		enum color_e {
			UNVISITED,
			VISITING,
			VISITED,
		};

		std::unordered_map<sw_txn_id_t, std::vector<sw_txn_id_t>> deps_;
		std::unordered_map<sw_txn_id_t, color_e> colors_;
		size_t max_tid_;
	};

	serial_checker_t checker;
}

static void print_stats() {
    printf("-------- General stats ---------\n");
    printf("Num cycles: %lu\n", stats::num_cycles);
    printf("Num txns: %lu\n", stats::num_txns);
    printf("Num passes: %lu\n", stats::num_passes);
    printf("Num ops: %lu\n", stats::num_ops);
    printf("--------------------------------\n");
}

__attribute__((constructor))
static void register_stats() {
    atexit(print_stats);
}

inline static size_t port_to_group(size_t port) {
    return port / (N_PORTS / N_PORT_GROUPS);
}

bool switch_t::ipb_almost_full(size_t port, double thr) {
    return ipb_[port_to_group(port)].size() >= IPB_SIZE*thr;
}

bool switch_t::send(sw_txn_t txn) {
    assert(txn.passes.size() > 0);
    static size_t incr_id = 1;
    size_t group = port_to_group(txn.port);
    if (ipb_[group].size() < IPB_SIZE) {
        txn.id = incr_id++;
        txn_pool_t::slot_id_t slot = txn_pool_.alloc();
        txn_pool_.at(slot) = txn;
        ipb_[group].push(slot);

		/*
        printf("txn %lu entering: ", txn.id);
        for (size_t i = 0; i<txn.locs.size(); ++i) {
            printf("{%lu,%lu,%lu} ", txn.locs[i].stage, txn.locs[i].reg, txn.locs[i].idx);
        }
        printf("| passes: %lu\n", txn.passes.size());
        printf("txn %lu entering ORIG: ", txn.id);
        for (size_t i = 0; i<txn.orig_txn.ops.size(); ++i) {
            printf("k=%lu ", txn.orig_txn.ops[i]);
        }
        printf("| passes: %lu\n", txn.passes.size());
		*/
        return true;
    } else {
        return false;
    }
}

void switch_t::run_reg_ops(size_t i) {
    if (ingr_pipe_[i].has_value()) {
        txn_pool_t::slot_id_t txn_slot = ingr_pipe_[i].value();
        sw_txn_t& txn = txn_pool_.at(txn_slot);
        if (txn.valid) {
            for (size_t j = 0; j<REGS_PER_STAGE; ++j) {
                auto& s1 = txn.passes[txn.pass_ct].grid;
                std::optional<size_t> slot_op = s1[i][j];
                
                if (slot_op.has_value()) {
                    sw_val_t& ref = regs_[i][j][slot_op.value()];
					stats::checker.add_dep(txn.id, ref.last_txn_id);
                    stats::num_ops += 1;
                    ref.last_txn_id = txn.id;
                }
            }
        }
    }
}

void switch_t::ipb_to_parser(size_t i) {
    if (!ipb_[i].empty()) {
        parser_[i] = ipb_[i].front();
        ipb_[i].pop();
    }
}

namespace helpers {
	template <typename F>
	class defer_action_t {
	public:
		defer_action_t(const F& action) : action_(action) {}
		~defer_action_t() {
			action_();
		}
	private:
		const F& action_;
	};
}

static void print_bitset(const char* prefix, std::bitset<N_LOCKS> bset) {
	printf("%s: {", prefix);
	for (size_t i = 0; i<N_LOCKS; ++i) {
		if (bset[i]) {
			printf("%lu, ", i);
		}
	}
	printf("}");
}

static bool whole_pipe_lock(sw_txn_t& txn) {
	static std::optional<sw_txn_id_t> holder = std::nullopt;
	if (!holder.has_value()) {
		// only acquire for multi-pass
		if (txn.passes.size() >= 2) {
			holder = txn.id;
		}
		return true;
	} else {
		if (holder.value() == txn.id) {
			// there's a lock, it's held by me
			if (txn.passes.size() == txn.pass_ct + 1) {
				// last pass, release it.
				holder = std::nullopt;
			}
			return true;
		} else {
			return false;
		}
	}
}

/*	being able to lock like normal at fine grain is optimal, but
	probably not possible. */
static bool granular_lock_OPT(sw_txn_t& txn) {
	static std::bitset<N_LOCKS> locks;
	if (txn.pass_ct == 0) {
		if ((locks & txn.locks_check).none()) {
			assert((locks & txn.locks_wanted) == 0);
			locks |= txn.locks_wanted;
			return true;
		} else {
			return false;
		}
	} else if (txn.pass_ct == txn.passes.size()-1) {
		assert((locks & txn.locks_wanted) == txn.locks_wanted);
		locks ^= txn.locks_wanted;
		return true;
	} else {
		return true;
	}
}

static bool granular_lock_real(sw_txn_t& txn) {
	static std::bitset<N_LOCKS> locks;

	printf("Txn %lu, p %lu, f %lu | ", txn.id, txn.pass_ct, txn.fail_ct);
	print_bitset("lglob", locks);
	print_bitset(" chk", txn.locks_check);
	print_bitset(" want", txn.locks_wanted);
	print_bitset(" undo", txn.locks_undo);
	printf("\n");

	if (txn.pass_ct == 0) {
		if (txn.locks_undo.none()) {
			// I'm not coming around to undo stuff, full speed ahead!
			std::bitset<N_LOCKS> before = locks;
			locks |= txn.locks_wanted;

			if ((before & txn.locks_check).none()) {
				// ok great, it worked.
				assert((before & txn.locks_wanted) == 0);
				printf("Txn %lu, p %lu | decided TRUE (1)\n", txn.id, txn.pass_ct);
				return true;
			} else {
				txn.locks_undo = (~before) & txn.locks_wanted;
				txn.fail_ct += 1;
				// do a fast recirc.
				printf("Txn %lu, p %lu | decided FALSE (2)\n", txn.id, txn.pass_ct);
				return false;
			}
		} else {
			// Ok just to undo.
			locks ^= txn.locks_undo;
			txn.locks_undo.reset();
			printf("Txn %lu, p %lu | decided FALSE (3)\n", txn.id, txn.pass_ct);
			return false;
		}
	} else if (txn.pass_ct == txn.passes.size()-1) {
		assert((locks & txn.locks_wanted) == txn.locks_wanted);
		locks ^= txn.locks_wanted;
		printf("Txn %lu, p %lu | decided TRUE (4)\n", txn.id, txn.pass_ct);
		return true;
	} else {
		printf("Txn %lu, p %lu | decided TRUE (5)\n", txn.id, txn.pass_ct);
		return true;
	}
}

bool switch_t::manage_locks(sw_txn_t& txn) {
    // return whole_pipe_lock(txn);
	// return granular_lock_OPT(txn);
	return granular_lock_real(txn);
	// return true;
}

#define LOOKUP_OPT(opt,var,dflt_val) ((opt).has_value() ? txn_pool_.at((opt).value()).var : dflt_val)

void switch_t::print_state() {
    for (size_t i = 0; i<N_PORT_GROUPS + 1; i += 8) {
        printf("ipb size on port %lu: %lu\n", i, ipb_[i].size());
        printf("parser occupied on port %lu: %lu\n", i, LOOKUP_OPT(parser_[i], id, 0));
    }
    if (LOOKUP_OPT(ingr_pipe_[N_STAGES-1], valid, 0) && 
        LOOKUP_OPT(ingr_pipe_[N_STAGES-1], passes.size(), 0) == 
            1 + LOOKUP_OPT(ingr_pipe_[N_STAGES-1], pass_ct, 0)) {
        printf("txn %lu leaving has %lu passes, %lu ops\n", LOOKUP_OPT(ingr_pipe_[N_STAGES-1], id, 0), LOOKUP_OPT(ingr_pipe_[N_STAGES-1], passes.size(), 0), LOOKUP_OPT(ingr_pipe_[N_STAGES-1], orig_txn.ops.size(), 0));
    }
    
    for (size_t i = 0; i<N_STAGES; ++i) {
        printf("stage %lu occupied: %lu, valid: %d\n", i, LOOKUP_OPT(ingr_pipe_[i], id, 0),
            LOOKUP_OPT(ingr_pipe_[i], valid, 0));
    }
    printf("\n");
}

void switch_t::run_cycle() {
    stats::num_cycles += 1;
    print_state();

    run_reg_ops(N_STAGES-1);
    if (ingr_pipe_[N_STAGES-1].has_value()) {
        txn_pool_t::slot_id_t txn_slot = ingr_pipe_[N_STAGES-1].value();
        sw_txn_t& txn = txn_pool_.at(txn_slot);
        if (txn.valid) {
            txn.pass_ct += 1;
        }
        stats::num_passes += 1;

        if (txn.passes.size() == txn.pass_ct) {
            stats::num_txns += 1;
            mock_egress_[txn.port].push({true, txn_slot});
        } else if (txn.pass_ct == 0 && txn.fail_ct >= MAX_FAIL_CT && txn.locks_undo.none()) {
			// i.e. I'm just recirculating too long, I don't have anything to undo.
			mock_egress_[txn.port].push({false, txn_slot});
		} else {
            txn.valid = true;
            ipb_[RECIRC_PORT].push(txn_slot);
        }
    }
    for (ssize_t i = N_STAGES-1; i>=1; --i) {
        run_reg_ops(i-1);
        ingr_pipe_[i] = ingr_pipe_[i-1];
    }
    // who has a parse result ready, of them, whose ipb is fullest?
    size_t idx = 0;
    ssize_t idx_ipb_size = -1;
    if (parser_[RECIRC_PORT].has_value()) {
        idx = RECIRC_PORT;
        idx_ipb_size = 0;
    } else {
        for (size_t i = 0; i<N_PORT_GROUPS; ++i) {
            if (parser_[i].has_value() && static_cast<ssize_t>(ipb_[i].size()) > idx_ipb_size) {
                idx = i;
                idx_ipb_size = ipb_[i].size();
            }
        }
    }

    if (idx_ipb_size == -1) {
        // no one had a value.
        ingr_pipe_[0] = std::nullopt;
    } else {
        sw_txn_t& txn = txn_pool_.at(parser_[idx].value());
        txn.valid = manage_locks(txn);
        ingr_pipe_[0] = parser_[idx];
        parser_[idx] = std::nullopt;
        ipb_to_parser(idx);
    }

    // fill the unfilled parsers, including recirc port.
    for (size_t i = 0; i<1+N_PORT_GROUPS; ++i) {
        if (!parser_[i].has_value()) {
            ipb_to_parser(i);
        }
    }
}

std::optional<std::pair<bool, sw_txn_id_t>> switch_t::recv(size_t port) {
    if (mock_egress_[port].empty()) {
        return std::nullopt;
    } else {
        std::pair<bool, txn_pool_t::slot_id_t> slot = mock_egress_[port].front();
        sw_txn_id_t id = txn_pool_.at(slot.second).id;
        mock_egress_[port].pop();
        txn_pool_.free(slot.second);

		std::pair<bool, sw_txn_id_t> res;
		res.first = slot.first;
		res.second = id;
		return res;
    }
}
