#include "switch.hpp"

void SwitchInfo::make_txn(const MultiOp& arg, BufferWriter& bw) {
    struct UniqInstrType_t {
        uint32_t id = 0;
        TupleLocation tl;
        Txn::OP op; // might save some bytes if we only copy necessary fields

        bool operator<(const UniqInstrType_t& other) {
            if (id == other.id) {
                return tl.stage_id < other.tl.stage_id;
            }
            return id < other.id;
        }
    };

    std::array<UniqInstrType_t, DeclusteredLayout::NUM_INSTRS> accesses;
    std::array<uint32_t, DeclusteredLayout::NUM_REGS> cntr{};
    for (size_t i = 0; auto& op : arg.ops.ops) {
        auto tl = declustered_layout.get_location(op.id);
        uint32_t id = cntr[tl.stage_id]++;
        accesses[i++] = UniqInstrType_t{id, tl, op};
    }
    std::sort(accesses.begin(), accesses.end());

    auto info = bw.write(info_t{});

    uint32_t nb_conflict = 0;
    lock_t locks{0, 0};
    for (int8_t last = -1; auto& access : accesses) {
        auto& tl = access.tl;
        auto& op = access.op;
        bool is_conflict = tl.stage_id <= last;
        if (tl.lock_bit == 0) { // left lock needed
            locks.left |= 1;
        } else {
            locks.right |= 1;
        }
        auto reg = InstrType_t::REG(tl.stage_id).set_stop(is_conflict);
        auto opcode = (op.mode == AccessMode::WRITE) ? OPCode_t::WRITE : OPCode_t::READ;
        bw.write(instr_t{reg, opcode, tl.reg_array_idx, op.value});
        nb_conflict += is_conflict;
        last = tl.stage_id;
    }
    bw.write(InstrType_t::STOP());

    info->multipass = (nb_conflict > 0);
    info->locks = (nb_conflict > 0) ? locks : lock_t{0, 0};

    // {
    //     std::stringstream ss;
    //     BufferReader br{bw.buffer};
    //     auto info = br.template read<info_t>();
    //     ss << *info << '\n';
    //     for (auto i = 0; i < 8; ++i) {
    //         auto instr = br.template read<instr_t>();
    //         ss << *instr << '\n';
    //     }
    //     std::cout << ss.str();
    // }
}

SwitchInfo::MultiOpOut SwitchInfo::parse_txn(const MultiOp& arg [[maybe_unused]], BufferReader& br) {
    MultiOpOut out;
    for (int i = 0; i < NUM_OPS; ++i) {
        auto instr = br.read<instr_t>();
        out.values[i] = *instr->data;
    }
    return out;
}
