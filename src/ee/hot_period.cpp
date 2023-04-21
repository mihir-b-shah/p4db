
#include <comm/comm.hpp>
#include <ee/args.hpp>
#include <ee/defs.hpp>
#include <ee/database.hpp>
#include <ee/executor.hpp>
#include <layout/declustered_layout.hpp>
#include <main/config.hpp>

#include <array>
#include <utility>

#include <errno.h>

/*  TODO Why does the switch process get different # of txns? Can't be drops, since
    otherwise this would stall. I speculate it is b/c we choose not to accelerate txns
    that don't fit in our batches- which is based on non-deterministic multithreading effects. */
static void fill_op(std::pair<Txn::OP, TupleLocation>& op, bool is_init, db_key_t k, TxnExecutor& exec, DeclusteredLayout* layout) {
    op.first.id = k;
    op.first.mode = is_init ? AccessMode::WRITE : AccessMode::READ;
    op.first.value = exec.kvs->lockless_access(k).value;
    op.second = layout->get_location(k).second;
}

static std::array<std::pair<Txn::OP, TupleLocation>, N_OPS>* init_and_get_arr(Txn& txn) {
    txn.init_done = true;
    txn.do_accel = true;
    return &txn.hot_ops_pass1;
}

static constexpr size_t MAX_STACKPOOL_SIZE = 2 * HOT_TXN_PKT_BYTES * ((N_ACCEL_KEYS+N_OPS-1)/N_OPS);
typedef StackPool<MAX_STACKPOOL_SIZE> se_stackpool_t;
static se_stackpool_t pool;

/*  TODO is this too slow?
    1) we could pre-compute the id_freqs per node, so we don't have to filter like this.
    2) This is an extra pass, could we just generate the packet directly? */
static void gen_start_end_packets(std::vector<std::pair<Txn, void*>>& start_fill, std::vector<std::pair<Txn, void*>>& end_fill, TxnExecutor& exec, DeclusteredLayout* layout) {
    Txn txn_start, txn_end;
    std::array<std::pair<Txn::OP, TupleLocation>, N_OPS>* arr_start = nullptr;
    std::array<std::pair<Txn::OP, TupleLocation>, N_OPS>* arr_end = nullptr;
    size_t ops_added = 0;

    auto make_txn = [&](bool first){
        if (!first) {
            void* pkt_start = pool.allocate(HOT_TXN_PKT_BYTES);
            exec.p4_switch.make_txn(txn_start, pkt_start);
            start_fill.emplace_back(txn_start, pkt_start);

            void* pkt_end = pool.allocate(HOT_TXN_PKT_BYTES);
            exec.p4_switch.make_txn(txn_end, pkt_end);
            end_fill.emplace_back(txn_end, pkt_end);
        }

        ops_added = 0;
        txn_start = Txn();
        txn_end = Txn();
        arr_start = init_and_get_arr(txn_start);
        arr_end = init_and_get_arr(txn_end);
    };
    make_txn(true);

    for (size_t s = 0; s<SLOTS_PER_SCHED_BLOCK; ++s) {
        for (size_t r = 0; r<N_REGS; ++r) {
            std::optional<db_key_t> k = layout->rev_lookup(r, s + SLOTS_PER_SCHED_BLOCK * layout->block_num);
            if (k.has_value() && exec.kvs->part_info.location(k.value()).is_local) {
                fill_op((*arr_start)[ops_added], true, k.value(), exec, layout);
                fill_op((*arr_end)[ops_added], false, k.value(), exec, layout);
                if (++ops_added == N_OPS) {
                    make_txn(false);
                }
            }
        }

        assert(ops_added < N_OPS);
        if (ops_added > 0) {
            (*arr_start)[ops_added].first.mode = AccessMode::INVALID;
            (*arr_end)[ops_added].first.mode = AccessMode::INVALID;
            make_txn(false);
        }
    }
}

