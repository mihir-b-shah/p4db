
#ifndef _SIM_H_
#define _SIM_H_

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <optional>

#include "mempool.h"

typedef size_t db_key_t;

#define N_STAGES 20
#define REGS_PER_STAGE 4
#define SLOTS_PER_REG 16000
#define MAX_BATCH 10000
#define FRAC_HOT 0.01

struct tuple_loc_t {
    size_t stage;
    size_t reg;
    size_t idx;

    bool operator==(const tuple_loc_t& tl) {
        return stage == tl.stage && reg == tl.stage;
    }
};

struct txn_t {
    std::vector<db_key_t> ops;
};

enum class workload_e {
    INSTACART,
    YCSB,
    SYN_UNIF,
};

class batch_iter_t {
public:
    batch_iter_t(std::vector<txn_t> all_txns) : all_txns_(all_txns), pos_(0) {}
    std::vector<txn_t> next_batch();

private:
    std::vector<txn_t> all_txns_;
    size_t pos_;
};

typedef std::unordered_map<db_key_t, tuple_loc_t> layout_t;

batch_iter_t get_batch_iter(workload_e wtype);
layout_t get_layout(const std::vector<txn_t>& txns);
void run_batch(const std::vector<txn_t>& txns);

/*  Each port is 100 GbE. A port group is 400 GbE.
    We'll act as if a parser can handle 400 GbE instead of needing 4 parsers of 100 GbE.
    Reserve half ports/pipes for uplink.
    Our ycsb txns are 88 bytes. UDP+ethernet adds 28 bytes. Round to 120 bytes.
    Each input buffer is 64 kB
    Not implementing egress for now, since resubmission/holding locks only applies
    to ingress pipe.
    Mock egress is just a way to let packets exit.
    Ignoring the deparser here, since its more hassle than worth it.
    We treat the lock table separately for implementation simplicity. */

#define N_PORTS 32
#define N_PORT_GROUPS 8
#define RECIRC_PORT 8
#define IPB_SIZE 500

typedef size_t sw_txn_id_t;
typedef size_t sw_val_t;

struct sw_pass_txn_t {
    std::optional<size_t> grid[N_STAGES][REGS_PER_STAGE];
};

struct sw_txn_t {
    size_t port;
    sw_txn_id_t id;
    size_t pass_ct;
    bool failed_lock;
    std::vector<sw_pass_txn_t> passes;

    // zero-arg constructor needed for mempool
    sw_txn_t() : id(0), pass_ct(0), failed_lock(false) {}
    sw_txn_t(size_t port, const layout_t& layout, const txn_t& txn);
};

class switch_t {
public:
    switch_t() : regs_{{0}} {}
    bool ipb_almost_full(size_t port, double thr);
    bool send(sw_txn_t txn);
    void run_cycle();
    std::optional<sw_txn_id_t> recv(size_t port);

private:
    typedef sw_mempool_t<sw_txn_t, 4096> txn_pool_t;

    txn_pool_t txn_pool_;
    std::queue<txn_pool_t::slot_id_t> ipb_[N_PORT_GROUPS + 1];
    std::optional<txn_pool_t::slot_id_t> parser_[N_PORT_GROUPS + 1];
    std::optional<txn_pool_t::slot_id_t> ingr_pipe_[N_STAGES];
    std::queue<txn_pool_t::slot_id_t> mock_egress_[N_PORTS];
    sw_val_t regs_[N_STAGES][REGS_PER_STAGE][SLOTS_PER_REG];
    
    void run_reg_ops(size_t i);
    void ipb_to_parser(size_t i);
    void print_state();
};

#endif
