
#include <comm/comm.hpp>
#include <ee/args.hpp>
#include <ee/database.hpp>
#include <ee/executor.hpp>
#include <layout/declustered_layout.hpp>
#include <main/config.hpp>

#include <array>
#include <utility>

#include <errno.h>

static void fill_op(std::pair<Txn::OP, TupleLocation>& op, bool is_init, db_key_t k, TxnExecutor& exec, DeclusteredLayout* layout) {
    op.first.id = k;
    op.first.mode = is_init ? AccessMode::WRITE : AccessMode::READ;
    op.first.value = exec.kvs->lockless_access(k).value;
    op.second = layout->get_location(k).second;
}

static std::array<std::pair<Txn::OP, TupleLocation>, N_OPS>& init_and_get_arr(Txn& txn) {
    txn.init_done = true;
    txn.do_accel = true;
    return txn.hot_ops_pass1;
}

static constexpr size_t MAX_STACKPOOL_SIZE = 2 * HOT_TXN_PKT_BYTES * ((DeclusteredLayout::N_ACCEL_KEYS+N_OPS-1)/N_OPS);
typedef StackPool<MAX_STACKPOOL_SIZE> se_stackpool_t;
static se_stackpool_t pool;

/*  TODO is this too slow?
    1) we could pre-compute the id_freqs per node, so we don't have to filter like this.
    2) This is an extra pass, could we just generate the packet directly? */
static void gen_start_end_packets(std::vector<void*>& start_fill, std::vector<void*>& end_fill, TxnExecutor& exec, DeclusteredLayout* layout) {
    size_t kp = 0;
    while (kp < DeclusteredLayout::N_ACCEL_KEYS) {
        size_t ops_added = 0;

        Txn txn_start, txn_end;
        auto& arr_start = init_and_get_arr(txn_start);
        auto& arr_end = init_and_get_arr(txn_end);

        while (kp < DeclusteredLayout::N_ACCEL_KEYS && ops_added < N_OPS) {
            db_key_t k = layout->id_freq[kp].first;
            if (exec.kvs->part_info.location(k).is_local) {
                fill_op(arr_start[ops_added], true, k, exec, layout);
                fill_op(arr_end[ops_added], false, k, exec, layout);
                ops_added += 1;
            }
            kp += 1;
        }
        
        if (ops_added < N_OPS) {
            // we hit the end, set the last one to invalid in the txn.
            arr_start[ops_added].first.mode = AccessMode::INVALID;
            arr_end[ops_added].first.mode = AccessMode::INVALID;
        }

        void* pkt_start = pool.allocate(HOT_TXN_PKT_BYTES);
        exec.p4_switch.make_txn(txn_start, (void*) ((char*) pkt_start + sizeof(msg::SwitchTxn)));
        start_fill.push_back(pkt_start);

        void* pkt_end = pool.allocate(HOT_TXN_PKT_BYTES);
        exec.p4_switch.make_txn(txn_end, (void*) ((char*) pkt_end + sizeof(msg::SwitchTxn)));
        end_fill.push_back(pkt_end);
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

/*  TODO as a later optimization- there is no need for tiny messages (i.e. I have 100 byte msgs).
    According to some stats I saw, for 64-byte frames link utilization is ~30%, for 1024-byte it is
    85%. Just put say 10 txns in the packet somehow (have the P4 parser stagger where it starts
    parsing from, if that is possible?). Recirculate at the end of every txn in the packet. 
    Then suddenly our packets are much larger, say 1024 bytes, and we achieve higher link util. */
void run_hot_period(TxnExecutor& exec, DeclusteredLayout* layout) {
    std::vector<void*> start_fill;
    std::vector<void*> end_fill;
    gen_start_end_packets(start_fill, end_fill, exec, layout);

    for (void* buf : start_fill) {
        assert(sendto(switch_sockfd, (char*) buf, HOT_TXN_PKT_BYTES, 0, (struct sockaddr*) &server_addr, sizeof(server_addr)) == HOT_TXN_PKT_BYTES);
    }
    for (void* buf : start_fill) {
        // just overwrite the buffer, don't need it now.
        // the recv is just to make sure the packets came back.
        socklen_t unused_len;
        assert(recvfrom(switch_sockfd, (char*) buf, HOT_TXN_PKT_BYTES, 0, (struct sockaddr*) &server_addr, &unused_len) == HOT_TXN_PKT_BYTES);
    }
    // printf("Sent %lu packets.\n", start_fill.size());

    exec.db.msg_handler->barrier.wait_nodes();

    constexpr size_t MAX_IN_FLIGHT = 100;

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

        // fprintf(stderr, "Sending [%lu,%lu)\n", window_start, q_p);
        ssize_t sent = sendmmsg(switch_sockfd, &mmsghdrs[0], q_p-window_start, 0);
        assert(sent == q_p-window_start);
        ssize_t received = recvmmsg(switch_sockfd, &mmsghdrs[0], q_p-window_start, 0, NULL);
        assert(received == q_p-window_start);
        
        if (q[window_start].mini_batch_num + 1 == q[q_p].mini_batch_num || q_p == q_size) {
            /*  fprintf(stderr, "mb %u: [%lu,%lu)\n", q[window_start].mini_batch_num, start_mb_i, q_p);
                start_mb_i = q_p; */
            exec.db.msg_handler->barrier.wait_nodes();
        }
    }

    for (void* buf : end_fill) {
        assert(sendto(switch_sockfd, (char*) buf, HOT_TXN_PKT_BYTES, 0, (struct sockaddr*) &server_addr, sizeof(server_addr)) == HOT_TXN_PKT_BYTES);
    }
    for (void* buf : end_fill) {
        socklen_t unused_len;
        void* new_pkt = pool.allocate(HOT_TXN_PKT_BYTES);
        assert(recvfrom(switch_sockfd, (char*) new_pkt, HOT_TXN_PKT_BYTES, 0, (struct sockaddr*) &server_addr, &unused_len) == HOT_TXN_PKT_BYTES);

        void* out_buf = (void*) ((char*) buf + sizeof(msg::SwitchTxn));
        void* in_buf = (void*) ((char*) new_pkt + sizeof(msg::SwitchTxn));
        SwitchInfo::SwResult res = exec.p4_switch.parse_txn(out_buf, in_buf);
        for (size_t i = 0; i<res.n_results; ++i) {
            SwitchInfo::read_info_t result = res.results[i];
            exec.kvs->lockless_access(result.k).value = result.reg_val;
        }
    }
    pool.clear();
    exec.db.msg_handler->barrier.wait_nodes();
}
