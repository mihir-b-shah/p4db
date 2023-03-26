
N_REGS = 72

HEADER = '''
#include <core.p4>
#include <t2na.p4>

typedef bit<48> mac_addr_t;
typedef bit<16> ether_type_t;

header network_t {
	mac_addr_t dst_addr;
    mac_addr_t src_addr;
    ether_type_t ether_type;
	bit<224> ipv4_udp_fields;
}

header uid_t {
	bit<32> v;
}

header reg_instr_map_t {
	bit<16> offset;
'''

INSTR_DECLS = ''
INSTR_DECL_TEMPL='''
	bit<8> i%d;
	bit<32> d%d;
'''
	
for i in range(N_REGS):
	INSTR_DECLS += INSTR_DECL_TEMPL % (i,i)

NEXT_FIXED='''}

struct header_t {
    network_t network;
	uid_t uid_1;
	uid_t uid_2;
	uid_t uid_3;
	uid_t uid_4;
	reg_instr_map_t instrs;
}

struct empty_header_t {}
struct empty_metadata_t {}
struct metadata_t {}

parser TofinoIngressParser(
	packet_in pkt,
	out ingress_intrinsic_metadata_t ig_intr_md) {
	state start {
		pkt.extract(ig_intr_md);
		transition select(ig_intr_md.resubmit_flag) {
			1 : parse_resubmit;
			0 : parse_port_metadata;
		}
	}

	state parse_resubmit {
		// Parse resubmitted packet here.
		transition reject;
	}
						
	state parse_port_metadata {
		pkt.advance(PORT_METADATA_SIZE);
		transition accept;
	}
}

// ---------------------------------------------------------------------------
// Ingress parser
// ---------------------------------------------------------------------------
parser SwitchIngressParser(
	packet_in pkt,
	out header_t hdr,
	out metadata_t meta,
	out ingress_intrinsic_metadata_t ig_intr_md) {

    TofinoIngressParser() tofino_parser;

	state start {
        tofino_parser.apply(pkt, ig_intr_md);
		transition parse_network;
	}

	state parse_network {
		pkt.extract(hdr.network);
		transition parse_uid_1;
	}

	state parse_uid_1 {
		pkt.extract(hdr.uid_1);
		transition select (hdr.uid_1.v) {
			0xbe87ef7e: parse_uid_2;
			default: accept;
		}
	}

	state parse_uid_2 {
		pkt.extract(hdr.uid_2);
		transition select (hdr.uid_2.v) {
			0x613c4b33: parse_uid_3;
			default: accept;
		}
	}

	state parse_uid_3 {
		pkt.extract(hdr.uid_3);
		transition select (hdr.uid_3.v) {
			0x82eb9066: parse_uid_4;
			default: accept;
		}
	}

	state parse_uid_4 {
		pkt.extract(hdr.uid_4);
		transition select (hdr.uid_4.v) {
			0x3a403dae: parse_reg_instrs;
			default: accept;
		}
	}

	state parse_reg_instrs {
		pkt.extract(hdr.instrs);
		transition accept;
	}
}

// ---------------------------------------------------------------------------
// Ingress Deparser
// ---------------------------------------------------------------------------
control SwitchIngressDeparser(
	packet_out pkt,
	inout header_t hdr,
	in metadata_t meta,
	in ingress_intrinsic_metadata_for_deparser_t ig_intr_md_for_dprsr) {

	apply {
		pkt.emit(hdr);
	}
}

control SwitchIngress(
	inout header_t hdr,
	inout metadata_t meta,
	in ingress_intrinsic_metadata_t ig_intr_md,
	in ingress_intrinsic_metadata_from_parser_t ig_intr_md_from_prsr,
	inout ingress_intrinsic_metadata_for_deparser_t ig_intr_md_for_dprsr,
	inout ingress_intrinsic_metadata_for_tm_t ig_intr_md_for_tm) {
	
	/*	TODO: recirculate using a loopback port configured using bf-rt. In sim, maybe just send
		to an unused port. */
	action recirculate(PortId_t port) {
		ig_intr_md_for_tm.ucast_egress_port = ig_intr_md.ingress_port[8:7] ++ port[6:0];
	}

	action send(PortId_t port) {
		ig_intr_md_for_tm.ucast_egress_port = port;
		mac_addr_t old_mac = hdr.network.dst_addr;
		hdr.network.dst_addr = hdr.network.src_addr;
		hdr.network.src_addr = old_mac;
	}

	action drop() {
		ig_intr_md_for_dprsr.drop_ctl = 1;
	}

	table l2fwd {
		key = {
			hdr.network.dst_addr: exact;
		}
		actions = {
			send;
			@defaultonly drop();
		}
		const default_action = drop;
		size = 32;
		const entries = {
			(48w0x222222222222) : send(2);
		}
	}
'''

REG_ACCESS_TEMPL='''
    Register<bit<32>, bit<16>>(32768, 0x%02d%02d%02d%02d) reg_%d;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_%d) reg_%d_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i%d[7:7] == 1) {     
                value = hdr.instrs.d%d;
            }
        }
    };
'''
REG_ACCESSES=''
for i in range(N_REGS):
	REG_ACCESSES += REG_ACCESS_TEMPL % (i,i,i,i,i,i,i,i,i)

BLOCK_START='''
	apply {
        ig_intr_md_for_tm.bypass_egress = 1;
		bit<9> offset = (bit<9>) (hdr.instrs.offset >> 7);
'''

REG_EXEC_TEMPL='''
		hdr.instrs.d%d = reg_%d_access.execute(offset ++ hdr.instrs.i%d[6:0]);'''

REG_EXECS=''
for i in range(N_REGS):
	REG_EXECS += REG_EXEC_TEMPL % (i,i,i)

END='''
		l2fwd.apply();
	}
}

parser EmptyEgressParser(
	packet_in pkt,
	out header_t hdr,
	out metadata_t meta,
	out egress_intrinsic_metadata_t eg_intr_md) {
	state start {
		transition accept;
	}
}

control EmptyEgressDeparser(
	packet_out pkt,
	inout header_t hdr,
	in metadata_t meta,
	in egress_intrinsic_metadata_for_deparser_t eg) {
	apply {}
}

control EmptyEgress(
	inout header_t hdr,
	inout metadata_t meta,
	in egress_intrinsic_metadata_t eg_intr_md,
	in egress_intrinsic_metadata_from_parser_t eg_intr_md_from_prsr,
	inout egress_intrinsic_metadata_for_deparser_t eg_intr_md_for_dprsr,
	inout egress_intrinsic_metadata_for_output_port_t eg_intr_md_for_oport) {
	apply {}
}

Pipeline(SwitchIngressParser(),
         SwitchIngress(),
         SwitchIngressDeparser(),
         EmptyEgressParser(),
         EmptyEgress(),
         EmptyEgressDeparser()) pipe;

Switch(pipe) main;'''

print(HEADER + INSTR_DECLS + NEXT_FIXED + REG_ACCESSES + BLOCK_START + REG_EXECS + END)
