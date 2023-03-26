
#include <core.p4>
#include <t2na.p4>

/*	When a txn enters, the following can happen.
1) 1-pass txn, we are free to enter.
2) 1-pass txn, we are not free to enter.
3) 2-pass txn, we acquire the locks successfully.
4) 2-pass txn, we fail to acquire the locks.
5) 2-pass txn, we release all acquired locks (works for both successful/failed acquisition).
*/

typedef bit<48> mac_addr_t;
typedef bit<16> ether_type_t;
typedef bit<32> node_t;
typedef bit<64> msgid_t;

header ethernet_t {
    mac_addr_t dst_addr;
    mac_addr_t src_addr;
    ether_type_t ether_type;
	bit<256> ip_udp_junk;
	bit<128> p4db_compat_junk;
}

header uid_t {
	bit<32> v;
}

header lock_info_t {
	bit<32> locks_check;
	bit<32> locks_acquire;
	bit<32> locks_undo;
	bit<8> is_second_pass;
	bit<8> n_failed;
}

enum bit<8> InstrType_t {
    SKIP = 0x00,
    REG_0 = 0x01,
    REG_1 = 0x02,
    REG_2 = 0x03,
    REG_3 = 0x04,
    REG_4 = 0x05,
    REG_5 = 0x06,
    REG_6 = 0x07,
    REG_7 = 0x08,
    REG_8 = 0x09,
    REG_9 = 0x0a,
    REG_10 = 0x0b,
    REG_11 = 0x0c,
    REG_12 = 0x0d,
    REG_13 = 0x0e,
    REG_14 = 0x0f,
    REG_15 = 0x10,
    REG_16 = 0x11,
    REG_17 = 0x12,
    REG_18 = 0x13,
    REG_19 = 0x14,
	REG_20 = 0x15,
	REG_21 = 0x16,
	REG_22 = 0x17,
	REG_23 = 0x18,
	REG_24 = 0x19,
	REG_25 = 0x1a,
	REG_26 = 0x1b,
	REG_27 = 0x1c,
	REG_28 = 0x1d,
	REG_29 = 0x1e,
	REG_30 = 0x1f,
	REG_31 = 0x20,
	REG_32 = 0x21,
	REG_33 = 0x22,
	REG_34 = 0x23,
	REG_35 = 0x24,
	REG_36 = 0x25,
	REG_37 = 0x26,
	REG_38 = 0x27,
	REG_39 = 0x28,
    STOP = 0x80,
    NEG_STOP = 0x7f
}

enum bit<8> OPCode_t {
    READ = 0x00,
    WRITE = 0x01
}

header reg_instr_t {
    InstrType_t type;   // set to SKIP after processing, maybe merge with op
    OPCode_t op;
    bit<16> idx;
    bit<32> data;
}

header next_type_t {
    InstrType_t type;
}

struct header_t {
    ethernet_t ethernet;
	uid_t uid_1;
	uid_t uid_2;
	uid_t uid_3;
	uid_t uid_4;
	lock_info_t lock_info;

    reg_instr_t[7] reg_skip;       // Worst case we need to skip 7 instructions
    reg_instr_t reg_0;
    reg_instr_t reg_1;
    reg_instr_t reg_2;
    reg_instr_t reg_3;
    reg_instr_t reg_4;
    reg_instr_t reg_5;
    reg_instr_t reg_6;
    reg_instr_t reg_7;
    reg_instr_t reg_8;
    reg_instr_t reg_9;
    reg_instr_t reg_10;
    reg_instr_t reg_11;
    reg_instr_t reg_12;
    reg_instr_t reg_13;
    reg_instr_t reg_14;
    reg_instr_t reg_15;
    reg_instr_t reg_16;
    reg_instr_t reg_17;
    reg_instr_t reg_18;
    reg_instr_t reg_19;
	reg_instr_t reg_20;
	reg_instr_t reg_21;
	reg_instr_t reg_22;
	reg_instr_t reg_23;
	reg_instr_t reg_24;
	reg_instr_t reg_25;
	reg_instr_t reg_26;
	reg_instr_t reg_27;
	reg_instr_t reg_28;
	reg_instr_t reg_29;
	reg_instr_t reg_30;
	reg_instr_t reg_31;
	reg_instr_t reg_32;
	reg_instr_t reg_33;
	reg_instr_t reg_34;
	reg_instr_t reg_35;
	reg_instr_t reg_36;
	reg_instr_t reg_37;
	reg_instr_t reg_38;
	reg_instr_t reg_39;
    next_type_t next_type;
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
    ParserPriority() parser_prio;

