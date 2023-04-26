
#include <core.p4>
#include <t2na.p4>

// ================ BEGIN headers ================

typedef bit<48> mac_addr_t;
typedef bit<12> vlan_id_t;
typedef bit<32> ipv4_addr_t;

typedef bit<16> ether_type_t;
const ether_type_t ETHERTYPE_SLINGSHOT = 0x8000;
const ether_type_t ETHERTYPE_IPV4 = 0x0800;
const ether_type_t ETHERTYPE_ARP = 0x0806;
const ether_type_t ETHERTYPE_VLAN = 0x8100;
const ether_type_t ETHERTYPE_ECPRI = 0xAEFE;

typedef bit<4> mirror_type_t;
const mirror_type_t MIRROR_TYPE_I2E = 1;

typedef bit<8>  pkt_type_t;
const pkt_type_t PKT_TYPE_NORMAL = 1;
const pkt_type_t PKT_TYPE_MIRROR = 2;

typedef bit<16> arp_opcode_t;
const arp_opcode_t ARP_REQUEST = 1;
const arp_opcode_t ARP_REPLY = 2;

typedef bit<8> ip_protocol_t;
const ip_protocol_t IP_PROTOCOLS_UDP = 17;

header ethernet_h {
  mac_addr_t dst_addr;
  mac_addr_t src_addr;
  bit<16> ether_type;
}

header vlan_tag_h {
  bit<3> pcp;
  bit<1> cfi;
  vlan_id_t vid;
  bit<16> ether_type;
}

header ipv4_h {
  bit<4> version;
  bit<4> ihl;
  bit<8> diffserv;
  bit<16> total_len;
  bit<16> identification;
  bit<3> flags;
  bit<13> frag_offset;
  bit<8> ttl;
  ip_protocol_t protocol;
  bit<16> hdr_checksum;
  ipv4_addr_t src_addr;
  ipv4_addr_t dst_addr;
}

header udp_h {
    bit<16> src_port;
    bit<16> dst_port;
    bit<16> hdr_length;
    bit<16> checksum;
}

const bit<3> MAC_LEARN_DIGEST_TYPE = 0x1;

struct mac_learn_digest_t {
    mac_addr_t mac_addr;
    bit<9> ingress_port;
}

header arp_h {
    bit<16>       hw_type;
    ether_type_t  proto_type;
    bit<8>        hw_addr_len;
    bit<8>        proto_addr_len;
    arp_opcode_t  opcode;
} 

header arp_ipv4_h {
    mac_addr_t   src_hw_addr;
    ipv4_addr_t  src_proto_addr;
    mac_addr_t   dst_hw_addr;
    ipv4_addr_t  dst_proto_addr;
}

header ptp_h {
    bit<4> major_sdoid;
    bit<4> msg_type;
    bit<4> minor_version;
    bit<4> version;
    bit<16> msg_length;
    bit<8> domain_number;
    bit<8> minor;
    bit<16> flags;
  
    bit<48> cf_ns;
    bit<16> cf_sub_ns;
  bit<32> msg_type_specific;
    bit<64> clock_identity;
    bit<16> source_port_id;
    bit<16> sequence_id;
    bit<8> control_bits;
    bit<8> log_message_interval;
    bit<48> timestamp;
    bit<32> timestamp_nanoseconds;
}

header ptp_no_cf_h {
    bit<4> major_sdoid;
    bit<4> msg_type;
    bit<4> minor_version;
    bit<4> version;
    bit<16> msg_length;
    bit<8> domain_number;
    bit<8> minor;
    bit<16> flags;
  
    bit<32> msg_type_specific;
    bit<64> clock_identity;
    bit<16> source_port_id;
    bit<16> sequence_id;
    bit<8> control_bits;
    bit<8> log_message_interval;
    bit<48> timestamp;
    bit<32> timestamp_nanoseconds;
}

header bridged_h {
  pkt_type_t pkt_type;
  bit<48> ingress_mac_tstamp;
}

header mirror_h {
  pkt_type_t pkt_type;
  bit<48> src_addr;
  bit<48> dst_addr;
}

struct ingress_headers_t {
  bridged_h                 bridged;
  ethernet_h                ethernet;
  
  arp_h                     arp;
  arp_ipv4_h                arp_ipv4;

  vlan_tag_h                vlan_tag;

  ipv4_h                    ipv4;
  udp_h                     udp;
}

struct egress_headers_t {
  mirror_h                  mirror;
  ptp_metadata_t            tx_ptp_md_hdr;
  ethernet_h                ethernet;
  ipv4_h                    ipv4;
  udp_h                     udp;
  ptp_h                     ptp;
  ptp_no_cf_h               ptp_no_cf;
}

// ================ END headers ================
struct ingress_metadata_t {
  bit<9> ingress_port;
  MirrorId_t ing_mir_ses;
  pkt_type_t mir_pkt_type;
  bit<48> mir_src_addr;
  bit<48> mir_dst_addr;
}

