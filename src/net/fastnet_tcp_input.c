/*
 *   Copyright 2017 Simon Schmidt
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
#include <net/nif.h>
#include <net/types.h>
#include <net/_config.h>
#include <net/header/tcphdr.h>
#include <net/safe_packet.h>
#include <net/socket_tcp.h>
#include <net/fastnet_tcp.h>
#include <net/header/layer4.h>
#include <net/checksum.h>

static uint32_t random_syn = 1234;

enum {
	TCP_LISTEN_MASK = FNET_TCP_SGT_RST|FNET_TCP_SGT_SYN|FNET_TCP_SGT_ACK,
};

/* netpp_retcode_t fastnet_tcp_input_listen(odp_packet_t pkt, fastnet_socket_t sock, fastnet_tcp_pcb_t* pcb, socket_key_t *key,int *send_rst) */



static
netpp_retcode_t fastnet_tcp_input_listen(odp_packet_t pkt, fastnet_tcp_pcb_t* parent_pcb, socket_key_t *key,int *send_rst) {
	fastnet_socket_t sock;
	fastnet_tcp_pcb_t*    pcb;
	uint32_t seg_seq,iss;
	//*send_rst = 1;
	
	
	fnet_tcp_header_t* th = fastnet_safe_l3(pkt,sizeof(fnet_tcp_header_t));
	if(odp_unlikely(th==NULL)) return NETPP_DROP;
	
	uint16_t flags = odp_be_to_cpu_16(th->hdrlength__flags);
	
	/*
	 * If the state is LISTEN then
	 *  1. check for an RST  : If RST Then DROP
	 *  2. check for an ACK  : If ACK Then <SEQ=SEG.ACK><CTL=RST>
	 *  3. check for a SYN   : Unless SYN <SEQ=SEG.ACK><CTL=RST>
	 *
	 * The Implementation is radically optimized:
	 *  IF (  RST==0  and  ACK==0  and  SYN==1  ) THEN
	 *     -- The conditions 1., 2. and 3. are all true.
	 *  ELSE
	 *     -- Eighter the condition 1., 2., or 3. is false.
	 *     IF (  RST==1  ) THEN
	 *        -- The condition 1. is false, do drop the packet
	 *        DROP PACKET;
	 *     ELSE
	 *        -- Eighter the condition 2., or 3. is false.
	 *        SEND <SEQ=SEG.ACK><CTL=RST>;
	 *     END IF
	 *  END IF
	 */
	
	/*
	 * UNLESS (  RST==0  and  ACK==0  and  SYN==1  ) IS TRUE:
	 */
	if(odp_unlikely( (flags&TCP_LISTEN_MASK) != FNET_TCP_SGT_SYN)){
		/*
		 * IF (  RST==1  ) THEN
		 */
		if(flags&FNET_TCP_SGT_RST){
			return NETPP_DROP;
		}else{
			/*
			 * ELSE:
			 *    SEND <SEQ=SEG.ACK><CTL=RST>
			 */
			return fastnet_tcp_output_flags(pkt,key,odp_be_to_cpu_32(th->ack_number),0,FNET_TCP_SGT_RST);
		}
	}
	
	sock = fastnet_tcp_allocate();
	if(odp_unlikely(sock==ODP_BUFFER_INVALID)) return NETPP_DROP;
	
	pcb = odp_buffer_addr(sock);
	
	/*
	 * Copy the PCB Fragments.
	 */
	pcb->rcv = parent_pcb->rcv;
	pcb->snd = parent_pcb->snd;
	
	/*
	 * Set the Address pair to the PCB and Initialize it (hash & reference count).
	 */
	((fastnet_sockstruct_t*) pcb)->key = *key;
	((fastnet_sockstruct_t*) pcb)->type_tag = IP_PROTOCOL_TCP;
	fastnet_socket_construct(sock);
	
	/*
	 * Set RCV.NXT to SEG.SEQ+1, IRS is set to SEG.SEQ and any other
	 * control or text should be queued for processing later.  ISS
	 * should be selected and a SYN segment sent of the form:
	 * 
	 * <SEQ=ISS><ACK=RCV.NXT><CTL=SYN,ACK>
	 *
	 * SND.NXT is set to ISS+1 and SND.UNA to ISS.  The connection
	 * state should be changed to SYN-RECEIVED.  Note that any other
	 * incoming control or data (combined with SYN) will be processed
	 * in the SYN-RECEIVED state, but processing of SYN and ACK should
	 * not be repeated.  If the listen was not fully specified (i.e.,
	 * the foreign socket was not fully specified), then the
	 * unspecified fields should be filled in now.
	 */
	iss          = random_syn;
	seg_seq      = odp_be_to_cpu_32(th->sequence_number);
	
	pcb->rcv.nxt = seg_seq+1;
	pcb->irs     = seg_seq;
	pcb->iss     = iss;
	pcb->snd.nxt = iss+1;
	pcb->snd.una = iss;
	pcb->state   = SYN_RECEIVED;
	
	/*
	 * Prevent a Reset from being send.
	 */
	*send_rst = 0;
	
	/*
	 * Insert socket into he socket table.
	 */
	fastnet_socket_insert(sock);
	
	/* Drop socket reference. */
	fastnet_socket_put(sock);
	
	/*
	 * SEND <SEQ=ISS><ACK=RCV.NXT><CTL=SYN,ACK>
	 */
	return fastnet_tcp_output_flags(pkt,key,
		/*SEQ=*/ odp_be_to_cpu_32(iss),
		/*ACK=*/ odp_be_to_cpu_32(seg_seq+1),
		/*CTL=*/ FNET_TCP_SGT_SYN | FNET_TCP_SGT_ACK);
}


static
netpp_retcode_t fastnet_tcp_input_ll(odp_packet_t pkt, fastnet_socket_t sock, socket_key_t *key){
	netpp_retcode_t ret = NETPP_DROP;
	int send_rst = 0;
	fastnet_tcp_pcb_t* pcb = odp_buffer_addr(sock);
	
	if(odp_unlikely( (((fastnet_sockstruct_t*) pcb)->type_tag) != IP_PROTOCOL_TCP))
		return NETPP_DROP;
	
	odp_ticketlock_lock(&(pcb->lock));
	
	switch(pcb->state){
	case LISTEN:
		odp_ticketlock_unlock(&(pcb->lock));
		ret = fastnet_tcp_input_listen(pkt,pcb,key,&send_rst);
		if(odp_unlikely(send_rst)){
			// fastnet_tcp_output_flags(ODP_PACKET_INVALID,key,odp_be_to_cpu_32(th->ack_number),0,FNET_TCP_SGT_RST);
		}
		return ret;
	}
	odp_ticketlock_unlock(&(pcb->lock));
	
	return ret;
}



netpp_retcode_t fastnet_tcp_input(odp_packet_t pkt) {
	socket_key_t key;
	
	/*
	 * Check checksum.
	 */
	if(odp_unlikely(fastnet_tcpudp_input_checksum(pkt,IP_PROTOCOL_TCP)!=0)) return NETPP_DROP;
	fastnet_socket_key_obtain(pkt,&key);
	key.layer4_version = IP_PROTOCOL_TCP;
	
	fnet_tcp_header_t* th = fastnet_safe_l3(pkt,sizeof(fnet_tcp_header_t));
	if(odp_unlikely(th==NULL)) return NETPP_DROP;
	
	NET_LOG("TCP segment: %d->%d\n",(int)odp_be_to_cpu_16(th->source_port),(int)odp_be_to_cpu_16(th->destination_port));
	return NETPP_DROP;
}