	state start {
        tofino_parser.apply(pkt, ig_intr_md);
		transition parse_ethernet;
	}

	state parse_ethernet {
		pkt.extract(hdr.ethernet);
		transition parse_uid_1;
	}

	state parse_uid_1 {
		pkt.extract(hdr.uid_1);
		transition select (hdr.uid_1.v) {
			0x7EEF87B8: parse_uid_2;
			default: accept;
		}
	}

	state parse_uid_2 {
		pkt.extract(hdr.uid_2);
		transition select (hdr.uid_2.v) {
			0x334B3C61: parse_uid_3;
			default: accept;
		}
	}

	state parse_uid_3 {
		pkt.extract(hdr.uid_3);
		transition select (hdr.uid_3.v) {
			0x6690EB82: parse_uid_4;
			default: accept;
		}
	}

	state parse_uid_4 {
		pkt.extract(hdr.uid_4);
		transition select (hdr.uid_4.v) {
			0xAE3D403A: parse_lock_info;
			default: accept;
		}
	}

	state parse_lock_info {
		pkt.extract(hdr.lock_info);
        transition select(hdr.lock_info.locks_undo) {
            0: parse_skips;
			default: set_high_prio;
        }
	}

    state set_high_prio {
        parser_prio.set(7);
        transition parse_skips;
    }
    
    state parse_skips {
        transition select(pkt.lookahead<InstrType_t>()) {
            InstrType_t.SKIP: parse_reg_skip;
            default: parse_regs;
        }
    }

    state parse_reg_skip {
        pkt.extract(hdr.reg_skip.next);
        transition parse_skips;
    }
    
    state parse_regs {
        transition select(pkt.lookahead<InstrType_t>()) {
            InstrType_t.STOP &&& InstrType_t.STOP: parse_next_type;  // if first bit is STOP_BIT
            InstrType_t.REG_0: parse_reg_0;
            InstrType_t.REG_1: parse_reg_1;
            InstrType_t.REG_2: parse_reg_2;
            InstrType_t.REG_3: parse_reg_3;
            InstrType_t.REG_4: parse_reg_4;
            InstrType_t.REG_5: parse_reg_5;
            InstrType_t.REG_6: parse_reg_6;
            InstrType_t.REG_7: parse_reg_7;
            InstrType_t.REG_8: parse_reg_8;
            InstrType_t.REG_9: parse_reg_9;
            InstrType_t.REG_10: parse_reg_10;
            InstrType_t.REG_11: parse_reg_11;
            InstrType_t.REG_12: parse_reg_12;
            InstrType_t.REG_13: parse_reg_13;
            InstrType_t.REG_14: parse_reg_14;
            InstrType_t.REG_15: parse_reg_15;
            InstrType_t.REG_16: parse_reg_16;
            InstrType_t.REG_17: parse_reg_17;
            InstrType_t.REG_18: parse_reg_18;
            InstrType_t.REG_19: parse_reg_19;
            InstrType_t.REG_20: parse_reg_20;
            InstrType_t.REG_21: parse_reg_21;
            InstrType_t.REG_22: parse_reg_22;
            InstrType_t.REG_23: parse_reg_23;
            InstrType_t.REG_24: parse_reg_24;
            InstrType_t.REG_25: parse_reg_25;
            InstrType_t.REG_26: parse_reg_26;
            InstrType_t.REG_27: parse_reg_27;
            InstrType_t.REG_28: parse_reg_28;
            InstrType_t.REG_29: parse_reg_29;
            InstrType_t.REG_30: parse_reg_30;
            InstrType_t.REG_31: parse_reg_31;
            InstrType_t.REG_32: parse_reg_32;
            InstrType_t.REG_33: parse_reg_33;
            InstrType_t.REG_34: parse_reg_34;
            InstrType_t.REG_35: parse_reg_35;
            InstrType_t.REG_36: parse_reg_36;
            InstrType_t.REG_37: parse_reg_37;
            InstrType_t.REG_38: parse_reg_38;
            InstrType_t.REG_39: parse_reg_39;
            default: reject;
        }
    }
    
