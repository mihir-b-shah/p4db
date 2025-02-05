#pragma once

#include "ee/future.hpp"
#include "utils/spinlock.hpp"
#include "ee/types.hpp"

#include <mutex>
#include <tbb/queuing_mutex.h>
#include <tbb/queuing_rw_mutex.h>

#include <cstdio>

template <typename Tuple_t>
struct Row {
    using lock_t = std::mutex;
    // using lock_t = SpinLock;
    // using lock_t = tbb::queuing_mutex;

    lock_t mutex;

    AccessMode lock_type = AccessMode::INVALID;
    uint32_t owner_cnt = 0;

    Tuple_t tuple;
	// TODO: maybe split into reader/writer for higher concurrency?
	TxnId last_acq;

	Row() : last_acq(true, 0, 0) {}

    using Future_t = TupleFuture<Tuple_t>;

	inline bool mb_allow_lock(TxnId txn_id) {
		if (txn_id.field.valid) {
			// TODO check for wrap-around!
			assert(txn_id.field.mini_batch_id >= last_acq.field.mini_batch_id);
			if (txn_id.field.mini_batch_id == last_acq.field.mini_batch_id &&
				(!USE_FLOW_ORDER || txn_id.field.node_id != last_acq.field.node_id)) {
                // fprintf(stderr, "Txn on k=%lu (n=%u,mb=%u) failed b/c of (n=%u,mb=%u)\n", tuple.id, txn_id.field.node_id, txn_id.field.mini_batch_id, last_acq.field.node_id, last_acq.field.mini_batch_id);
				return false;
			}
		}
		return true;
	}

    ErrorCode local_lock(const AccessMode mode, timestamp_t, Future_t* future) {
        if (!is_compatible(mode)) { // early abort test
            switch (mode) {
                case AccessMode::READ:
                    return ErrorCode::READ_LOCK_FAILED;
                case AccessMode::WRITE:
                    return ErrorCode::WRITE_LOCK_FAILED;
                default:
                    return ErrorCode::INVALID_ACCESS_MODE;
            }
        }

        const std::lock_guard<lock_t> lock(mutex);

		// TODO last_acq is a bad name, maybe use a union in the future?
		TxnId txn_id = future->last_acq;
		bool allow_lock = mb_allow_lock(txn_id);
        if (!is_compatible(mode) || !allow_lock) {
            switch (mode) {
                case AccessMode::READ:
                    return ErrorCode::READ_LOCK_FAILED;
                case AccessMode::WRITE:
                    return ErrorCode::WRITE_LOCK_FAILED;
                default:
                    return ErrorCode::INVALID_ACCESS_MODE;
            }
        }

        ++owner_cnt;
        lock_type = mode;
        future->tuple.store(&tuple);
		future->last_acq = last_acq;

        return ErrorCode::SUCCESS;
    }

    void remote_lock(Communicator& comm, Communicator::Pkt_t* pkt, msg::TupleGetReq* req) {
        const std::lock_guard<lock_t> lock(mutex);

		TxnId txn_id(req->me_pack);
		bool allow_lock = mb_allow_lock(txn_id);
        if (!is_compatible(req->mode) || !allow_lock) {
            auto res = req->convert<msg::TupleGetRes>();
            res->mode = AccessMode::INVALID;
            comm.send(res->sender, pkt, comm.mh_tid); // always called from msg-handler
            return;
        }

        ++owner_cnt;
        lock_type = req->mode;

        auto res = req->convert<msg::TupleGetRes>();
        auto size = msg::TupleGetRes::size(sizeof(tuple));
        pkt->resize(size);
		res->last_acq_pack = last_acq.get_packed();
        std::memcpy(res->tuple, &tuple, sizeof(tuple));

        comm.send(res->sender, pkt, comm.mh_tid); // always called from msg-handler
    }

    void remote_unlock(msg::TuplePutReq* req, Communicator& comm) {
        if (req->mode == AccessMode::WRITE) {
            std::memcpy(&tuple, req->tuple, sizeof(tuple));
        }
        auto rc = local_unlock(req->mode, req->ts, comm, TxnId(req->last_acq_pack));
        (void)rc;
    }

    ErrorCode local_unlock(const AccessMode mode, const timestamp_t, Communicator&, TxnId id) {
        const std::lock_guard<lock_t> lock(mutex);
        // lock_t::scoped_lock lock;
        // lock.acquire(mutex);

		// fprintf(stderr, "id: (%u,%u,%u)\n", id.field.valid, id.field.node_id, id.field.mini_batch_id);
		last_acq = id;
        if (lock_type != mode) [[unlikely]] {
            std::cout << "lock_type=" << static_cast<uint8_t>(lock_type) << " mode=" << static_cast<uint8_t>(mode) << '\n';
            return ErrorCode::INVALID_ACCESS_MODE;
        }

        if (--owner_cnt > 0) {
            return ErrorCode::SUCCESS;
        }
        // owner_cnt == 0 -> no active locks

        lock_type = AccessMode::INVALID;
        return ErrorCode::SUCCESS;
    }


    bool is_compatible(AccessMode mode) {
        if (lock_type == AccessMode::INVALID) {
            return true;
        }
        if (lock_type == AccessMode::WRITE || mode == AccessMode::WRITE) {
            return false;
        }
        return true; // shared
    };

    bool check() {
        return (lock_type == AccessMode::INVALID);
    }
};
