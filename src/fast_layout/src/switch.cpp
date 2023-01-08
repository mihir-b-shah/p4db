
#include "sim.h"

inline static size_t port_to_group(size_t port) {
    return port / (N_PORTS / N_PORT_GROUPS);
}

bool switch_t::send(size_t port, sw_txn_t txn) {
    size_t group = port_to_group(port);
    if (ipb_[group].size() < IPB_SIZE) {
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
        for (size_t j = 0; j<N_REGS; ++j) {
            std::optional<size_t> slot_op = txn.passes[txn.pass_num][i][j];
            if (slot_op.has_value()) {
                regs_[i][j][op.value()] = txn.id;
            }
        }
    }
}

void switch_t::ipb_to_parser(size_t i) {
    if (!ipb_[i].empty()) {
        parser_[i] = ipb_[i].front();
        ipb_[i].poll();
    }
}

void switch_t:run_cycle() {
    run_reg_ops(N_STAGES-1);
    if (ingr_pipe_[N_STAGES-1].has_value()) {
        txn_pool_t::slot_id_t txn_slot = ingr_pipe_[N_STAGES-1].value();
        sw_txn_t& txn = txn_pool_.at(txn_slot);
        if (txn.passes.size() == txn.pass_ct) {
            mock_egress_[txn.port].push(txn_slot);
        } else {
            txn.pass_ct += 1;
            ipb_[RECIRC_PORT].push(txn_slot);
        }
    }
    for (ssize_t i = N_STAGES-1; i>=1; --i) {
        run_reg_ops(i-1);
        ingr_pipe_[i] = ingr_pipe[i-1];
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
        ingr_pipe_[0] = parser_[idx];
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
