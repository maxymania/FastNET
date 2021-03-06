/*
 *   Copyright 2017 Simon Schmidt
 *   Copyright 2011-2016 by Andrey Butok. FNET Community.
 *   Copyright 2008-2010 by Andrey Butok. Freescale Semiconductor, Inc.
 *   Copyright 2003 by Andrey Butok. Motorola SPS.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */
#include <net/types.h>
#include <net/nif.h>
#include <net/ipv4.h>
#include <net/header/arphdr.h>
#include <net/ipv4_mac_cache.h>
#include <net/packet_input.h>
#include <net/packet_output.h>
#include <net/header/ethhdr.h>
#include <net/mac_addr_ldst.h>
#include <net/safe_packet.h>

#define M2I fastnet_mac_to_int
#define I2M fastnet_int_to_mac

static void arp_setmacaddrs(fnet_eth_header_t* __restrict__ ethp, uint64_t src, uint64_t dst){
	union {
		uint8_t  addr8[8];
		uint64_t addr64 ODP_PACKED;
	} srci = { .addr64 = src };
	
	union {
		uint8_t  addr8[8];
		uint64_t addr64 ODP_PACKED;
	} dsti = { .addr64 = dst };
	
	ethp->destination_addr[0] = srci.addr8[0];
	ethp->destination_addr[1] = srci.addr8[1];
	ethp->destination_addr[2] = srci.addr8[2];
	ethp->destination_addr[3] = srci.addr8[3];
	ethp->destination_addr[4] = srci.addr8[4];
	ethp->destination_addr[5] = srci.addr8[5];
	ethp->source_addr[0] = dsti.addr8[0];
	ethp->source_addr[1] = dsti.addr8[1];
	ethp->source_addr[2] = dsti.addr8[2];
	ethp->source_addr[3] = dsti.addr8[3];
	ethp->source_addr[4] = dsti.addr8[4];
	ethp->source_addr[5] = dsti.addr8[5];
}

netpp_retcode_t fastnet_arp_input(odp_packet_t pkt){
	odp_packet_t        chain;
	nif_t*              nif;
	fnet_arp_header_t*  arp_hdr;
	ipv4_addr_t         sender_prot_addr;
	ipv4_addr_t         target_prot_addr;
	uint64_t            sender_hard_addr;
	uint64_t            target_hard_addr;
	uint32_t            pretrail;
	int                 is_ours;
	
	nif = odp_packet_user_ptr(pkt);
	
	if(odp_unlikely(nif->ipv4 == NULL)) return NETPP_DROP;
	
	target_hard_addr = nif->hwaddr;
	
	arp_hdr = fastnet_safe_l3(pkt,sizeof(fnet_arp_header_t));
	if (odp_unlikely(arp_hdr == NULL)) return NETPP_DROP;
	
	sender_prot_addr = arp_hdr->sender_prot_addr;
	target_prot_addr = arp_hdr->target_prot_addr;
	sender_hard_addr = M2I(arp_hdr->sender_hard_addr);
	//target_hard_addr = M2I(arp_hdr->target_hard_addr);
	
	if (!IP4ADDR_EQ(sender_prot_addr,nif->ipv4->address)){
		/*
		 * If the target protocol address is ours, we're going to create a new ARP
		 * cache entry. Otherwise we update it, if it exists.
		 */
		is_ours = IP4ADDR_EQ(target_prot_addr,nif->ipv4->address) ? 1 : 0; /* It's for me. */
		
		/*
		 * Create or update ARP entry.
		 */
		chain = fastnet_ipv4_mac_put(nif,sender_prot_addr,sender_hard_addr,is_ours);
		
		/*
		 * Send all network packets out to the 'sender_hard_addr'.
		 */
		fastnet_ip_arp_transmit(chain,nif,nif->hwaddr,sender_hard_addr);
	}else{
		is_ours = 0;
		// TODO: duplicate address detection.
	}
	
	/* ARP request. If it asked for our address, we send out a reply.*/
	if(is_ours && (odp_be_to_cpu_16(arp_hdr->op) == FNET_ARP_OP_REQUEST) ){
		
		arp_hdr->op = odp_cpu_to_be_16(FNET_ARP_OP_REPLY); /* Opcode */
		
		I2M(arp_hdr->target_hard_addr,sender_hard_addr);
		I2M(arp_hdr->sender_hard_addr,target_hard_addr);
		
		arp_hdr->target_prot_addr = arp_hdr->sender_prot_addr;
		arp_hdr->sender_prot_addr = nif->ipv4->address;
		
		arp_setmacaddrs(odp_packet_l2_ptr(pkt,NULL),sender_hard_addr,target_hard_addr);
		
		pretrail = odp_packet_l2_offset(pkt);
		if(odp_unlikely(pretrail>0))
			odp_packet_pull_head(pkt,pretrail);
		
		return fastnet_pkt_output(pkt,nif);
	}
	
	return NETPP_DROP;
}


