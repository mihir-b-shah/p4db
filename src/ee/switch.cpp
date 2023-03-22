#include "switch.hpp"

#include <cstdio>
#include <array>
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

struct __attribute__((packed)) reg_instr_t {
	uint8_t type__be;
	uint8_t op__be;
	uint16_t idx__be;
	uint32_t data__be;
};

struct __attribute__((packed)) packet_t {
    uint8_t uid[sizeof(UID_HDR)];
	uint32_t locks_check__nb;
	uint32_t locks_acquire__nb;
	uint32_t locks_undo__nb;
	uint8_t is_second_pass__nb;
	uint8_t n_failed__nb;
	// TODO doesn't mesh well with N_OPS
	reg_instr_t reg_instrs[1+DeclusteredLayout::NUM_MAX_OPS];
};

static void fill_reg_instr(const std::pair<Txn::OP, TupleLocation>& pr, reg_instr_t* instr) {
	instr->type__be = pr.second.reg_array_id;
	instr->op__be = static_cast<uint8_t>(pr.first.mode);
	instr->idx__be = htons(pr.second.reg_array_idx);
	instr->data__be = htonl(pr.first.value);
}

void SwitchInfo::make_txn(const Txn& txn, void* comm_pkt) {
    static_assert(sizeof(packet_t) == HOT_TXN_BYTES);
	assert(txn.do_accel == true);

	packet_t* pkt = reinterpret_cast<packet_t*>(comm_pkt);	

	size_t hot1_p = 0, hot2_p = 0;
	while (hot1_p < N_OPS && txn.hot_ops_pass1[hot1_p].first.mode == AccessMode::INVALID) {
		fill_reg_instr(txn.hot_ops_pass1[hot1_p], &pkt->reg_instrs[hot1_p]);
		hot1_p += 1;
	}
	if (MAX_PASSES_ACCEL > 1) {
		while (hot2_p < MAX_OPS_PASS2_ACCEL && 
				txn.hot_ops_pass2[hot2_p].first.mode == AccessMode::INVALID) {
			fill_reg_instr(txn.hot_ops_pass1[hot2_p], &pkt->reg_instrs[hot2_p]);
			hot2_p += 1;
		}
		pkt->reg_instrs[hot2_p].type__be |= STOP;
	}
	pkt->reg_instrs[hot1_p].type__be |= STOP;
	
	pkt->locks_check__nb = htonl(txn.locks_check.to_ulong());
	pkt->locks_acquire__nb = htonl(txn.locks_acquire.to_ulong());
	pkt->locks_undo__nb = 0;
	pkt->is_second_pass__nb = 0;
	pkt->n_failed__nb = 0;

    memcpy(&pkt->uid[0], UID_HDR, sizeof(UID_HDR));
}

SwitchInfo::SwResult SwitchInfo::parse_txn(void* out_pkt_raw, void* in_pkt_raw) {
    SwResult out;
	packet_t* out_pkt = reinterpret_cast<packet_t*>(out_pkt_raw);
	packet_t* in_pkt = reinterpret_cast<packet_t*>(in_pkt_raw);

    size_t i;
    for (i = 0; i<DeclusteredLayout::NUM_MAX_OPS; ++i) {
        reg_instr_t* out_instr = &out_pkt->reg_instrs[i];
        reg_instr_t* in_instr = &in_pkt->reg_instrs[i];
        
        uint8_t reg_id = out_instr->type__be;
        if (reg_id == STOP) {
            break;
        } else if (reg_id & STOP) {
            reg_id &= ~STOP;
        }
        // both are in network byte order.
        assert(out_instr->idx__be == in_instr->idx__be);
        uint16_t reg_idx = ntohs(in_instr->idx__be);
        uint32_t read_val = ntohl(in_instr->data__be);
        
        db_key_t k = declustered_layout->rev_loc_lookup(reg_id, reg_idx);
        out.results[i] = {k, read_val};

    }
    out.n_results = i;
    return out;
}
