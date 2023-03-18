
#pragma once

#include <cstdlib>

#include <ee/errors.hpp>
#include <utils/hex_dump.hpp>

struct PacketBuffer {
    static constexpr size_t BUF_SIZE = 1500;

    uint8_t buffer[BUF_SIZE]; // size stored at end
    int len = 0;

    static auto alloc() {
        void* data = std::malloc(BUF_SIZE + sizeof(int)); // MTU for now
        return static_cast<PacketBuffer*>(data);
    }

    template <typename T, typename... Args>
    auto ctor(Args&&... args) {
        len = sizeof(T);
        return new (this) T{std::forward<Args>(args)...};
    }

    template <typename T>
    auto as() {
        return reinterpret_cast<T*>(this);
    }

    void resize(const std::size_t len) {
        if (len > BUF_SIZE) {
            throw error::PacketBufferTooSmall();
        }
        this->len = len;
    }

    auto size() {
        return len;
    }

    operator uint8_t*() {
        return reinterpret_cast<uint8_t*>(this);
    }

    void free() {
        std::free(this);
    }

    void dump(std::ostream& os) {
        auto bytes = as<uint8_t>();
        hex_dump(os, bytes, size());
    }
};