void run_hot_period(TxnExecutor& exec, DeclusteredLayout* layout) {
    switch_intf_t& sw_intf = Config::instance().sw_intf;

    std::vector<std::pair<Txn, void*>> start_fill;
    std::vector<std::pair<Txn, void*>> end_fill;
    gen_start_end_packets(start_fill, end_fill, exec, layout);
    int rc;

    struct timespec timeout;
    timeout.tv_sec = N_SECS_TIMEOUT;
    timeout.tv_nsec = N_NSECS_TIMEOUT;

    for (auto& pr : start_fill) {
        struct iovec ivec = {pr.second, HOT_TXN_PKT_BYTES};
        struct msghdr msg_hdr;
        sw_intf.prepare_msghdr(&msg_hdr, &ivec);
        rc = sendmsg(sw_intf.sockfd, &msg_hdr, 0);
        assert(rc == HOT_TXN_PKT_BYTES);
    }
    for (auto& pr : start_fill) {
        // just overwrite the buffer, don't need it now.
        // the recv is just to make sure the packets came back.
        struct iovec ivec = {pr.second, HOT_TXN_PKT_BYTES};
        struct msghdr msg_hdr;
        sw_intf.prepare_msghdr(&msg_hdr, &ivec);
        rc = recvmsg(sw_intf.sockfd, &msg_hdr, 0);

        //  Just silently drop packet for now...
        if (rc == -1) {
            assert(errno == EAGAIN || errno == EWOULDBLOCK);
            exec.n_dropped += 1;
        } else {
            assert(rc == HOT_TXN_PKT_BYTES);
        }
    }

    exec.db.msg_handler->barrier.wait_nodes();
    fprintf(stderr, "Made it past start_fill.\n");

    //  TODO MAX=100 is causing packet drops??
    constexpr size_t MAX_IN_FLIGHT = 50;

    size_t q_size = exec.db.hot_send_q.send_q_tail;
    hot_send_q_t::hot_txn_entry_t* q = exec.db.hot_send_q.send_q;
    size_t q_p = 0;
    size_t start_mb_i = 0;
    struct mmsghdr mmsghdrs[MAX_IN_FLIGHT];

    while (q_p < q_size) {
        size_t window_start = q_p;
        while (q_p < q_size && q[window_start].mini_batch_num == q[q_p].mini_batch_num 
                && q_p - window_start < MAX_IN_FLIGHT) {
            sw_intf.prepare_msghdr(&mmsghdrs[q_p-window_start].msg_hdr, &q[q_p].iov);
            q_p += 1;
        }

        ssize_t sent = sendmmsg(sw_intf.sockfd, &mmsghdrs[0], q_p-window_start, 0);
        assert(sent == q_p-window_start);

        fprintf(stderr, "Before-Recvmmsg.\n");
        ssize_t received = recvmmsg(sw_intf.sockfd, &mmsghdrs[0], q_p-window_start, 0, NULL);
        fprintf(stderr, "After-Recvmmsg. %u %u %lu %lu\n", q[window_start].mini_batch_num + 1, q[q_p].mini_batch_num, q_p, q_size);
        assert(q_p == q_size || q[window_start].mini_batch_num <= q[q_p].mini_batch_num);

        if (received < q_p-window_start) {
            //  Overcounts n_dropped by 1 if recieved=-1, who cares...
            exec.n_dropped += q_p - window_start - received;
        }

        if (q[window_start].mini_batch_num + 1 == q[q_p].mini_batch_num || q_p == q_size) {
            // fprintf(stderr, "mb %u: %lu\n", q[window_start].mini_batch_num, q_p-start_mb_i);
            start_mb_i = q_p;
            exec.db.msg_handler->barrier.wait_nodes();
        }
    }

    for (auto& pr : end_fill) {
        struct iovec ivec = {pr.second, HOT_TXN_PKT_BYTES};
        struct msghdr msg_hdr;
        sw_intf.prepare_msghdr(&msg_hdr, &ivec);
        rc = sendmsg(sw_intf.sockfd, &msg_hdr, 0);
        assert(rc == HOT_TXN_PKT_BYTES);
    }

    // fprintf(stderr, "Made it to end_fill.\n");
    for (auto& pr : end_fill) {
        // just overwrite the buffer, don't need it now.
        // the recv is just to make sure the packets came back.
        struct iovec ivec = {pr.second, HOT_TXN_PKT_BYTES};
        struct msghdr msg_hdr;
        sw_intf.prepare_msghdr(&msg_hdr, &ivec);
        rc = recvmsg(sw_intf.sockfd, &msg_hdr, 0);

        if (rc == -1) {
            assert(errno == EAGAIN || errno == EWOULDBLOCK);
            exec.n_dropped += 1;
        } else {
            assert(rc == HOT_TXN_PKT_BYTES);
            exec.p4_switch.process_reply_txn(&pr.first, pr.second, true);
        }
    }

    pool.clear();
    exec.db.msg_handler->barrier.wait_nodes();
}