struct egress_metadata_t {
  bit<48> ingress_mac_tstamp;
  bit<48> cf_ns;
}
// ================ END metadata ================

parser IngressParser(packet_in        pkt,
    /* User */    
    out ingress_headers_t          hdr,
    out ingress_metadata_t         meta,
    /* Intrinsic */
    out ingress_intrinsic_metadata_t  ig_intr_md)
{
    /* This is a mandatory state, required by Tofino Architecture */
     state start {
        pkt.extract(ig_intr_md);
        pkt.advance(PORT_METADATA_SIZE);

        transition parse_ethernet;
    }

    state parse_ethernet {
        pkt.extract(hdr.ethernet);
        transition select(hdr.ethernet.ether_type) {
            ETHERTYPE_ARP:        parse_arp;
            ETHERTYPE_VLAN:       parse_vlan_tag;
            ETHERTYPE_IPV4:       parse_ipv4;
            default: accept;
        }
    }

    state parse_vlan_tag {
        pkt.extract(hdr.vlan_tag);
        transition select(hdr.vlan_tag.ether_type) {
            ETHERTYPE_IPV4:       parse_ipv4;
            default: accept;
        }
    }

    state parse_arp {
        pkt.extract(hdr.arp);
        transition select(hdr.arp.hw_type, hdr.arp.proto_type) {
            (0x0001, ETHERTYPE_IPV4): parse_arp_ipv4;
            default: accept;
        }
    }
    
    state parse_arp_ipv4 {
        pkt.extract(hdr.arp_ipv4);
        transition accept;
    }        

    state parse_ipv4 {
        pkt.extract(hdr.ipv4);
        transition select(hdr.ipv4.protocol) {
            IP_PROTOCOLS_UDP:   parse_udp;
            default: accept;
        }
    }

    state parse_udp {
        pkt.extract(hdr.udp);
        transition accept;
    }
}

control IngressDeparser(packet_out pkt,
    /* User */
    inout ingress_headers_t                       hdr,
    in    ingress_metadata_t                      meta,
    /* Intrinsic */
    in    ingress_intrinsic_metadata_for_deparser_t  ig_dprsr_md)
{

    Digest<mac_learn_digest_t>()          mac_learn_digest;
    Mirror() mirror;

    apply {
        if (ig_dprsr_md.digest_type == MAC_LEARN_DIGEST_TYPE) {
            mac_learn_digest.pack({
                hdr.ethernet.src_addr,
                meta.ingress_port
            });
        } 
        
        if (ig_dprsr_md.mirror_type == MIRROR_TYPE_I2E) {
            mirror.emit<mirror_h>(meta.ing_mir_ses, 
                {meta.mir_pkt_type, meta.mir_src_addr, meta.mir_dst_addr});
        }
        pkt.emit(hdr);
    }
}

parser EgressParser(packet_in        pkt,
    /* User */
    out egress_headers_t          hdr,
    out egress_metadata_t         meta,
    /* Intrinsic */
    out egress_intrinsic_metadata_t  eg_intr_md)
{
    /* This is a mandatory state, required by Tofino Architecture */
    state start {
        pkt.extract(eg_intr_md);
        transition parse_pkt_type;
    }
    
    state parse_pkt_type {
        mirror_h mirror = pkt.lookahead<mirror_h>();
        transition select(mirror.pkt_type) {
            PKT_TYPE_MIRROR : parse_mirror;
            PKT_TYPE_NORMAL : parse_bridged;
            default : accept;
        }
    }
    
    state parse_mirror {
        pkt.extract(hdr.mirror);
        transition parse_ethernet;
    }
    
    state parse_bridged {
        bridged_h bridged;
        pkt.extract(bridged);
        meta.ingress_mac_tstamp = bridged.ingress_mac_tstamp;
        transition parse_ethernet;
    }

    state parse_ethernet {
        pkt.extract(hdr.ethernet);
        transition select(hdr.ethernet.ether_type) {
            ETHERTYPE_IPV4:       parse_ipv4;
            default: accept;
        }
    }
    
    state parse_ipv4 {
        pkt.extract(hdr.ipv4);
        transition select(hdr.ipv4.protocol) {
            IP_PROTOCOLS_UDP:   parse_udp;
            default: accept;
        }
    }

    state parse_udp {
        pkt.extract(hdr.udp);
        transition select(hdr.udp.src_port, hdr.udp.dst_port) {
            //(319, 319): check_ptp_msgtype;
            //(320, 320): check_ptp_msgtype;
            default: accept;
        }
    }

    state check_ptp_msgtype {
        ptp_h ptp_tmp = pkt.lookahead<ptp_h>();
        transition select(ptp_tmp.msg_type) {
            0x0: parse_ptp;
            0x1: parse_ptp;
            0x9: parse_ptp;
            default: accept;
        }
    }

    state parse_ptp {
        pkt.extract(hdr.ptp);
        meta.cf_ns = hdr.ptp.cf_ns;
        transition accept;
    }
}

