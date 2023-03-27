
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
            exec.p4_switch.make_txn(txn_start, (void*) ((char*) pkt_start + sizeof(msg::SwitchTxn)));
            start_fill.emplace_back(txn_start, pkt_start);

            void* pkt_end = pool.allocate(HOT_TXN_PKT_BYTES);
            exec.p4_switch.make_txn(txn_end, (void*) ((char*) pkt_end + sizeof(msg::SwitchTxn)));
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

/*  The switch sockfd right now is just to talk to the simulated switch. In the real setup,
    I want to send to anyone connected by the switch (the switch will then send a reply...)

    TODO not using MSG_ZEROCOPY here- we have very small packets, and Linux documentation
    says the page mgmt overhead for zero-copy is only worth it for sends bigger than 10 kB
    Unless I'm misunderstanding- maybe revisit this? */
static int switch_sockfd;
static struct sockaddr_in server_addr;

void setup_switch_sock() {
    switch_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    assert(switch_sockfd >= 0);

    auto& conf = Config::instance();
    auto& switch_server = conf.servers[conf.switch_id];

	memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(switch_server.port);
	inet_aton((const char*) switch_server.ip.c_str(), &server_addr.sin_addr);
}

static void setup_single_mmsghdr(struct mmsghdr* hdr, struct iovec* ivec) {
    struct msghdr* msg_hdr = &hdr->msg_hdr;
    msg_hdr->msg_name = &server_addr;
    msg_hdr->msg_namelen = sizeof(server_addr);
    msg_hdr->msg_iov = ivec;
    msg_hdr->msg_iovlen = 1;
    msg_hdr->msg_control = NULL; // no ancilliary data
    msg_hdr->msg_controllen = 0;
    msg_hdr->msg_flags = 0;
}

void run_hot_period(TxnExecutor& exec, DeclusteredLayout* layout) {
    std::vector<std::pair<Txn, void*>> start_fill;
    std::vector<std::pair<Txn, void*>> end_fill;
    gen_start_end_packets(start_fill, end_fill, exec, layout);

    for (auto& pr : start_fill) {
        assert(sendto(switch_sockfd, (char*) pr.second, HOT_TXN_PKT_BYTES, 0, (struct sockaddr*) &server_addr, sizeof(server_addr)) == HOT_TXN_PKT_BYTES);
    }
    for (auto& pr : start_fill) {
        // just overwrite the buffer, don't need it now.
        // the recv is just to make sure the packets came back.
        socklen_t unused_len;
        assert(recvfrom(switch_sockfd, (char*) pr.second, HOT_TXN_PKT_BYTES, 0, (struct sockaddr*) &server_addr, &unused_len) == HOT_TXN_PKT_BYTES);
    }

    exec.db.msg_handler->barrier.wait_nodes();

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
            setup_single_mmsghdr(&mmsghdrs[q_p-window_start], &q[q_p].iov);
            q_p += 1;
        }

        ssize_t sent = sendmmsg(switch_sockfd, &mmsghdrs[0], q_p-window_start, 0);
        assert(sent == q_p-window_start);
        ssize_t received = recvmmsg(switch_sockfd, &mmsghdrs[0], q_p-window_start, 0, NULL);
        assert(received == q_p-window_start);
        
        if (q[window_start].mini_batch_num + 1 == q[q_p].mini_batch_num || q_p == q_size) {
            // fprintf(stderr, "mb %u: %lu\n", q[window_start].mini_batch_num, q_p-start_mb_i);
            start_mb_i = q_p;
            exec.db.msg_handler->barrier.wait_nodes();
        }
    }

    for (auto& pr : end_fill) {
        assert(sendto(switch_sockfd, (char*) pr.second, HOT_TXN_PKT_BYTES, 0, (struct sockaddr*) &server_addr, sizeof(server_addr)) == HOT_TXN_PKT_BYTES);
    }
    for (auto& pr : end_fill) {
        socklen_t unused_len;
        assert(recvfrom(switch_sockfd, (char*) pr.second, HOT_TXN_PKT_BYTES, 0, (struct sockaddr*) &server_addr, &unused_len) == HOT_TXN_PKT_BYTES);
        exec.p4_switch.process_reply_txn(&pr.first, pr.second, true);
    }
    pool.clear();
    exec.db.msg_handler->barrier.wait_nodes();
}
