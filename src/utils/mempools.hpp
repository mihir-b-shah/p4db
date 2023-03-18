#pragma once

#include "utils/spinlock.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <sys/mman.h>
#include <utility>
#include <vector>

template <std::size_t max_size>
struct StackPool {
    size_t size = 0;
    uint8_t buffer[max_size];

    template <typename T, typename... Args>
    auto allocate(Args&&... args) {
        if ((size + sizeof(T)) > max_size) {
            throw std::runtime_error("StackPool overflow");
        }
        auto ptr = new (buffer + size) T{std::forward<Args>(args)...};
        size += sizeof(T);
        return ptr;
    }

    void* allocate(size_t want) {
        if ((size + want) > max_size) {
            throw std::runtime_error("StackPool overflow");
        }
        void* ptr = buffer+size;
        size += want;
        return ptr;
    }

    void clear() {
        size = 0;
    }
};

// assume threads do not overflow the pool size.
struct LockedStackPool {
    const size_t capacity;
    size_t size;
    uint8_t* buffer;

    LockedStackPool(size_t capacity) : capacity(capacity), size(0) {
        buffer = (uint8_t*) malloc(capacity);
    }
    ~LockedStackPool() {
        free(buffer);
    }

    void* allocate(size_t want) {
        // TODO doesn't need to be sequentially consistent.
        size_t cur_size = __atomic_fetch_add(&size, want, __ATOMIC_SEQ_CST);
        void* ret = buffer+cur_size;
        assert(cur_size + want < capacity);
        return ret;
    }

    void clear() {
        //  TODO does this need to be atomic at all?
        __atomic_store_n(&size, 0, __ATOMIC_SEQ_CST);
    }
};

constexpr auto CACHE_SIZE = 64;

template <typename T, std::size_t N = CACHE_SIZE>
struct Cache {
    size_t fill = 0;
    std::array<T*, N> cache;


    T* allocate() {
        if (fill == 0) {
            // std::cout << "cache no hit (alloc) " << this << "\n";
            return nullptr;
        }
        // std::cout << "alloc fill=" << fill << " " << this << '\n';
        return cache[--fill];
    }

    bool deallocate(T* ptr) {
        if (fill == N) {
            // std::cout << "cache no hit (dealloc) " << this << "\n";
            return false;
        }
        cache[fill++] = ptr;
        // std::cout << "dealloc fill=" << fill << " " << this << '\n';
        return true;
    }
};


template <typename T>
struct FixedThreadsafeMempool {
    using lock_t = SpinLock;
    // using lock_t = std::mutex;
    lock_t mutex;

    std::unique_ptr<T[]> data;
    std::vector<T*> free;

    inline static thread_local Cache<T> cache;


    FixedThreadsafeMempool(const size_t size) {
        data = std::make_unique<T[]>(size);
        free.reserve(size);
        for (size_t i = 0; i < size; ++i) {
            free.emplace_back(&data[i]);
        }
    }

    T* allocate() {
        // if (!cache) {
        //     cache = new Cache<T>{};
        //     const std::lock_guard<lock_t> lock(mutex);
        //     for (size_t i = 0; i < 16; ++i) {
        //         T* ptr = free.back();
        //         free.pop_back();
        //         cache->deallocate(ptr);
        //     }
        // }
        if (auto ptr = cache.allocate()) {
            return ptr;
        }
        const std::lock_guard<lock_t> lock(mutex);
        while (cache.fill < CACHE_SIZE) {
            T* ptr = free.back();
            free.pop_back();
            cache.deallocate(ptr);
        }

        T* node = free.back();
        free.pop_back();
        return node;
    }

    void deallocate(T* node) {
        // if (!cache) {
        //     cache = new Cache<T>{};
        //     for (size_t i = 0; i < 32; ++i) {
        //         T* ptr = free.back();
        //         free.pop_back();
        //         cache->deallocate(ptr);
        //     }
        // }
        if (cache.deallocate(node)) {
            return;
        }
        const std::lock_guard<lock_t> lock(mutex);
        while (cache.fill > CACHE_SIZE) {
            auto ptr = cache.allocate();
            free.emplace_back(ptr);
        }
        free.emplace_back(node);
    }
};


template <typename T>
struct FixedMempool {
    std::unique_ptr<T[]> data;
    std::vector<T*> free;

    FixedMempool(const size_t size) {
        data = std::make_unique<T[]>(size);
        free.reserve(size);
        for (size_t i = 0; i < size; ++i) {
            free.emplace_back(&data[i]);
        }
    }

    T* allocate() {
        T* node = free.back();
        free.pop_back();
        return node;
    }

    void deallocate(T* node) {
        free.emplace_back(node);
    }
};
