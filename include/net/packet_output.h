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
#pragma once
#include <net/nif.h>
#include <net/types.h>

netpp_retcode_t fastnet_pkt_output(odp_packet_t pkt,nif_t *dest);

netpp_retcode_t fastnet_pkt_loopback(odp_packet_t pkt,nif_t *dest);

/*
 * Transmit a chain of ip-packets with the given source and destination address.
 *
 * ARGS:
 *   pkt  the packet chain.
 *   nif  the network interface to send it through.
 *   src  source MAC address.
 *   dst  target MAC address.
 */
void fastnet_ip_arp_transmit(odp_packet_t pkt,nif_t *nif,uint64_t src,uint64_t dst);

/*
 * Transmit a chain of ip6-packets with the given source and destination address.
 *
 * ARGS:
 *   pkt  the packet chain.
 *   nif  the network interface to send it through.
 *   src  source MAC address.
 *   dst  target MAC address.
 */
void fastnet_ip6_nd6_transmit(odp_packet_t pkt,nif_t *nif,uint64_t src,uint64_t dst);