    state parse_next_type {
        pkt.extract(hdr.next_type);
        transition accept;
    }
    
    
    state parse_reg_0 {
        pkt.extract(hdr.reg_0);
        transition parse_regs;
    }
    
    state parse_reg_1 {
        pkt.extract(hdr.reg_1);
        transition parse_regs;
    }
    
    state parse_reg_2 {
        pkt.extract(hdr.reg_2);
        transition parse_regs;
    }
    
    state parse_reg_3 {
        pkt.extract(hdr.reg_3);
        transition parse_regs;
    }
    
    state parse_reg_4 {
        pkt.extract(hdr.reg_4);
        transition parse_regs;
    }
    
    state parse_reg_5 {
        pkt.extract(hdr.reg_5);
        transition parse_regs;
    }
    
    state parse_reg_6 {
        pkt.extract(hdr.reg_6);
        transition parse_regs;
    }
    
    state parse_reg_7 {
        pkt.extract(hdr.reg_7);
        transition parse_regs;
    }
    
    state parse_reg_8 {
        pkt.extract(hdr.reg_8);
        transition parse_regs;
    }
    
    state parse_reg_9 {
        pkt.extract(hdr.reg_9);
        transition parse_regs;
    }
    
    state parse_reg_10 {
        pkt.extract(hdr.reg_10);
        transition parse_regs;
    }
    
    state parse_reg_11 {
        pkt.extract(hdr.reg_11);
        transition parse_regs;
    }
    
    state parse_reg_12 {
        pkt.extract(hdr.reg_12);
        transition parse_regs;
    }
    
    state parse_reg_13 {
        pkt.extract(hdr.reg_13);
        transition parse_regs;
    }
    
    state parse_reg_14 {
        pkt.extract(hdr.reg_14);
        transition parse_regs;
    }
    
    state parse_reg_15 {
        pkt.extract(hdr.reg_15);
        transition parse_regs;
    }
    
    state parse_reg_16 {
        pkt.extract(hdr.reg_16);
        transition parse_regs;
    }
    
    state parse_reg_17 {
        pkt.extract(hdr.reg_17);
        transition parse_regs;
    }
    
    state parse_reg_18 {
        pkt.extract(hdr.reg_18);
        transition parse_regs;
    }
    
    state parse_reg_19 {
        pkt.extract(hdr.reg_19);
        transition parse_regs;
    }

    state parse_reg_20 {
        pkt.extract(hdr.reg_20);
        transition parse_regs;
    }
    
    state parse_reg_21 {
        pkt.extract(hdr.reg_21);
        transition parse_regs;
    }
    
    state parse_reg_22 {
        pkt.extract(hdr.reg_22);
        transition parse_regs;
    }
    
    state parse_reg_23 {
        pkt.extract(hdr.reg_23);
        transition parse_regs;
    }
    
    state parse_reg_24 {
        pkt.extract(hdr.reg_24);
        transition parse_regs;
    }
    
    state parse_reg_25 {
        pkt.extract(hdr.reg_25);
        transition parse_regs;
    }
    
    state parse_reg_26 {
        pkt.extract(hdr.reg_26);
        transition parse_regs;
    }
    
    state parse_reg_27 {
        pkt.extract(hdr.reg_27);
        transition parse_regs;
    }
    
    state parse_reg_28 {
        pkt.extract(hdr.reg_28);
        transition parse_regs;
    }
    
    state parse_reg_29 {
        pkt.extract(hdr.reg_29);
        transition parse_regs;
    }
    
    state parse_reg_30 {
        pkt.extract(hdr.reg_30);
        transition parse_regs;
    }
    
