
#ifndef _MEMPOOL_H_
#define _MEMPOOL_H_

#include <forward_list>
#include <cassert>

template <typename T, size_t N>
class sw_mempool_t {
public:
    typedef size_t slot_id_t;

    sw_mempool_t() {
        for (ssize_t i = N-1; i>=0; --i) {
            free_list_.push_front(static_cast<slot_id_t>(i));
        }
    }

    slot_id_t alloc() {
        assert(!free_list_.empty());
        slot_id_t slot = free_list_.front();
        free_list_.pop_front();
        return slot;
    }

    void free(slot_id_t slot) {
        free_list_.push_front(slot);
    }

    T& at(slot_id_t slot) {
        return pool_[slot];
    }

private:
    T pool_[N];
    std::forward_list<slot_id_t> free_list_;
};

#endif
