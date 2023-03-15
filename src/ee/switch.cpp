#include "switch.hpp"

#include <cstdio>
#include <array>
#include <cstring>
#include <cassert>
#include <cstdlib>
#include <utility>
#include <optional>
#include <netinet/in.h>

#define STOP 0x80

struct __attribute__((packed)) reg_instr_t {
	uint8_t type__be;
	uint8_t op__be;
	uint16_t idx__be;
	uint32_t data__be;
};

struct __attribute__((packed)) packet_t {
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

size_t SwitchInfo::make_txn(const Txn& txn, uint8_t* buf) {
	assert(txn.do_accel);
	packet_t* pkt = reinterpret_cast<packet_t*>(buf);	

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
	return sizeof(packet_t);
}

SwitchInfo::MultiOpOut SwitchInfo::parse_txn(BufferReader& br) {
    MultiOpOut out;
	/* TODO replace with real logic
    for (int i = 0; i < N_OPS; ++i) {
        auto instr = br.read<instr_t>();
        out.values[i] = *instr->data;
    }
	*/
    return out;
}