    state parse_reg_31 {
        pkt.extract(hdr.reg_31);
        transition parse_regs;
    }
    
    state parse_reg_32 {
        pkt.extract(hdr.reg_32);
        transition parse_regs;
    }
    
    state parse_reg_33 {
        pkt.extract(hdr.reg_33);
        transition parse_regs;
    }
    
    state parse_reg_34 {
        pkt.extract(hdr.reg_34);
        transition parse_regs;
    }
    
    state parse_reg_35 {
        pkt.extract(hdr.reg_35);
        transition parse_regs;
    }
    
    state parse_reg_36 {
        pkt.extract(hdr.reg_36);
        transition parse_regs;
    }
    
    state parse_reg_37 {
        pkt.extract(hdr.reg_37);
        transition parse_regs;
    }
    
    state parse_reg_38 {
        pkt.extract(hdr.reg_38);
        transition parse_regs;
    }
    
    state parse_reg_39 {
        pkt.extract(hdr.reg_39);
        transition parse_regs;
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
		mac_addr_t old_mac = hdr.ethernet.dst_addr;
		hdr.ethernet.dst_addr = hdr.ethernet.src_addr;
		hdr.ethernet.src_addr = old_mac;
	}

	action drop() {
		ig_intr_md_for_dprsr.drop_ctl = 1;
	}

	table l2fwd {
		key = {
			hdr.ethernet.dst_addr: exact;
		}
		actions = {
			send;
			@defaultonly drop();
		}
		const default_action = drop;
		size = 32;
		const entries = {
			(48w0x222222222222) : send(130);
		}
	}

