#ifndef _LINKPROBE_PACKET_H
#define _LINKPROBE_PACKET_H

/*
 * OLSR Link Probe plugin.
 * Copyright (c) 2008 Erik Tromp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met:
 *
 * * Redistributions of source code must retain the above copyright 
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright 
 *   notice, this list of conditions and the following disclaimer in 
 *   the documentation and/or other materials provided with the 
 *   distribution.
 * * Neither the name of olsr.org, olsrd nor the names of its 
 *   contributors may be used to endorse or promote products derived 
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY 
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE 
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED 
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* -------------------------------------------------------------------------
 * File       : Packet.h
 * Description: Probe packet definitions and declarations
 *
 * ------------------------------------------------------------------------- */

/* System includes */
#include <sys/types.h> /* u_int8_t, u_int16_t, u_int32_t */
#include <net/if.h> /* IFNAMSIZ, IFHWADDRLEN */
#include <netinet/ip.h> /* struct ip */
#include <netinet/udp.h> /* struct udp */

/* Offsets and sizes into IP-ethernet packets */
#define ETH_TYPE_OFFSET (2*IFHWADDRLEN)
#define ETH_TYPE_LEN 2
#define ETH_HDR_SIZE (ETH_TYPE_OFFSET + ETH_TYPE_LEN)
#define IP_HDR_OFFSET ETH_HDR_SIZE
#define IP_HDR_SIZE sizeof(struct ip)
#define UDP_HDR_SIZE sizeof(struct udphdr)

enum TProbeType
{
  PtUnicastSmall = 0,
  PtUnicastLarge,

  /* Keep this at the end of the enum */
  PtNTypes,
  PtLast = PtNTypes - 1,
  PtFirst = 0
};

struct TProbePacket
{
  u_int8_t phase; /* request or reply */
  u_int8_t type; /* small or large */
  u_int16_t sentSize;
  u_int32_t daddr;
  u_int32_t id;
  struct timeval sentAt;
  u_int32_t myEstimatedLinkSpeed; /* Mbits/sec */
  /* padding follows here */
} __attribute__((__packed__));

/* Absolute minimum probe packet length */
#define	PROBE_MINLEN sizeof(struct TProbePacket)

/* Number of bits in a small probe packet */
#define SMALL_PROBE_BIT_SIZE ((PROBE_MINLEN + ETH_HDR_SIZE + IP_HDR_SIZE + UDP_HDR_SIZE) * 8.0)

#define PT_PROBE_REPLY 0
#define PT_PROBE_REQUEST 1

#endif /* _LINKPROBE_PACKET_H */
