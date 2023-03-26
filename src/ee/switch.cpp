#include "switch.hpp"

#include <cstdio>
#include <array>
#include <climits>
#include <cstring>
#include <cassert>
#include <cstdlib>
#include <utility>
#include <optional>
#include <netinet/in.h>

#include <ee/database.hpp>

static constexpr uint8_t STOP = 0x80;
// just a randomly generated 128-bit integer.
static constexpr uint8_t UID_HDR[] = {0xBE, 0x87, 0xEF, 0x7E, 0x61, 0x3C, 0x4B, 0x33, 0x82, 0xEB, 0x90, 0x66, 0x3A, 0x40, 0x3D, 0xAE};

static constexpr size_t N_INSTRS_PADDED = 20;
static constexpr size_t BASE_OFFSET_MASK = ~(SLOTS_PER_SCHED_BLOCK-1);

struct __attribute__((packed)) pass1_normal_instr_t {
    uint8_t is_write : 1;
    uint8_t idx : 7;
    uint32_t data__be;
};

struct __attribute__((packed)) pass1_padded_instr_t {
    uint8_t padding;
    pass1_normal_instr_t reg_instr;
};

struct __attribute__((packed)) generic_instr_t {
	uint8_t type;
	uint8_t op;
	uint16_t idx__be;
	uint32_t data__be;
};

struct __attribute__((packed)) pass1_pkt_t {
    uint8_t uid[sizeof(UID_HDR)];
    uint16_t base_offset;
    pass1_padded_instr_t padded_instrs[N_INSTRS_PADDED];
    pass1_normal_instr_t normal_instrs[N_REGS-N_INSTRS_PADDED];
};

struct __attribute__((packed)) generic_pkt_t {
    uint8_t uid[sizeof(UID_HDR)];
	uint32_t locks_check__nb;
	uint32_t locks_acquire__nb;
	uint32_t locks_undo__nb;
	uint8_t is_second_pass;
	uint8_t n_failed;
    generic_instr_t instrs[1+MAX_HOT_OPS];
};

static void fill_reg_instr(const std::pair<Txn::OP, TupleLocation>& pr, generic_instr_t* instr) {
    //  TODO wrong! needs to be offset by 1.
    // fprintf(stderr, "type: %lu | ", 1+pr.second.reg_array_id);
	instr->type = 1+pr.second.reg_array_id;
	instr->op = static_cast<uint8_t>(pr.first.mode);
    //  TODO why is this always 6?? wtf? prob a bug with the virtual block translation scheme.
	instr->idx__be = htons(pr.second.reg_array_idx);
	instr->data__be = htonl(pr.first.value);
}

