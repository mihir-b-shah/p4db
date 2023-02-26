
#include "sim.h"

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
	reg_instr_t reg_instrs[1+N_MAX_HOT_OPS];
};

void txn_to_sw_fmt(const sw_txn_t& txn, uint8_t* buf, size_t txn_id) {
	printf("=================\n");
	packet_t* pkt = reinterpret_cast<packet_t*>(buf);	
	printf("locks_check: %lx, locks_wanted: %lx\n", txn.locks_check.to_ulong(), txn.locks_wanted.to_ulong());

	memset(buf, 0, sizeof(packet_t));

	pkt->locks_check__nb = htonl(txn.locks_check.to_ulong());
	pkt->locks_acquire__nb = htonl(txn.locks_wanted.to_ulong());
	pkt->locks_undo__nb = 0;
	pkt->is_second_pass__nb = 0;
	pkt->n_failed__nb = 0;

	size_t reg_instr_p = 0;
	for (size_t p = 0; p<txn.passes.size(); ++p) {
		auto& grid = txn.passes[p].grid;
		for (size_t s = 0; s<N_STAGES; ++s) {
			for (size_t r = 0; r<REGS_PER_STAGE; ++r) {
				if (grid[s][r].has_value()) {
					size_t idx = grid[s][r].value();
					reg_instr_t* instr = &pkt->reg_instrs[reg_instr_p++];
					// important, since the high bit has already been set.
					instr->type__be = (instr->type__be & STOP) | ((s*REGS_PER_STAGE+r) + 1);
					instr->op__be = 1;
					instr->idx__be = htons(static_cast<uint16_t>(idx));
					instr->data__be = htonl(static_cast<uint32_t>(txn_id));
				}
			}
		}
		// safe since a single byte.
		pkt->reg_instrs[reg_instr_p].type__be = STOP;
	}

	for (size_t i = 0; i<reg_instr_p+1; ++i) {
		reg_instr_t* instr = &pkt->reg_instrs[i];
		printf("instr->type: %x, instr->op: %x, idx: %u, id: %u\n", instr->type__be, instr->op__be, ntohs(instr->idx__be), ntohl(instr->data__be));
	}
	printf("=================\n");
}

int main() {
    batch_iter_t iter = get_batch_iter(workload_e::YCSB_99_16);
	std::vector<txn_t> all_txns;
    std::vector<txn_t> batch;
	while ((batch = iter.next_batch()).size() > 0) {
		for (const txn_t& txn : batch) {
			all_txns.push_back(txn);
		}
	}

	FILE* f = fopen("artifacts/packet_trace", "w");

    layout_t layout(all_txns);
	std::vector<sw_txn_t> sw_txns = prepare_txns_sw(0, all_txns, layout);
	// for alignment
	uint64_t pkt_buf[(sizeof(packet_t)+sizeof(uint64_t)-1)/sizeof(uint64_t)];
	uint8_t* buf = reinterpret_cast<uint8_t*>(&pkt_buf[0]);
	printf("# sw_txns: %lu\n", sw_txns.size());
	for (size_t i = 0; i<sw_txns.size(); ++i) {
		const sw_txn_t& sw_txn = sw_txns[i];
		txn_to_sw_fmt(sw_txn, buf, i);
		assert(fwrite(buf, sizeof(uint8_t), sizeof(packet_t), f) == sizeof(packet_t));
	}

	fflush(f);
	fclose(f);
    return 0;
}