typedef struct ODP_PACKED {
	fnet_eth_header_t eth;
	fnet_arp_header_t arp;
} arp_pkt_t;

int fastnet_arp_output(ipv4_addr_t src,ipv4_addr_t dst,nif_t* nif){
	odp_pool_t      pool;
	odp_packet_t    pkt;
	arp_pkt_t*      hdr;
	netpp_retcode_t ret;
	
	pool = odp_pool_lookup("fn_pktout");
	if(odp_unlikely(pool == ODP_POOL_INVALID)) return 0;
	
	pkt  = odp_packet_alloc(pool,sizeof(arp_pkt_t));
	if(odp_unlikely(pkt == ODP_PACKET_INVALID)) return 0;
	
	hdr = odp_packet_offset(pkt,0,NULL,NULL);
	
	hdr->eth.type = odp_cpu_to_be_16(NETPROT_L3_ARP);
	hdr->arp.hard_type = odp_cpu_to_be_16(1);            /* The type of hardware address (=1 for Ethernet).*/
	hdr->arp.prot_type = odp_cpu_to_be_16(0x0800);       /* The type of protocol address (=0x0800 for IP). */
	hdr->arp.hard_size = 6;                              /* The size in bytes of the hardware address (=6). */
	hdr->arp.prot_size = 4;                              /* The size in bytes of the protocol address (=4). */
	hdr->arp.op = odp_cpu_to_be_16(FNET_ARP_OP_REQUEST); /* Opcode. */
	/*
	 * Destination addresses:
	 * - Ethernet header: broadcast-address
	 * - ARP header: NULL-address.
	 */
	hdr->eth.destination_addr[0] = 0xff;
	hdr->eth.destination_addr[1] = 0xff;
	hdr->eth.destination_addr[2] = 0xff;
	hdr->eth.destination_addr[3] = 0xff;
	hdr->eth.destination_addr[4] = 0xff;
	hdr->eth.destination_addr[5] = 0xff;
	hdr->arp.target_hard_addr[0] = 0;
	hdr->arp.target_hard_addr[1] = 0;
	hdr->arp.target_hard_addr[2] = 0;
	hdr->arp.target_hard_addr[3] = 0;
	hdr->arp.target_hard_addr[4] = 0;
	hdr->arp.target_hard_addr[5] = 0;
	
	/*
	 * Set Source and Destination IP addresses in the ARP header
	 */
	hdr->arp.sender_prot_addr = src;
	hdr->arp.target_prot_addr = dst;
	
	I2M(hdr->arp.sender_hard_addr,nif->hwaddr);
	I2M(hdr->eth.source_addr     ,nif->hwaddr);
	
	odp_packet_l2_offset_set(pkt,0);
	odp_packet_l3_offset_set(pkt,sizeof(fnet_eth_header_t));
	odp_packet_has_eth_set(pkt,1);
	odp_packet_has_arp_set(pkt,1);
	
	ret = fastnet_pkt_output(pkt,nif);
	if(odp_unlikely(ret!=NETPP_CONSUMED)){
		odp_packet_free(pkt);
		return 0;
	}
	
	return 1;
}