void SwitchInfo::make_txn(const Txn& txn, void* comm_pkt) {
	assert(txn.do_accel == true);
    // fprintf(stderr, "loader_id: %lu |", txn.loader_id);

    // TODO is comm_pkt big enough?
    if (USE_1PASS_PKTS) {
        static_assert(!USE_1PASS_PKTS || sizeof(pass1_pkt_t) == HOT_TXN_BYTES);
	    pass1_pkt_t* pkt = reinterpret_cast<pass1_pkt_t*>(comm_pkt);
        memcpy(&pkt->uid[0], UID_HDR, sizeof(UID_HDR));

        /*  TODO If we allow different block idx's, we need to check indices before indexing
            into p4 register (since the supplied index might be wrong), and thus untrusting
            DB tenants might read each other's stuff. */
        size_t base_offset = declustered_layout->block_num * SLOTS_PER_SCHED_BLOCK;
        assert(base_offset < UINT16_MAX && ((base_offset & BASE_OFFSET_MASK) == base_offset));
        pkt->base_offset = htons(base_offset);

        for (size_t i = 0; i<N_INSTRS_PADDED; ++i) {
            pkt->padded_instrs[i].reg_instr.is_write = 0;
        }
        for (size_t i = N_INSTRS_PADDED; i<N_REGS; ++i) {
            pkt->normal_instrs[i].is_write = 0;
        }

        for (size_t p = 0; p<N_OPS && txn.hot_ops_pass1[p].first.mode != AccessMode::INVALID; ++p) {
            const std::pair<Txn::OP, TupleLocation>& pr = txn.hot_ops_pass1[p];
            pass1_normal_instr_t* instr;
            if (pr.second.reg_array_id < N_INSTRS_PADDED) {
                instr = &pkt->padded_instrs[pr.second.reg_array_id].reg_instr;
            } else {
                instr = &pkt->normal_instrs[pr.second.reg_array_id];
            }
            instr->is_write = pr.first.mode == AccessMode::WRITE ? 1 : 0;
            
            /*  TODO kind of pointless, since we are getting back the physical offset this way, after
                we computed the virtual offset. */
            size_t idx = pr.second.reg_array_idx - base_offset; 
            assert((idx & ~BASE_OFFSET_MASK) == idx);
            
            instr->idx = idx;
            instr->data__be = htonl(pr.first.value);
        }
    } else {
        static_assert(USE_1PASS_PKTS || sizeof(generic_pkt_t) == HOT_TXN_BYTES);
        generic_pkt_t* pkt = reinterpret_cast<generic_pkt_t*>(comm_pkt);
        memcpy(&pkt->uid[0], UID_HDR, sizeof(UID_HDR));

        size_t p;
        for (p = 0; p<N_OPS && txn.hot_ops_pass1[p].first.mode != AccessMode::INVALID; ++p) {
            fill_reg_instr(txn.hot_ops_pass1[p], &pkt->instrs[p]);
        }
        if (MAX_PASSES_ACCEL > 1) {
            size_t off1 = p;
            for (p = 0; p<MAX_OPS_PASS2_ACCEL && 
                txn.hot_ops_pass2[p].first.mode != AccessMode::INVALID; ++p) {
                fill_reg_instr(txn.hot_ops_pass2[p], &pkt->instrs[off1 + p]);
            }
            pkt->instrs[off1].type |= STOP;
            pkt->instrs[off1 + p].type = STOP;
        } else {
            pkt->instrs[p].type = STOP;
        }
        
        pkt->locks_check__nb = htonl(txn.locks_check.to_ulong());
        pkt->locks_acquire__nb = htonl(txn.locks_acquire.to_ulong());
        pkt->locks_undo__nb = 0;
        pkt->is_second_pass = 0;
        pkt->n_failed = 0;
    }
}

void SwitchInfo::process_reply_txn(const Txn* txn, void* in_pkt_raw, bool write) {
    if (USE_1PASS_PKTS) {
	    pass1_pkt_t* pkt = reinterpret_cast<pass1_pkt_t*>(in_pkt_raw);
        for (size_t p = 0; p<N_OPS && txn->hot_ops_pass1[p].first.mode != AccessMode::INVALID; ++p) {
            const std::pair<Txn::OP, TupleLocation>& pr = txn->hot_ops_pass1[p];
            pass1_normal_instr_t* instr;
            if (pr.second.reg_array_id < N_INSTRS_PADDED) {
                instr = &pkt->padded_instrs[pr.second.reg_array_id].reg_instr;
            } else {
                instr = &pkt->normal_instrs[pr.second.reg_array_id];
            }
            if (write) {
                table->lockless_access(pr.first.id).value = ntohl(instr->data__be);
            }
        }
    } else {
        generic_pkt_t* pkt = reinterpret_cast<generic_pkt_t*>(in_pkt_raw);

        size_t p;
        for (p = 0; p<N_OPS && txn->hot_ops_pass1[p].first.mode != AccessMode::INVALID; ++p) {
            const std::pair<Txn::OP, TupleLocation>& pr = txn->hot_ops_pass1[p];
            generic_instr_t* instr = &pkt->instrs[p];
            if (write) {
                table->lockless_access(pr.first.id).value = ntohl(instr->data__be);
            }
        }
        if (MAX_PASSES_ACCEL > 1) {
            size_t off1 = p;
            for (p = 0; p<MAX_OPS_PASS2_ACCEL && 
                txn->hot_ops_pass2[p].first.mode != AccessMode::INVALID; ++p) {

                const std::pair<Txn::OP, TupleLocation>& pr = txn->hot_ops_pass2[off1+p];
                generic_instr_t* instr = &pkt->instrs[off1+p];
                if (write) {
                    table->lockless_access(pr.first.id).value = ntohl(instr->data__be);
                }
            }
        }
    }
}
