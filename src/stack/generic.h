/*
 *
 * Copyright 2016 Simon Schmidt
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#pragma once

#include "typedefs.h"

odp_packet_t fstn_alloc_packet(thr_s* thr);

static inline int fstn_packet_add_l2(odp_packet_t pkt,uint32_t pre){
	uint32_t l3,l4;
	l3 = odp_packet_l3_offset(pkt);
	l4 = odp_packet_l4_offset(pkt);
	if(odp_unlikely(!odp_packet_push_head(pkt,pre)))
		return 0;
	l3+=pre;
	l4+=pre;
	odp_packet_l2_offset_set(pkt,0);
	odp_packet_l3_offset_set(pkt,l3);
	odp_packet_l4_offset_set(pkt,l4);
	return 1;
}
static inline int fstn_packet_cut_l2(odp_packet_t pkt){
	uint32_t l3,l4;
	l3 = odp_packet_l3_offset(pkt);
	l4 = odp_packet_l4_offset(pkt);
	l4-=l3;
	if(odp_unlikely(!odp_packet_pull_head(pkt,l3)))
		return 0;
	odp_packet_l3_offset_set(pkt,0);
	odp_packet_l4_offset_set(pkt,l4);
	return 1;
}

void fstn_trimm_packet(odp_packet_t pkt,uint32_t len);

uint64_t fstn_fnv1a (const uint8_t* pBuffer, const uint8_t* const pBufferEnd)
{
	const uint64_t  MagicPrime = 0x00000100000001b3;
	uint64_t        Hash       = 0xcbf29ce484222325;
	for ( ; pBuffer < pBufferEnd; pBuffer++) 
		Hash = (Hash * MagicPrime) ^ *pBuffer;
	return Hash;
}