control EgressDeparser(packet_out pkt,
    /* User */
    inout egress_headers_t                       hdr,
    in    egress_metadata_t                      meta,
    /* Intrinsic */
    in    egress_intrinsic_metadata_for_deparser_t  eg_dprsr_md)
{
    apply {
        pkt.emit(hdr);
    }
}

control Ingress(/* User */
                inout ingress_headers_t                       hdr,
                inout ingress_metadata_t                      meta,
                /* Intrinsic */
                in    ingress_intrinsic_metadata_t               ig_intr_md,
                in    ingress_intrinsic_metadata_from_parser_t   ig_prsr_md,
                inout ingress_intrinsic_metadata_for_deparser_t  ig_dprsr_md,
                inout ingress_intrinsic_metadata_for_tm_t        ig_tm_md)
{
  action mirror_fwd_match(MirrorId_t ing_ses, mac_addr_t dst_addr) {
    // Enable mirror for this packet in deparser
    ig_dprsr_md.mirror_type = MIRROR_TYPE_I2E;

    meta.mir_pkt_type = PKT_TYPE_MIRROR;
    
    meta.mir_dst_addr = dst_addr;
    meta.mir_src_addr = hdr.ethernet.dst_addr;

    // Which a session includes which port to use
    meta.ing_mir_ses = ing_ses;
    
  }
  
  action mirror_fwd_miss() {
    ig_dprsr_md.mirror_type = 0;
  }

  table  mirror_fwd {
    key = {
      ig_intr_md.ingress_port  : exact;
      hdr.ethernet.src_addr  : exact;
    }

    actions = {
      mirror_fwd_match;
      @defaultonly mirror_fwd_miss;
    }
  }
  action drop() {
    ig_dprsr_md.drop_ctl = 1;
  }
  
  action broadcast_miss() {
    ig_tm_md.mcast_grp_a = (bit<16>)ig_intr_md.ingress_port;
  }

  action forward_match(PortId_t port) {
    ig_tm_md.ucast_egress_port = port;
  }

  table forward {
    key = {
      hdr.ethernet.dst_addr: exact;
    }
    actions = {
      forward_match;
      broadcast_miss;
    }
    const default_action = broadcast_miss();
    size = 1024;
  }
  
  action learned_mac_digest () {
    ig_dprsr_md.digest_type = MAC_LEARN_DIGEST_TYPE;
    meta.ingress_port = ig_intr_md.ingress_port;
  }

  action learned_mac_match() { 
    ig_dprsr_md.digest_type = 0;
  }

  table learned_mac {
    key = {
      hdr.ethernet.src_addr: exact;
    }
    actions = {
      learned_mac_match; 
      learned_mac_digest;
    }
    const default_action = learned_mac_digest();
    size = 1024;
    idle_timeout = true;
  }

  action bcast_arp_req () {
      ig_tm_md.mcast_grp_a = (bit<16>)ig_intr_md.ingress_port;
  }

  table process_arp {
    key = {
      hdr.arp.isValid(): exact;
      hdr.arp_ipv4.isValid(): exact;
      hdr.arp.opcode: exact;
      hdr.ethernet.dst_addr: exact;
    }
    actions = {
      bcast_arp_req;
      NoAction;
    }
    const default_action = NoAction();
    const entries = {
      (true, true, ARP_REQUEST, 0xFFFFFFFFFFFF) : bcast_arp_req();
    }
  }
  
  apply {
    // Before anything: learn the mac
    learned_mac.apply();

    // Handle ARP
    process_arp.apply();
    
    // for all packets not dropped, find the port out
    forward.apply();
  
    mirror_fwd.apply();
    
    hdr.bridged.setValid();
    hdr.bridged.pkt_type = PKT_TYPE_NORMAL;
    hdr.bridged.ingress_mac_tstamp = ig_intr_md.ingress_mac_tstamp;
  }
}

