#pragma once

#include "comm/comm.hpp"
#include "comm/msg.hpp"
#include "row.hpp"
#include "ee/errors.hpp"
#include "utils/mempools.hpp"
#include "utils/util.hpp"
#include "partition.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <variant>

struct Table {
    Table() = default;
    Table(Table&&) = default;
    Table(const Table&) = delete;
    virtual ~Table() = default;

    p4db::table_t id;
    std::string name;

    // returns bytes written by tuple
    virtual size_t tuple_size() = 0;
    virtual void remote_get(Communicator::Pkt_t* pkt, msg::TupleGetReq* req) = 0;
    virtual void remote_put(msg::TuplePutReq* req) = 0;

    virtual void print(){};
};


struct Serializer {
    std::fstream file;

    Serializer(const std::string filename)
        : file(filename + ".data", std::fstream::out | std::fstream::binary) {}

    template <typename T>
    void write(const T& val) {
        static_assert(std::is_trivially_copyable<T>::value, "T must be a POD type.");
        file.write(reinterpret_cast<const char*>(&val), sizeof(val));
    }

    template <typename T>
    void write(const std::unique_ptr<T>& val, const size_t size) {
        static_assert(std::is_trivially_copyable<T>::value, "T must be a POD type.");
        file.write(reinterpret_cast<const char*>(val.get()), sizeof(*val.get()) * size); // sizeof(T) Error: incomplete type
    }
};


struct DeSerializer {
    std::fstream file;

    DeSerializer(const std::string filename)
        : file(filename + ".data", std::fstream::in | std::fstream::binary) {}

    template <typename T>
    auto read() {
        static_assert(std::is_trivially_copyable<T>::value, "T must be a POD type.");
        T val{};
        file.read(reinterpret_cast<char*>(&val), sizeof(val));
        return val;
    }

    template <typename T>
    void read(const std::unique_ptr<T>& val, const size_t size) {
        static_assert(std::is_trivially_copyable<T>::value, "T must be a POD type.");
        file.read(reinterpret_cast<char*>(val.get()), sizeof(*val.get()) * size);
    }
};

struct KV {
	static constexpr auto TABLE_NAME = "kvs";
	uint64_t id;
	uint32_t value;

	static constexpr db_key_t pk(uint64_t id) {
		return db_key_t{id};
	}

	void print() {
		if (value != 0) {
			std::cout << "id=" << id << " value=" << value << '\n';
		}
	}
};


struct StructTable final : public Table {
    using Row_t = Row<KV>;
    using Future_t = TupleFuture<KV>;

    std::atomic<uint64_t> size{0};
    const size_t max_size;

    PartitionInfo part_info;
    Communicator& comm;
    std::unique_ptr<Row_t[]> data;

    StructTable(std::size_t max_size, Communicator& comm)
        : max_size(max_size), part_info(max_size), comm(comm) {
        std::cout << "size: " << stringifyFileSize(sizeof(Row_t) * max_size) << '\n';
        data = std::make_unique<Row_t[]>(max_size);
        // data.allocate(max_size);
    }

    ~StructTable() {
        auto& config = Config::instance();
        if (!config.verify) {
            return;
        }
        for (size_t i = 0; i < max_size; ++i) {
            if (!data[i].check()) {
                std::stringstream ss;
                ss << "table: " << name << " row[" << i << "]: check failed\n";
                std::cerr << ss.str();
            }
        }
    }


    void write_dump() {
        std::cout << "write_dump() table: " << name << '\n';
        Serializer s{name};
        s.write(max_size);
        s.write(size.load());
        s.write(data, size);
    }

    bool read_dump() {
        std::cout << "read_dump() table: " << name << '\n';
        DeSerializer s{name};
        if (!s.file.good()) {
            std::cout << "file not good: " << std::strerror(errno) << '\n';
            return false;
        }
        if (s.read<uint64_t>() != max_size) {
            std::cout << "table max_size missmatch\n";
            return false;
        }
        size = s.read<size_t>();
        s.read(data, size);
        return true;
    }

    KV& lockless_access(const db_key_t index) {
        auto local_index = part_info.translate(index);
        if (local_index >= size) {
            assert(false && "Out of bounds access.");
        }
        auto& row = data[local_index];
        return row.tuple;
    }

    ErrorCode get(const db_key_t index, const AccessMode mode, Future_t* future, const timestamp_t ts) {
        auto local_index = part_info.translate(index);
        if (local_index >= size) {
            return ErrorCode::INVALID_ROW_ID;
        }

		// fprintf(stderr, "Trying to lock key %lu\n", index);
        auto& row = data[local_index];
        return row.local_lock(mode, ts, future);
    }

    ErrorCode put(db_key_t index, const AccessMode mode, const timestamp_t ts, TxnId id) {
        auto local_index = part_info.translate(index);
        if (local_index >= size) {
            return ErrorCode::INVALID_ROW_ID;
        }

        if constexpr (error::LOG_TABLE) {
            std::stringstream ss;
            ss << "local_put to " << name << " index=" << index << " local_index=" << local_index << " mode=" << static_cast<uint8_t>(mode) << '\n';
            std::cout << ss.str();
        }

        auto& row = data[local_index];
        return row.local_unlock(mode, ts, comm, id);
    }


    auto& insert(db_key_t& index) {
        if (size >= max_size) {
            throw error::TableFull();
        }
        index = size++;
        return data[index].tuple;
    }


    virtual void remote_get(Communicator::Pkt_t* pkt, msg::TupleGetReq* req) override {
        auto local_index = part_info.translate(req->rid);
        auto& row = data[local_index];
		// fprintf(stderr, "Trying to lock key %lu\n", req->rid);
        row.remote_lock(comm, pkt, req);
    }

    virtual void remote_put(msg::TuplePutReq* req) override {
        auto local_index = part_info.translate(req->rid);

        if constexpr (error::LOG_TABLE) {
            std::stringstream ss;
            ss << "remote_put to " << name << " index=" << req->rid << " local_index=" << local_index << " mode=" << static_cast<uint8_t>(req->mode) << '\n';
            std::cout << ss.str();
        }

        auto& row = data[local_index];
        row.remote_unlock(req, comm);
    }

    virtual size_t tuple_size() override {
        return sizeof(KV);
    }
};
