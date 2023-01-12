
#include "sim.h"

#include <cstdio>
#include <cassert>
#include <cstdlib>

namespace stats {
    size_t num_cycles = 0;
    size_t num_txns = 0;
    size_t num_passes = 0;
    size_t num_dirty_ops = 0;
    size_t num_ops = 0;
}

static void print_stats() {
    printf("Num cycles: %lu\n", stats::num_cycles);
    printf("Num txns: %lu\n", stats::num_txns);
    printf("Num passes: %lu\n", stats::num_passes);
    printf("Num dirty ops: %lu\n", stats::num_dirty_ops);
    printf("Num ops: %lu\n", stats::num_ops);
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
    static size_t incr_id = 1;
    size_t group = port_to_group(txn.port);
    if (ipb_[group].size() < IPB_SIZE) {
        txn.id = incr_id++;
        txn_pool_t::slot_id_t slot = txn_pool_.alloc();
        txn_pool_.at(slot) = txn;
        ipb_[group].push(slot);
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
                std::optional<size_t> slot_op = txn.passes[txn.pass_ct].grid[i][j];
                if (slot_op.has_value()) {
                    // violates serializability
                    if (regs_[i][j][slot_op.value()] > txn.id) {
                        stats::num_dirty_ops += 1;
                    }
                    stats::num_ops += 1;
                    regs_[i][j][slot_op.value()] = txn.id;
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

bool switch_t::manage_locks(const sw_txn_t& txn) {
    // whole-pipe lock
    static std::optional<sw_txn_id_t> holder = std::nullopt;
    if (!holder.has_value()) {
        if (txn.passes.size() != 1) {
            // only acquire for multi-pass
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

#define LOOKUP_OPT(opt,var,dflt_val) ((opt).has_value() ? txn_pool_.at((opt).value()).var : dflt_val)

void switch_t::print_state() {
    for (size_t i = 0; i<N_PORT_GROUPS + 1; i += 8) {
        printf("ipb size on port %lu: %lu\n", i, ipb_[i].size());
        printf("parser occupied on port %lu: %lu\n", i, LOOKUP_OPT(parser_[i], id, 0));
    }
    if (LOOKUP_OPT(ingr_pipe_[N_STAGES-1], valid, 0) && 
        LOOKUP_OPT(ingr_pipe_[N_STAGES-1], passes.size(), 0) == 
            1 + LOOKUP_OPT(ingr_pipe_[N_STAGES-1], pass_ct, 0)) {
        printf("txn leaving has %lu passes, %lu ops\n", LOOKUP_OPT(ingr_pipe_[N_STAGES-1], passes.size(), 0), LOOKUP_OPT(ingr_pipe_[N_STAGES-1], orig_txn.ops.size(), 0));
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
            mock_egress_[txn.port].push(txn_slot);
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

std::optional<sw_txn_id_t> switch_t::recv(size_t port) {
    if (mock_egress_[port].empty()) {
        return std::nullopt;
    } else {
        txn_pool_t::slot_id_t slot = mock_egress_[port].front();
        sw_txn_id_t id = txn_pool_.at(slot).id;
        mock_egress_[port].pop();
        txn_pool_.free(slot);
        return id;
    }
}
