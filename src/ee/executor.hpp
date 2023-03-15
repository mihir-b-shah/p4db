#pragma once

#include "comm/comm.hpp"
#include "comm/msg.hpp"
#include "comm/msg_handler.hpp"
#include "utils/buffers.hpp"
#include "ee/args.hpp"
#include "ee/database.hpp"
#include "ee/defs.hpp"
#include "ee/errors.hpp"
#include "ee/future.hpp"
#include "ee/switch.hpp"
#include "ee/table.hpp"
#include "utils/mempools.hpp"
#include "utils/ts_factory.hpp"
#include "ee/types.hpp"
#include "ee/undolog.hpp"
#include "utils/util.hpp"
#include "stats/context.hpp"
#include "main/config.hpp"

#include <iostream>
#include <vector>
#include <cassert>
#include <pthread.h>

enum RC {
	COMMIT,
	ROLLBACK,
};

int setup_txn_sched_sock();
void sendall(int sockfd, char* buf, int len);
void recvall(int sockfd, char* buf, int len);

struct sched_pkt_hdr_t {
	size_t node_id;
	size_t thread_id;
};

struct __attribute__((packed)) out_sched_entry_t {
	uint64_t k : 40;
	uint32_t idx : 24;
};

typedef uint32_t txn_pos_t;
struct __attribute__((packed)) in_sched_entry_t {
	txn_pos_t idx : 24;
	uint8_t thr_id : 8;
};

struct TxnExecutor;
struct scheduler_t {
	size_t node_id;
	TxnExecutor* exec;
	DeclusteredLayout* layout;
	std::vector<std::vector<std::vector<size_t>>> schedules;
	size_t n_schedules;
	size_t n_queues;
	size_t schedule_len;
	//	TODO: is this efficient enough? Replace with a vector+pointer.
	std::queue<in_sched_entry_t>* mb_queues;

	scheduler_t(TxnExecutor* exec);
	~scheduler_t(){ delete[] mb_queues; }
	void sched_batch(std::vector<Txn>& txns, size_t s, size_t e);
	Txn& entry_to_txn(in_sched_entry_t entry);
	void print_schedules(size_t node);
};

struct TxnExecutor {
    StructTable* kvs;
    SwitchInfo p4_switch;
    Database& db;
    Undolog log;
    StackPool<8192> mempool;
    uint32_t tid;
	uint32_t mini_batch_num;

	/*
	char* raw_buf;
	out_sched_entry_t* sched_packet_buf;
	int txn_sched_sockfd;
	int sched_packet_buf_len;
	int sched_reply_len;
	*/

	std::vector<Txn> non_accel_txns;

    TimestampFactory ts_factory;
    timestamp_t ts;

    TxnExecutor(Database& db)
        : db(db), log(db.comm.get()), tid(WorkerContext::get().tid), mini_batch_num(1) {
        db.get_casted(KV::TABLE_NAME, kvs);
		// setup_txn_sched();
	}

	// void setup_txn_sched();
	// void send_get_txn_sched();

	~TxnExecutor() {
		// free(raw_buf);
	}

	RC my_execute(Txn& arg, Communicator::Pkt_t** packet_fill);
    RC execute(Txn& arg);
    RC commit();
    RC rollback();
    TupleFuture<KV>* read(StructTable* table, const Txn::OP& op, TxnId id);
    TupleFuture<KV>* write(StructTable* table, const Txn::OP& op, TxnId id);
    TupleFuture<KV>* insert(StructTable* table);
};

void extract_hot_cold(StructTable* table, Txn& txn, DeclusteredLayout* layout);
void txn_executor(Database& db, std::vector<Txn>& txns);