	Register<bit<32>, bit<32>>(1, 0) my_lock;
	RegisterAction<bit<32>, bit<1>, bit<32>>(my_lock) lock_action = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.lock_info.locks_undo != 0) {
				value = value - hdr.lock_info.locks_undo;
			} else {
				value = value | hdr.lock_info.locks_acquire;
			}
		}
	};

    Register<bit<32>, bit<16>>(32768, 0x0101) reg_0;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_0) reg_0_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_0.op == OPCode_t.WRITE) {     
                value = hdr.reg_0.data;
            }
        }
    };
    Register<bit<32>, bit<16>>(32768, 0x0202) reg_1;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_1) reg_1_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_1.op == OPCode_t.WRITE) {     
                value = hdr.reg_1.data;
            }
        }
    };
    Register<bit<32>, bit<16>>(32768, 0x0303) reg_2;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_2) reg_2_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_2.op == OPCode_t.WRITE) {     
                value = hdr.reg_2.data;
            }
        }
    };
    Register<bit<32>, bit<16>>(32768, 0x0404) reg_3;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_3) reg_3_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_3.op == OPCode_t.WRITE) {     
                value = hdr.reg_3.data;
            }
        }
    };
    Register<bit<32>, bit<16>>(32768, 0x0505) reg_4;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_4) reg_4_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_4.op == OPCode_t.WRITE) {     
                value = hdr.reg_4.data;
            }
        }
    };
    Register<bit<32>, bit<16>>(32768, 0x0606) reg_5;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_5) reg_5_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_5.op == OPCode_t.WRITE) {     
                value = hdr.reg_5.data;
            }
        }
    };
    Register<bit<32>, bit<16>>(32768, 0x0707) reg_6;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_6) reg_6_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_6.op == OPCode_t.WRITE) {     
                value = hdr.reg_6.data;
            }
        }
    };
    Register<bit<32>, bit<16>>(32768, 0x0808) reg_7;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_7) reg_7_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_7.op == OPCode_t.WRITE) {     
                value = hdr.reg_7.data;
            }
        }
    };
    Register<bit<32>, bit<16>>(32768, 0x0909) reg_8;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_8) reg_8_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_8.op == OPCode_t.WRITE) {     
                value = hdr.reg_8.data;
            }
        }
    };
    Register<bit<32>, bit<16>>(32768, 0x0a0a) reg_9;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_9) reg_9_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_9.op == OPCode_t.WRITE) {     
                value = hdr.reg_9.data;
            }
        }
    };
    Register<bit<32>, bit<16>>(32768, 0x0b0b) reg_10;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_10) reg_10_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_10.op == OPCode_t.WRITE) {     
                value = hdr.reg_10.data;
            }
        }
    };
    Register<bit<32>, bit<16>>(32768, 0x0c0c) reg_11;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_11) reg_11_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_11.op == OPCode_t.WRITE) {     
                value = hdr.reg_11.data;
            }
        }
    };
    Register<bit<32>, bit<16>>(32768, 0x0d0d) reg_12;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_12) reg_12_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_12.op == OPCode_t.WRITE) {     
                value = hdr.reg_12.data;
            }
        }
    };
    Register<bit<32>, bit<16>>(32768, 0x0e0e) reg_13;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_13) reg_13_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_13.op == OPCode_t.WRITE) {     
                value = hdr.reg_13.data;
            }
        }
    };
    Register<bit<32>, bit<16>>(32768, 0x0f0f) reg_14;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_14) reg_14_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_14.op == OPCode_t.WRITE) {     
                value = hdr.reg_14.data;
            }
        }
    };
    Register<bit<32>, bit<16>>(32768, 0x1010) reg_15;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_15) reg_15_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_15.op == OPCode_t.WRITE) {     
                value = hdr.reg_15.data;
            }
        }
    };
    Register<bit<32>, bit<16>>(32768, 0x1111) reg_16;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_16) reg_16_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_16.op == OPCode_t.WRITE) {     
                value = hdr.reg_16.data;
            }
        }
    };
    Register<bit<32>, bit<16>>(32768, 0x1212) reg_17;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_17) reg_17_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_17.op == OPCode_t.WRITE) {     
                value = hdr.reg_17.data;
            }
        }
    };
    Register<bit<32>, bit<16>>(32768, 0x1313) reg_18;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_18) reg_18_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_18.op == OPCode_t.WRITE) {     
                value = hdr.reg_18.data;
            }
        }
    };
    Register<bit<32>, bit<16>>(32768, 0x1414) reg_19;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_19) reg_19_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.reg_19.op == OPCode_t.WRITE) {     
                value = hdr.reg_19.data;
            }
        }
    };
	Register<bit<32>, bit<16>>(32768, 0x1515) reg_20;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_20) reg_20_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_20.op == OPCode_t.WRITE) {
				value = hdr.reg_20.data;
			}
		}
	};
	Register<bit<32>, bit<16>>(32768, 0x1616) reg_21;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_21) reg_21_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_21.op == OPCode_t.WRITE) {
				value = hdr.reg_21.data;
			}
		}
	};
	Register<bit<32>, bit<16>>(32768, 0x1717) reg_22;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_22) reg_22_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_22.op == OPCode_t.WRITE) {
				value = hdr.reg_22.data;
			}
		}
	};
	Register<bit<32>, bit<16>>(32768, 0x1818) reg_23;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_23) reg_23_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_23.op == OPCode_t.WRITE) {
				value = hdr.reg_23.data;
			}
		}
	};
	Register<bit<32>, bit<16>>(32768, 0x1919) reg_24;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_24) reg_24_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_24.op == OPCode_t.WRITE) {
				value = hdr.reg_24.data;
			}
		}
	};
	Register<bit<32>, bit<16>>(32768, 0x1a1a) reg_25;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_25) reg_25_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_25.op == OPCode_t.WRITE) {
				value = hdr.reg_25.data;
			}
		}
	};
	Register<bit<32>, bit<16>>(32768, 0x1b1b) reg_26;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_26) reg_26_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_26.op == OPCode_t.WRITE) {
				value = hdr.reg_26.data;
			}
		}
	};
	Register<bit<32>, bit<16>>(32768, 0x1c1c) reg_27;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_27) reg_27_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_27.op == OPCode_t.WRITE) {
				value = hdr.reg_27.data;
			}
		}
	};
	Register<bit<32>, bit<16>>(32768, 0x1d1d) reg_28;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_28) reg_28_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_28.op == OPCode_t.WRITE) {
				value = hdr.reg_28.data;
			}
		}
	};
	Register<bit<32>, bit<16>>(32768, 0x1e1e) reg_29;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_29) reg_29_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_29.op == OPCode_t.WRITE) {
				value = hdr.reg_29.data;
			}
		}
	};
	Register<bit<32>, bit<16>>(32768, 0x1f1f) reg_30;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_30) reg_30_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_30.op == OPCode_t.WRITE) {
				value = hdr.reg_30.data;
			}
		}
	};
	Register<bit<32>, bit<16>>(32768, 0x2020) reg_31;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_31) reg_31_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_31.op == OPCode_t.WRITE) {
				value = hdr.reg_31.data;
			}
		}
	};
	Register<bit<32>, bit<16>>(32768, 0x2121) reg_32;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_32) reg_32_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_32.op == OPCode_t.WRITE) {
				value = hdr.reg_32.data;
			}
		}
	};
	Register<bit<32>, bit<16>>(32768, 0x2222) reg_33;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_33) reg_33_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_33.op == OPCode_t.WRITE) {
				value = hdr.reg_33.data;
			}
		}
	};
	Register<bit<32>, bit<16>>(32768, 0x2323) reg_34;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_34) reg_34_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_34.op == OPCode_t.WRITE) {
				value = hdr.reg_34.data;
			}
		}
	};
	Register<bit<32>, bit<16>>(32768, 0x2424) reg_35;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_35) reg_35_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_35.op == OPCode_t.WRITE) {
				value = hdr.reg_35.data;
			}
		}
	};
	Register<bit<32>, bit<16>>(32768, 0x2525) reg_36;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_36) reg_36_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_36.op == OPCode_t.WRITE) {
				value = hdr.reg_36.data;
			}
		}
	};
	Register<bit<32>, bit<16>>(32768, 0x2626) reg_37;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_37) reg_37_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_37.op == OPCode_t.WRITE) {
				value = hdr.reg_37.data;
			}
		}
	};
	Register<bit<32>, bit<16>>(32768, 0x2727) reg_38;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_38) reg_38_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_38.op == OPCode_t.WRITE) {
				value = hdr.reg_38.data;
			}
		}
	};
	Register<bit<32>, bit<16>>(32768, 0x2828) reg_39;
	RegisterAction<bit<32>, bit<16>, bit<32>>(reg_39) reg_39_access = {
		void apply(inout bit<32> value, out bit<32> rv) {
			rv = value;
			if (hdr.reg_39.op == OPCode_t.WRITE) {
				value = hdr.reg_39.data;
			}
		}
	};

	// note this scheme allows fallback with no overhead to whole-pipe locking, by just locking
	// all the bits, all the time!

	apply {
        ig_intr_md_for_tm.bypass_egress = 1;
		if (hdr.lock_info.isValid()) {
			bool do_access = false;
			bool do_recirc = false;

			bit<32> r = lock_action.execute(0) & hdr.lock_info.locks_check;

			if (hdr.lock_info.locks_undo != 0) {
				if (hdr.lock_info.is_second_pass == 1) {
					// it's my 2nd pass.
					do_access = true;
				} else if (hdr.lock_info.n_failed < 8) {
					// I need to recirculate, and try acquiring locks again.
					hdr.lock_info.locks_undo = 0;
					do_recirc = true;
				}
			} else {
				hdr.lock_info.locks_undo = (~r) & hdr.lock_info.locks_acquire;
				if (r == 0) {
					do_access = true;
					// from p4db: remove stop bit
                    hdr.next_type.type = (InstrType_t) (hdr.next_type.type & InstrType_t.NEG_STOP);
					if (hdr.lock_info.locks_acquire != 0) {
						// this signifies we have 2 passes.
						do_recirc = true;
						hdr.lock_info.is_second_pass = 1;
					}
				} else {
					hdr.lock_info.n_failed = hdr.lock_info.n_failed + 1;
					do_recirc = true;
				}
			}

			if (do_access) {
				if (hdr.reg_0.isValid()) {
					hdr.reg_0.data = reg_0_access.execute(hdr.reg_0.idx);
					hdr.reg_0.type = InstrType_t.SKIP;
				}
				if (hdr.reg_1.isValid()) {
					hdr.reg_1.data = reg_1_access.execute(hdr.reg_1.idx);
					hdr.reg_1.type = InstrType_t.SKIP;
				}
				if (hdr.reg_2.isValid()) {
					hdr.reg_2.data = reg_2_access.execute(hdr.reg_2.idx);
					hdr.reg_2.type = InstrType_t.SKIP;
				}
				if (hdr.reg_3.isValid()) {
					hdr.reg_3.data = reg_3_access.execute(hdr.reg_3.idx);
					hdr.reg_3.type = InstrType_t.SKIP;
				}
				if (hdr.reg_4.isValid()) {
					hdr.reg_4.data = reg_4_access.execute(hdr.reg_4.idx);
					hdr.reg_4.type = InstrType_t.SKIP;
				}
				if (hdr.reg_5.isValid()) {
					hdr.reg_5.data = reg_5_access.execute(hdr.reg_5.idx);
					hdr.reg_5.type = InstrType_t.SKIP;
				}
				if (hdr.reg_6.isValid()) {
					hdr.reg_6.data = reg_6_access.execute(hdr.reg_6.idx);
					hdr.reg_6.type = InstrType_t.SKIP;
				}
				if (hdr.reg_7.isValid()) {
					hdr.reg_7.data = reg_7_access.execute(hdr.reg_7.idx);
					hdr.reg_7.type = InstrType_t.SKIP;
				}
				if (hdr.reg_8.isValid()) {
					hdr.reg_8.data = reg_8_access.execute(hdr.reg_8.idx);
					hdr.reg_8.type = InstrType_t.SKIP;
				}
				if (hdr.reg_9.isValid()) {
					hdr.reg_9.data = reg_9_access.execute(hdr.reg_9.idx);
					hdr.reg_9.type = InstrType_t.SKIP;
				}
				if (hdr.reg_10.isValid()) {
					hdr.reg_10.data = reg_10_access.execute(hdr.reg_10.idx);
					hdr.reg_10.type = InstrType_t.SKIP;
				}
				if (hdr.reg_11.isValid()) {
					hdr.reg_11.data = reg_11_access.execute(hdr.reg_11.idx);
					hdr.reg_11.type = InstrType_t.SKIP;
				}
				if (hdr.reg_12.isValid()) {
					hdr.reg_12.data = reg_12_access.execute(hdr.reg_12.idx);
					hdr.reg_12.type = InstrType_t.SKIP;
				}
				if (hdr.reg_13.isValid()) {
					hdr.reg_13.data = reg_13_access.execute(hdr.reg_13.idx);
					hdr.reg_13.type = InstrType_t.SKIP;
				}
				if (hdr.reg_14.isValid()) {
					hdr.reg_14.data = reg_14_access.execute(hdr.reg_14.idx);
					hdr.reg_14.type = InstrType_t.SKIP;
				}
				if (hdr.reg_15.isValid()) {
					hdr.reg_15.data = reg_15_access.execute(hdr.reg_15.idx);
					hdr.reg_15.type = InstrType_t.SKIP;
				}
				if (hdr.reg_16.isValid()) {
					hdr.reg_16.data = reg_16_access.execute(hdr.reg_16.idx);
					hdr.reg_16.type = InstrType_t.SKIP;
				}
				if (hdr.reg_17.isValid()) {
					hdr.reg_17.data = reg_17_access.execute(hdr.reg_17.idx);
					hdr.reg_17.type = InstrType_t.SKIP;
				}
				if (hdr.reg_18.isValid()) {
					hdr.reg_18.data = reg_18_access.execute(hdr.reg_18.idx);
					hdr.reg_18.type = InstrType_t.SKIP;
				}
				if (hdr.reg_19.isValid()) {
					hdr.reg_19.data = reg_19_access.execute(hdr.reg_19.idx);
					hdr.reg_19.type = InstrType_t.SKIP;
				}
				if (hdr.reg_20.isValid()) {
					hdr.reg_20.data = reg_20_access.execute(hdr.reg_20.idx);
					hdr.reg_20.type = InstrType_t.SKIP;
				}
				if (hdr.reg_21.isValid()) {
					hdr.reg_21.data = reg_21_access.execute(hdr.reg_21.idx);
					hdr.reg_21.type = InstrType_t.SKIP;
				}
				if (hdr.reg_22.isValid()) {
					hdr.reg_22.data = reg_22_access.execute(hdr.reg_22.idx);
					hdr.reg_22.type = InstrType_t.SKIP;
				}
				if (hdr.reg_23.isValid()) {
					hdr.reg_23.data = reg_23_access.execute(hdr.reg_23.idx);
					hdr.reg_23.type = InstrType_t.SKIP;
				}
				if (hdr.reg_24.isValid()) {
					hdr.reg_24.data = reg_24_access.execute(hdr.reg_24.idx);
					hdr.reg_24.type = InstrType_t.SKIP;
				}
				if (hdr.reg_25.isValid()) {
					hdr.reg_25.data = reg_25_access.execute(hdr.reg_25.idx);
					hdr.reg_25.type = InstrType_t.SKIP;
				}
				if (hdr.reg_26.isValid()) {
					hdr.reg_26.data = reg_26_access.execute(hdr.reg_26.idx);
					hdr.reg_26.type = InstrType_t.SKIP;
				}
				if (hdr.reg_27.isValid()) {
					hdr.reg_27.data = reg_27_access.execute(hdr.reg_27.idx);
					hdr.reg_27.type = InstrType_t.SKIP;
				}
				if (hdr.reg_28.isValid()) {
					hdr.reg_28.data = reg_28_access.execute(hdr.reg_28.idx);
					hdr.reg_28.type = InstrType_t.SKIP;
				}
				if (hdr.reg_29.isValid()) {
					hdr.reg_29.data = reg_29_access.execute(hdr.reg_29.idx);
					hdr.reg_29.type = InstrType_t.SKIP;
				}
				if (hdr.reg_30.isValid()) {
					hdr.reg_30.data = reg_30_access.execute(hdr.reg_30.idx);
					hdr.reg_30.type = InstrType_t.SKIP;
				}
				if (hdr.reg_31.isValid()) {
					hdr.reg_31.data = reg_31_access.execute(hdr.reg_31.idx);
					hdr.reg_31.type = InstrType_t.SKIP;
				}
				if (hdr.reg_32.isValid()) {
					hdr.reg_32.data = reg_32_access.execute(hdr.reg_32.idx);
					hdr.reg_32.type = InstrType_t.SKIP;
				}
				if (hdr.reg_33.isValid()) {
					hdr.reg_33.data = reg_33_access.execute(hdr.reg_33.idx);
					hdr.reg_33.type = InstrType_t.SKIP;
				}
				if (hdr.reg_34.isValid()) {
					hdr.reg_34.data = reg_34_access.execute(hdr.reg_34.idx);
					hdr.reg_34.type = InstrType_t.SKIP;
				}
				if (hdr.reg_35.isValid()) {
					hdr.reg_35.data = reg_35_access.execute(hdr.reg_35.idx);
					hdr.reg_35.type = InstrType_t.SKIP;
				}
				if (hdr.reg_36.isValid()) {
					hdr.reg_36.data = reg_36_access.execute(hdr.reg_36.idx);
					hdr.reg_36.type = InstrType_t.SKIP;
				}
				if (hdr.reg_37.isValid()) {
					hdr.reg_37.data = reg_37_access.execute(hdr.reg_37.idx);
					hdr.reg_37.type = InstrType_t.SKIP;
				}
				if (hdr.reg_38.isValid()) {
					hdr.reg_38.data = reg_38_access.execute(hdr.reg_38.idx);
					hdr.reg_38.type = InstrType_t.SKIP;
				}
				if (hdr.reg_39.isValid()) {
					hdr.reg_39.data = reg_39_access.execute(hdr.reg_39.idx);
					hdr.reg_39.type = InstrType_t.SKIP;
				}
			}

			if (do_recirc) {
				recirculate(192);
			} else {
				// reply
				l2fwd.apply();
			}
		}
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

Switch(pipe) main;