control Egress(
    /* User */
    inout egress_headers_t                          hdr,
    inout egress_metadata_t                         meta,
    /* Intrinsic */    
    in    egress_intrinsic_metadata_t                  eg_intr_md,
    in    egress_intrinsic_metadata_from_parser_t      eg_prsr_md,
    inout egress_intrinsic_metadata_for_deparser_t     eg_dprsr_md,
    inout egress_intrinsic_metadata_for_output_port_t  eg_oport_md)
{
  
  action update_ptp_hdr() {
    hdr.ptp_no_cf.major_sdoid = hdr.ptp.major_sdoid;
    hdr.ptp_no_cf.msg_type = hdr.ptp.msg_type;
    hdr.ptp_no_cf.minor_version = hdr.ptp.minor_version;
    hdr.ptp_no_cf.version = hdr.ptp.version;
    hdr.ptp_no_cf.msg_length = hdr.ptp.msg_length;
    hdr.ptp_no_cf.domain_number = hdr.ptp.domain_number;
    hdr.ptp_no_cf.minor = hdr.ptp.minor;
    hdr.ptp_no_cf.flags = hdr.ptp.flags;
  
    hdr.ptp_no_cf.msg_type_specific = hdr.ptp.msg_type_specific;
    hdr.ptp_no_cf.clock_identity = hdr.ptp.clock_identity;
    hdr.ptp_no_cf.source_port_id = hdr.ptp.source_port_id;
    hdr.ptp_no_cf.sequence_id = hdr.ptp.sequence_id;
    hdr.ptp_no_cf.control_bits = hdr.ptp.control_bits;
    hdr.ptp_no_cf.log_message_interval = hdr.ptp.log_message_interval;
    hdr.ptp_no_cf.timestamp = hdr.ptp.timestamp;
    hdr.ptp_no_cf.timestamp_nanoseconds = hdr.ptp.timestamp_nanoseconds;
  }
  
  apply {
    eg_oport_md.update_delay_on_tx = 0;
   
    if (hdr.mirror.isValid() && hdr.ethernet.isValid()) {
      hdr.ethernet.dst_addr = hdr.mirror.dst_addr;
      hdr.ethernet.src_addr = hdr.mirror.src_addr;
      hdr.mirror.setInvalid();
    }
    
    if (hdr.ptp.isValid()) {
      // Declare and initialize two temporary variables for the 64-bit arithmetic
      bit<64> tmp1 = (bit<64>)meta.cf_ns;
      bit<64> tmp2 = (bit<64>)meta.ingress_mac_tstamp;
      
      // request tx ptp correction timestamp insertion
      eg_oport_md.update_delay_on_tx = 1;

      // Instructions for the ptp correction timestamp writer
      hdr.tx_ptp_md_hdr.setValid();
      hdr.tx_ptp_md_hdr.cf_byte_offset = 8w50;
      hdr.tx_ptp_md_hdr.udp_cksum_byte_offset = 8w0; // Let the writer not update the UDP checksum
      
      // Do 64-bit arithmetic and then write the result back
      tmp1 = tmp1 - tmp2;
      @in_hash { hdr.tx_ptp_md_hdr.updated_cf[47:32] = tmp1[47:32]; }
      hdr.tx_ptp_md_hdr.updated_cf[31:0] = tmp1[31:0];
    
      // Remove the ptp correction field
      hdr.ptp_no_cf.setValid();
      update_ptp_hdr();
      hdr.ptp.setInvalid();
    }
  }
}

