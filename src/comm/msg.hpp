#pragma once

#include "ee/types.hpp"
#include "utils/ts_factory.hpp"
#include "utils/util.hpp"

#include <cstdint>


namespace msg {

struct node_t {
    uint32_t value; // thread << 24 | node

    node_t() = default;

    constexpr node_t(uint32_t nid)
        : value(nid) {}

    constexpr node_t(uint32_t nid, uint32_t tid)
        : value(tid << 8 | (nid & 0xff)) {}

    uint32_t get_tid() const {
        return value >> 8;
    }

    operator uint32_t() const {
        return value & 0x000000ff;
    }
};

typedef uint64_t id_t;

enum class Type : uint32_t {
    INIT = 0x00010001,
    BARRIER = 0x00010002,

    TUPLE_GET_REQ = 0x00000001,
    TUPLE_GET_RES = 0x00000002,
    TUPLE_PUT_REQ = 0x00000003,
    TUPLE_PUT_RES = 0x00000004,

    SWITCH_TXN = 0x00000005,
};

struct Header {
    Type type;
    node_t sender;
    id_t msg_id; // match msg future with reply

    Header(Type type) : type(type) {}

    template <typename T>
    auto as() {
        return reinterpret_cast<T*>(this);
    }

    template <typename T>
    auto convert() {
        type = T::MSG_TYPE;
        return reinterpret_cast<T*>(this);
    }
};

template <typename T, Type TYPE>
struct Base : crtp<T>, public Header {
    Base() : Header{TYPE} {}

    static constexpr Type MSG_TYPE = TYPE;

    constexpr size_t size() {
        return sizeof(this->underlying());
    }
};


struct Init : public Base<Init, Type::INIT> {};

struct Barrier : public Base<Barrier, Type::BARRIER> {
	bool is_hard;
	uint32_t mini_batch_num;

	Barrier() : is_hard(false), mini_batch_num(0) {}
	Barrier(uint32_t num) : is_hard(true), mini_batch_num(num) {
		assert(mini_batch_num < (1ULL << TxnId::MINI_BATCH_ID_WIDTH));
	}
};

// used by all 4 tuple interaction messages
struct TupleMsgHeader {
    timestamp_t ts;
    p4db::table_t tid;
    db_key_t rid;
    AccessMode mode;
};

struct TupleGetReq : public Base<TupleGetReq, Type::TUPLE_GET_REQ>, public TupleMsgHeader {
	uint32_t me_pack;

    TupleGetReq(timestamp_t ts, p4db::table_t tid, db_key_t rid, AccessMode mode, TxnId me)
        : TupleMsgHeader{ts, tid, rid, mode}, me_pack(me.get_packed()) {}
};

struct TupleGetRes : public Base<TupleGetRes, Type::TUPLE_GET_RES>, public TupleMsgHeader {
	uint32_t last_acq_pack;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    uint8_t tuple[0] __attribute__((aligned (sizeof(void*))));
#pragma GCC diagnostic pop

    TupleGetRes(timestamp_t ts, p4db::table_t tid, db_key_t rid, AccessMode mode, TxnId last_acq)
        : TupleMsgHeader{ts, tid, rid, mode}, // mode==INVALID if e.g. locking failed
		  last_acq_pack(last_acq.get_packed()) {}

    static constexpr auto size(size_t tuple_size) {
        return sizeof(TupleGetRes) + tuple_size;
    }
};

struct TuplePutReq : public Base<TuplePutReq, Type::TUPLE_PUT_REQ>, public TupleMsgHeader {
	uint32_t last_acq_pack;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    uint8_t tuple[0] __attribute__((aligned (sizeof(void*))));
#pragma GCC diagnostic pop

    TuplePutReq(timestamp_t ts, p4db::table_t tid, db_key_t rid, AccessMode mode, TxnId last_acq)
        : TupleMsgHeader{ts, tid, rid, mode}, // if INVALID then tuple invalid, but free up locks
		  last_acq_pack(last_acq.get_packed()) {}

    static constexpr auto size(size_t tuple_size) {
        return sizeof(TuplePutReq) + tuple_size;
    }
};


struct TuplePutRes : public Base<TuplePutRes, Type::TUPLE_PUT_RES>, public TupleMsgHeader {
    TuplePutRes(timestamp_t ts, p4db::table_t tid, db_key_t rid, AccessMode mode)
        : TupleMsgHeader{ts, tid, rid, mode} {}
};


struct SwitchTxn : public Base<SwitchTxn, Type::SWITCH_TXN> {
    SwitchTxn() = default;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    uint8_t data[0] __attribute__((aligned (sizeof(void*))));
#pragma GCC diagnostic pop

    static constexpr auto size(size_t data_size) {
        return sizeof(SwitchTxn) + data_size;
    }
};

} // namespace msg
