
#ifndef _SIM_H_
#define _SIM_H_

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <bitset>
#include <list>
#include <utility>
#include <queue>
#include <optional>

#include "mempool.h"

typedef size_t db_key_t;

#define N_STAGES 18
#define REGS_PER_STAGE 2
#define SLOTS_PER_REG (14000000/(REGS_PER_STAGE*N_STAGES))
#define MAX_BATCH 10000
#define FRAC_HOT 0.001
#define N_MAX_HOT_OPS 8

struct tuple_loc_t {
    size_t stage;
    size_t reg;
    size_t idx;

    bool operator==(const tuple_loc_t& tl) {
        return stage == tl.stage && reg == tl.reg;
    }
};

struct txn_t {
    std::vector<db_key_t> ops;
};

enum class workload_e {
    INSTACART,
    YCSB_80_8,
    YCSB_99_8,
    YCSB_99_16,
    SYN_UNIF,
    SYN_HOT_8,
    SYN_ADVERSARIAL,
};

std::vector<std::pair<db_key_t, size_t>> get_key_cts(const std::vector<txn_t>& txns);

class batch_iter_t {
public:
    batch_iter_t(std::vector<txn_t> all_txns);
    std::vector<txn_t> next_batch();

private:
    // first is hot, second is cold
    std::list<std::pair<txn_t, txn_t>> txn_comps_;
};

class layout_t {
public:
    layout_t(const std::vector<txn_t>& txns);
    size_t get_key_ct(db_key_t key) const;
    std::optional<tuple_loc_t> lookup(db_key_t key) const;
    db_key_t rev_lookup(size_t stage, size_t reg, size_t idx) const;
    size_t num_keys() const { return forward_.size(); }

private:
    std::unordered_map<db_key_t, tuple_loc_t> forward_;
    std::unordered_map<size_t, db_key_t> backward_per_reg_[N_STAGES][REGS_PER_STAGE];
    std::vector<std::pair<db_key_t, size_t>> keys_sorted_;
    std::unordered_map<db_key_t, size_t> key_cts_;

    void naive_spray_impl(const std::vector<txn_t>& txns);
    void freq_heuristic_impl(const std::vector<txn_t>& txns);
    void better_random_impl(const std::vector<txn_t>& txns);
    void random_spray_impl(const std::vector<txn_t>& txns);
};

batch_iter_t get_batch_iter(workload_e wtype);

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
#define N_LOCKS 3

typedef size_t sw_txn_id_t;

struct sw_val_t {
    inline static constexpr sw_txn_id_t START_TXN_ID = 0;
    size_t last_txn_id;

    sw_val_t() : last_txn_id(START_TXN_ID) {}
};

struct sw_pass_txn_t {
    std::optional<size_t> grid[N_STAGES][REGS_PER_STAGE];
};

struct sw_txn_t {
    size_t port;
    sw_txn_id_t id;
    size_t pass_ct;
    txn_t orig_txn;

    bool valid;
	std::bitset<N_LOCKS> locks_check;
	std::bitset<N_LOCKS> locks_wanted;
	std::bitset<N_LOCKS> locks_undo;

    std::vector<tuple_loc_t> locs;
    std::vector<sw_pass_txn_t> passes;
    std::optional<tuple_loc_t> one_lock;

    // zero-arg constructor needed for mempool
    sw_txn_t() : id(0), pass_ct(0), valid(true) {}
    sw_txn_t(size_t port, const layout_t& layout, const txn_t& txn);
};

std::vector<sw_txn_t> prepare_txns_sw(size_t port, const std::vector<txn_t>& txns, const layout_t& lay);

class switch_t {
public:
    switch_t() {}
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
    bool manage_locks(sw_txn_t& txn);
    void print_state();
};

#endif
