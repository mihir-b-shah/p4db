#pragma once


#include "comm/comm.hpp"
#include "comm/msg.hpp"
#include "ee/errors.hpp"
#include "ee/args.hpp"

#include <atomic>
#include <limits>
#include <cstdio>


struct AbstractFuture {
    std::atomic<Communicator::Pkt_t*> pkt{nullptr}; // msg::TupleGetRes

    void set_pkt(Communicator::Pkt_t* pkt) {
        this->pkt.store(pkt, std::memory_order_release);
    }

    auto get_pkt() {
        Communicator::Pkt_t* pkt;
        // Wait for pkt without generating cache misses
        while (!(pkt = this->pkt.load(std::memory_order_relaxed))) {
            __builtin_ia32_pause();
        }
        return pkt;
    }
};


template <typename Tuple_t>
struct TupleFuture final : public AbstractFuture {
    static inline Tuple_t* EXCEPTION = reinterpret_cast<Tuple_t*>(0xffffffff'ffffffff);

    // Tuple_t* tuple;
	// TODO: don't understand why this needs to be atomic.
    std::atomic<Tuple_t*> tuple{nullptr};
    // char __cache_align[64-16];
	TxnId last_acq;

    TupleFuture() : AbstractFuture{}, tuple(nullptr) {}
    TupleFuture(Tuple_t* tuple) : AbstractFuture{}, tuple(tuple) {}

    // not threadsafe
    Tuple_t* get() {
        if (tuple == EXCEPTION) [[unlikely]] { // fast path
            return nullptr;
        } else if (tuple) [[likely]] {
            return tuple;
        }

        return wait();
    }

private:
    Tuple_t* wait() {
        while (true) {
            if (auto pkt = this->pkt.load(std::memory_order_relaxed)) [[likely]] {
                auto res = pkt->as<msg::TupleGetRes>();
                if (res->mode == AccessMode::INVALID) [[unlikely]] {
                    tuple = EXCEPTION;
                    pkt->free();
                    return nullptr;
                }
                tuple = reinterpret_cast<Tuple_t*>(res->tuple);
				last_acq = TxnId(res->last_acq_pack);
                return tuple;
            }

            if (tuple == EXCEPTION) [[unlikely]] {
                return nullptr;
            } else if (tuple) [[likely]] {
                return tuple;
            }

            __builtin_ia32_pause();
        }
    }
};

template <typename P4Switch>
struct SwitchFuture final : public AbstractFuture {
	P4Switch& p4_switch;
	const Txn& arg;
    void* orig_pkt;

    SwitchFuture(P4Switch& p4_switch, const Txn& arg, void* orig_pkt)
        : AbstractFuture{}, p4_switch(p4_switch), arg(arg), orig_pkt(orig_pkt) {}

    const auto get() { // can be only called once
        auto pkt = get_pkt();
		auto txn = pkt->as<msg::SwitchTxn>();
		auto ret = p4_switch.parse_txn(orig_pkt, txn->data);
        pkt->free();
        return ret;
    }
};