Pipeline(
    IngressParser(),
    Ingress(),
    IngressDeparser(),
    EgressParser(),
    Egress(),
    EgressDeparser()
) pipe;

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

	bit<16> i0;
	bit<32> d0;

	bit<16> i1;
	bit<32> d1;

	bit<16> i2;
	bit<32> d2;

	bit<16> i3;
	bit<32> d3;

	bit<16> i4;
	bit<32> d4;

	bit<16> i5;
	bit<32> d5;

	bit<16> i6;
	bit<32> d6;

	bit<16> i7;
	bit<32> d7;

	bit<16> i8;
	bit<32> d8;

	bit<16> i9;
	bit<32> d9;

	bit<16> i10;
	bit<32> d10;

	bit<16> i11;
	bit<32> d11;

	bit<16> i12;
	bit<32> d12;

	bit<16> i13;
	bit<32> d13;

	bit<16> i14;
	bit<32> d14;

	bit<16> i15;
	bit<32> d15;

	bit<16> i16;
	bit<32> d16;

	bit<16> i17;
	bit<32> d17;

	bit<16> i18;
	bit<32> d18;

	bit<16> i19;
	bit<32> d19;

	bit<8> i20;
	bit<32> d20;

	bit<8> i21;
	bit<32> d21;

	bit<8> i22;
	bit<32> d22;

	bit<8> i23;
	bit<32> d23;

	bit<8> i24;
	bit<32> d24;

	bit<8> i25;
	bit<32> d25;

	bit<8> i26;
	bit<32> d26;

	bit<8> i27;
	bit<32> d27;

	bit<8> i28;
	bit<32> d28;

	bit<8> i29;
	bit<32> d29;

	bit<8> i30;
	bit<32> d30;

	bit<8> i31;
	bit<32> d31;

	bit<8> i32;
	bit<32> d32;

	bit<8> i33;
	bit<32> d33;

	bit<8> i34;
	bit<32> d34;

	bit<8> i35;
	bit<32> d35;

	bit<8> i36;
	bit<32> d36;

	bit<8> i37;
	bit<32> d37;

	bit<8> i38;
	bit<32> d38;

	bit<8> i39;
	bit<32> d39;

	bit<8> i40;
	bit<32> d40;

	bit<8> i41;
	bit<32> d41;

	bit<8> i42;
	bit<32> d42;

	bit<8> i43;
	bit<32> d43;

	bit<8> i44;
	bit<32> d44;

	bit<8> i45;
	bit<32> d45;

	bit<8> i46;
	bit<32> d46;

	bit<8> i47;
	bit<32> d47;

	bit<8> i48;
	bit<32> d48;

	bit<8> i49;
	bit<32> d49;

	bit<8> i50;
	bit<32> d50;

	bit<8> i51;
	bit<32> d51;

	bit<8> i52;
	bit<32> d52;

	bit<8> i53;
	bit<32> d53;

	bit<8> i54;
	bit<32> d54;

	bit<8> i55;
	bit<32> d55;

	bit<8> i56;
	bit<32> d56;

	bit<8> i57;
	bit<32> d57;

	bit<8> i58;
	bit<32> d58;

	bit<8> i59;
	bit<32> d59;

	bit<8> i60;
	bit<32> d60;

	bit<8> i61;
	bit<32> d61;

	bit<8> i62;
	bit<32> d62;

	bit<8> i63;
	bit<32> d63;

	bit<8> i64;
	bit<32> d64;

	bit<8> i65;
	bit<32> d65;

	bit<8> i66;
	bit<32> d66;

	bit<8> i67;
	bit<32> d67;

	bit<8> i68;
	bit<32> d68;

	bit<8> i69;
	bit<32> d69;

	bit<8> i70;
	bit<32> d70;

	bit<8> i71;
	bit<32> d71;
}

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

	state start {
		pkt.extract(ig_intr_md);
		pkt.advance(PORT_METADATA_SIZE);
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

    Register<bit<32>, bit<16>>(32768, 0x00000000) reg_0;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_0) reg_0_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i0[7:7] == 1) {     
                value = hdr.instrs.d0;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x01010101) reg_1;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_1) reg_1_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i1[7:7] == 1) {     
                value = hdr.instrs.d1;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x02020202) reg_2;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_2) reg_2_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i2[7:7] == 1) {     
                value = hdr.instrs.d2;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x03030303) reg_3;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_3) reg_3_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i3[7:7] == 1) {     
                value = hdr.instrs.d3;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x04040404) reg_4;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_4) reg_4_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i4[7:7] == 1) {     
                value = hdr.instrs.d4;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x05050505) reg_5;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_5) reg_5_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i5[7:7] == 1) {     
                value = hdr.instrs.d5;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x06060606) reg_6;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_6) reg_6_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i6[7:7] == 1) {     
                value = hdr.instrs.d6;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x07070707) reg_7;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_7) reg_7_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i7[7:7] == 1) {     
                value = hdr.instrs.d7;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x08080808) reg_8;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_8) reg_8_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i8[7:7] == 1) {     
                value = hdr.instrs.d8;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x09090909) reg_9;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_9) reg_9_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i9[7:7] == 1) {     
                value = hdr.instrs.d9;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x10101010) reg_10;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_10) reg_10_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i10[7:7] == 1) {     
                value = hdr.instrs.d10;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x11111111) reg_11;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_11) reg_11_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i11[7:7] == 1) {     
                value = hdr.instrs.d11;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x12121212) reg_12;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_12) reg_12_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i12[7:7] == 1) {     
                value = hdr.instrs.d12;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x13131313) reg_13;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_13) reg_13_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i13[7:7] == 1) {     
                value = hdr.instrs.d13;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x14141414) reg_14;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_14) reg_14_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i14[7:7] == 1) {     
                value = hdr.instrs.d14;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x15151515) reg_15;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_15) reg_15_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i15[7:7] == 1) {     
                value = hdr.instrs.d15;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x16161616) reg_16;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_16) reg_16_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i16[7:7] == 1) {     
                value = hdr.instrs.d16;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x17171717) reg_17;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_17) reg_17_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i17[7:7] == 1) {     
                value = hdr.instrs.d17;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x18181818) reg_18;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_18) reg_18_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i18[7:7] == 1) {     
                value = hdr.instrs.d18;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x19191919) reg_19;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_19) reg_19_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i19[7:7] == 1) {     
                value = hdr.instrs.d19;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x20202020) reg_20;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_20) reg_20_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i20[7:7] == 1) {     
                value = hdr.instrs.d20;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x21212121) reg_21;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_21) reg_21_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i21[7:7] == 1) {     
                value = hdr.instrs.d21;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x22222222) reg_22;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_22) reg_22_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i22[7:7] == 1) {     
                value = hdr.instrs.d22;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x23232323) reg_23;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_23) reg_23_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i23[7:7] == 1) {     
                value = hdr.instrs.d23;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x24242424) reg_24;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_24) reg_24_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i24[7:7] == 1) {     
                value = hdr.instrs.d24;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x25252525) reg_25;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_25) reg_25_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i25[7:7] == 1) {     
                value = hdr.instrs.d25;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x26262626) reg_26;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_26) reg_26_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i26[7:7] == 1) {     
                value = hdr.instrs.d26;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x27272727) reg_27;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_27) reg_27_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i27[7:7] == 1) {     
                value = hdr.instrs.d27;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x28282828) reg_28;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_28) reg_28_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i28[7:7] == 1) {     
                value = hdr.instrs.d28;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x29292929) reg_29;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_29) reg_29_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i29[7:7] == 1) {     
                value = hdr.instrs.d29;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x30303030) reg_30;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_30) reg_30_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i30[7:7] == 1) {     
                value = hdr.instrs.d30;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x31313131) reg_31;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_31) reg_31_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i31[7:7] == 1) {     
                value = hdr.instrs.d31;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x32323232) reg_32;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_32) reg_32_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i32[7:7] == 1) {     
                value = hdr.instrs.d32;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x33333333) reg_33;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_33) reg_33_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i33[7:7] == 1) {     
                value = hdr.instrs.d33;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x34343434) reg_34;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_34) reg_34_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i34[7:7] == 1) {     
                value = hdr.instrs.d34;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x35353535) reg_35;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_35) reg_35_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i35[7:7] == 1) {     
                value = hdr.instrs.d35;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x36363636) reg_36;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_36) reg_36_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i36[7:7] == 1) {     
                value = hdr.instrs.d36;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x37373737) reg_37;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_37) reg_37_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i37[7:7] == 1) {     
                value = hdr.instrs.d37;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x38383838) reg_38;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_38) reg_38_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i38[7:7] == 1) {     
                value = hdr.instrs.d38;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x39393939) reg_39;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_39) reg_39_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i39[7:7] == 1) {     
                value = hdr.instrs.d39;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x40404040) reg_40;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_40) reg_40_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i40[7:7] == 1) {     
                value = hdr.instrs.d40;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x41414141) reg_41;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_41) reg_41_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i41[7:7] == 1) {     
                value = hdr.instrs.d41;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x42424242) reg_42;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_42) reg_42_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i42[7:7] == 1) {     
                value = hdr.instrs.d42;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x43434343) reg_43;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_43) reg_43_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i43[7:7] == 1) {     
                value = hdr.instrs.d43;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x44444444) reg_44;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_44) reg_44_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i44[7:7] == 1) {     
                value = hdr.instrs.d44;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x45454545) reg_45;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_45) reg_45_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i45[7:7] == 1) {     
                value = hdr.instrs.d45;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x46464646) reg_46;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_46) reg_46_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i46[7:7] == 1) {     
                value = hdr.instrs.d46;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x47474747) reg_47;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_47) reg_47_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i47[7:7] == 1) {     
                value = hdr.instrs.d47;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x48484848) reg_48;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_48) reg_48_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i48[7:7] == 1) {     
                value = hdr.instrs.d48;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x49494949) reg_49;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_49) reg_49_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i49[7:7] == 1) {     
                value = hdr.instrs.d49;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x50505050) reg_50;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_50) reg_50_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i50[7:7] == 1) {     
                value = hdr.instrs.d50;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x51515151) reg_51;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_51) reg_51_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i51[7:7] == 1) {     
                value = hdr.instrs.d51;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x52525252) reg_52;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_52) reg_52_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i52[7:7] == 1) {     
                value = hdr.instrs.d52;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x53535353) reg_53;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_53) reg_53_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i53[7:7] == 1) {     
                value = hdr.instrs.d53;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x54545454) reg_54;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_54) reg_54_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i54[7:7] == 1) {     
                value = hdr.instrs.d54;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x55555555) reg_55;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_55) reg_55_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i55[7:7] == 1) {     
                value = hdr.instrs.d55;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x56565656) reg_56;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_56) reg_56_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i56[7:7] == 1) {     
                value = hdr.instrs.d56;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x57575757) reg_57;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_57) reg_57_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i57[7:7] == 1) {     
                value = hdr.instrs.d57;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x58585858) reg_58;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_58) reg_58_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i58[7:7] == 1) {     
                value = hdr.instrs.d58;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x59595959) reg_59;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_59) reg_59_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i59[7:7] == 1) {     
                value = hdr.instrs.d59;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x60606060) reg_60;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_60) reg_60_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i60[7:7] == 1) {     
                value = hdr.instrs.d60;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x61616161) reg_61;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_61) reg_61_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i61[7:7] == 1) {     
                value = hdr.instrs.d61;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x62626262) reg_62;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_62) reg_62_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i62[7:7] == 1) {     
                value = hdr.instrs.d62;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x63636363) reg_63;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_63) reg_63_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i63[7:7] == 1) {     
                value = hdr.instrs.d63;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x64646464) reg_64;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_64) reg_64_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i64[7:7] == 1) {     
                value = hdr.instrs.d64;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x65656565) reg_65;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_65) reg_65_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i65[7:7] == 1) {     
                value = hdr.instrs.d65;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x66666666) reg_66;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_66) reg_66_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i66[7:7] == 1) {     
                value = hdr.instrs.d66;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x67676767) reg_67;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_67) reg_67_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i67[7:7] == 1) {     
                value = hdr.instrs.d67;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x68686868) reg_68;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_68) reg_68_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i68[7:7] == 1) {     
                value = hdr.instrs.d68;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x69696969) reg_69;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_69) reg_69_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i69[7:7] == 1) {     
                value = hdr.instrs.d69;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x70707070) reg_70;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_70) reg_70_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i70[7:7] == 1) {     
                value = hdr.instrs.d70;
            }
        }
    };

    Register<bit<32>, bit<16>>(32768, 0x71717171) reg_71;
    RegisterAction<bit<32>, bit<16>, bit<32>>(reg_71) reg_71_access = {
        void apply(inout bit<32> value, out bit<32> rv) {
            rv = value;
            if (hdr.instrs.i71[7:7] == 1) {     
                value = hdr.instrs.d71;
            }
        }
    };

	apply {
        ig_intr_md_for_tm.bypass_egress = 1;
		bit<9> offset = (bit<9>) (hdr.instrs.offset >> 7);

		hdr.instrs.d0 = reg_0_access.execute(offset ++ hdr.instrs.i0[6:0]);
		hdr.instrs.d1 = reg_1_access.execute(offset ++ hdr.instrs.i1[6:0]);
		hdr.instrs.d2 = reg_2_access.execute(offset ++ hdr.instrs.i2[6:0]);
		hdr.instrs.d3 = reg_3_access.execute(offset ++ hdr.instrs.i3[6:0]);
		hdr.instrs.d4 = reg_4_access.execute(offset ++ hdr.instrs.i4[6:0]);
		hdr.instrs.d5 = reg_5_access.execute(offset ++ hdr.instrs.i5[6:0]);
		hdr.instrs.d6 = reg_6_access.execute(offset ++ hdr.instrs.i6[6:0]);
		hdr.instrs.d7 = reg_7_access.execute(offset ++ hdr.instrs.i7[6:0]);
		hdr.instrs.d8 = reg_8_access.execute(offset ++ hdr.instrs.i8[6:0]);
		hdr.instrs.d9 = reg_9_access.execute(offset ++ hdr.instrs.i9[6:0]);
		hdr.instrs.d10 = reg_10_access.execute(offset ++ hdr.instrs.i10[6:0]);
		hdr.instrs.d11 = reg_11_access.execute(offset ++ hdr.instrs.i11[6:0]);
		hdr.instrs.d12 = reg_12_access.execute(offset ++ hdr.instrs.i12[6:0]);
		hdr.instrs.d13 = reg_13_access.execute(offset ++ hdr.instrs.i13[6:0]);
		hdr.instrs.d14 = reg_14_access.execute(offset ++ hdr.instrs.i14[6:0]);
		hdr.instrs.d15 = reg_15_access.execute(offset ++ hdr.instrs.i15[6:0]);
		hdr.instrs.d16 = reg_16_access.execute(offset ++ hdr.instrs.i16[6:0]);
		hdr.instrs.d17 = reg_17_access.execute(offset ++ hdr.instrs.i17[6:0]);
		hdr.instrs.d18 = reg_18_access.execute(offset ++ hdr.instrs.i18[6:0]);
		hdr.instrs.d19 = reg_19_access.execute(offset ++ hdr.instrs.i19[6:0]);
		hdr.instrs.d20 = reg_20_access.execute(offset ++ hdr.instrs.i20[6:0]);
		hdr.instrs.d21 = reg_21_access.execute(offset ++ hdr.instrs.i21[6:0]);
		hdr.instrs.d22 = reg_22_access.execute(offset ++ hdr.instrs.i22[6:0]);
		hdr.instrs.d23 = reg_23_access.execute(offset ++ hdr.instrs.i23[6:0]);
		hdr.instrs.d24 = reg_24_access.execute(offset ++ hdr.instrs.i24[6:0]);
		hdr.instrs.d25 = reg_25_access.execute(offset ++ hdr.instrs.i25[6:0]);
		hdr.instrs.d26 = reg_26_access.execute(offset ++ hdr.instrs.i26[6:0]);
		hdr.instrs.d27 = reg_27_access.execute(offset ++ hdr.instrs.i27[6:0]);
		hdr.instrs.d28 = reg_28_access.execute(offset ++ hdr.instrs.i28[6:0]);
		hdr.instrs.d29 = reg_29_access.execute(offset ++ hdr.instrs.i29[6:0]);
		hdr.instrs.d30 = reg_30_access.execute(offset ++ hdr.instrs.i30[6:0]);
		hdr.instrs.d31 = reg_31_access.execute(offset ++ hdr.instrs.i31[6:0]);
		hdr.instrs.d32 = reg_32_access.execute(offset ++ hdr.instrs.i32[6:0]);
		hdr.instrs.d33 = reg_33_access.execute(offset ++ hdr.instrs.i33[6:0]);
		hdr.instrs.d34 = reg_34_access.execute(offset ++ hdr.instrs.i34[6:0]);
		hdr.instrs.d35 = reg_35_access.execute(offset ++ hdr.instrs.i35[6:0]);
		hdr.instrs.d36 = reg_36_access.execute(offset ++ hdr.instrs.i36[6:0]);
		hdr.instrs.d37 = reg_37_access.execute(offset ++ hdr.instrs.i37[6:0]);
		hdr.instrs.d38 = reg_38_access.execute(offset ++ hdr.instrs.i38[6:0]);
		hdr.instrs.d39 = reg_39_access.execute(offset ++ hdr.instrs.i39[6:0]);
		hdr.instrs.d40 = reg_40_access.execute(offset ++ hdr.instrs.i40[6:0]);
		hdr.instrs.d41 = reg_41_access.execute(offset ++ hdr.instrs.i41[6:0]);
		hdr.instrs.d42 = reg_42_access.execute(offset ++ hdr.instrs.i42[6:0]);
		hdr.instrs.d43 = reg_43_access.execute(offset ++ hdr.instrs.i43[6:0]);
		hdr.instrs.d44 = reg_44_access.execute(offset ++ hdr.instrs.i44[6:0]);
		hdr.instrs.d45 = reg_45_access.execute(offset ++ hdr.instrs.i45[6:0]);
		hdr.instrs.d46 = reg_46_access.execute(offset ++ hdr.instrs.i46[6:0]);
		hdr.instrs.d47 = reg_47_access.execute(offset ++ hdr.instrs.i47[6:0]);
		hdr.instrs.d48 = reg_48_access.execute(offset ++ hdr.instrs.i48[6:0]);
		hdr.instrs.d49 = reg_49_access.execute(offset ++ hdr.instrs.i49[6:0]);
		hdr.instrs.d50 = reg_50_access.execute(offset ++ hdr.instrs.i50[6:0]);
		hdr.instrs.d51 = reg_51_access.execute(offset ++ hdr.instrs.i51[6:0]);
		hdr.instrs.d52 = reg_52_access.execute(offset ++ hdr.instrs.i52[6:0]);
		hdr.instrs.d53 = reg_53_access.execute(offset ++ hdr.instrs.i53[6:0]);
		hdr.instrs.d54 = reg_54_access.execute(offset ++ hdr.instrs.i54[6:0]);
		hdr.instrs.d55 = reg_55_access.execute(offset ++ hdr.instrs.i55[6:0]);
		hdr.instrs.d56 = reg_56_access.execute(offset ++ hdr.instrs.i56[6:0]);
		hdr.instrs.d57 = reg_57_access.execute(offset ++ hdr.instrs.i57[6:0]);
		hdr.instrs.d58 = reg_58_access.execute(offset ++ hdr.instrs.i58[6:0]);
		hdr.instrs.d59 = reg_59_access.execute(offset ++ hdr.instrs.i59[6:0]);
		hdr.instrs.d60 = reg_60_access.execute(offset ++ hdr.instrs.i60[6:0]);
		hdr.instrs.d61 = reg_61_access.execute(offset ++ hdr.instrs.i61[6:0]);
		hdr.instrs.d62 = reg_62_access.execute(offset ++ hdr.instrs.i62[6:0]);
		hdr.instrs.d63 = reg_63_access.execute(offset ++ hdr.instrs.i63[6:0]);
		hdr.instrs.d64 = reg_64_access.execute(offset ++ hdr.instrs.i64[6:0]);
		hdr.instrs.d65 = reg_65_access.execute(offset ++ hdr.instrs.i65[6:0]);
		hdr.instrs.d66 = reg_66_access.execute(offset ++ hdr.instrs.i66[6:0]);
		hdr.instrs.d67 = reg_67_access.execute(offset ++ hdr.instrs.i67[6:0]);
		hdr.instrs.d68 = reg_68_access.execute(offset ++ hdr.instrs.i68[6:0]);
		hdr.instrs.d69 = reg_69_access.execute(offset ++ hdr.instrs.i69[6:0]);
		hdr.instrs.d70 = reg_70_access.execute(offset ++ hdr.instrs.i70[6:0]);
		hdr.instrs.d71 = reg_71_access.execute(offset ++ hdr.instrs.i71[6:0]);
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
         EmptyEgressDeparser()) pipe2;

Switch(pipe, pipe, pipe, pipe2) main;
