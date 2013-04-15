
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004, Andreas Tonnesen(andreto@olsr.org)
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Visit http://www.olsr.org for more information.
 *
 * If you find this software useful feel free to make a donation
 * to the project. For more information see the website or contact
 * the copyright holders.
 *
 */

/*
 *Values and packet formats as proposed in RFC3626 and misc. values and
 *data structures used by the olsr.org OLSR daemon.
 */

#ifndef _OLSR_PROTOCOL_H
#define	_OLSR_PROTOCOL_H

/* OLSRD includes */
#include "olsr_types.h" /* uint8_t, uint16_t, uint32_t */

#define OLSR_HEADERSIZE (sizeof(uint16_t) + sizeof(uint16_t))

/* TODO: not so nice to have these numbers without underlying calculation */
#define OLSR_MSGHDRSZ_IPV4 12
#define OLSR_MSGHDRSZ_IPV6 24

/*
 *Emission Intervals
 */
#define HELLO_INTERVAL        2
#define REFRESH_INTERVAL      2
#define TC_INTERVAL           5
#define MID_INTERVAL          TC_INTERVAL
#define HNA_INTERVAL          TC_INTERVAL

/* Emission Jitter */
#define HELLO_JITTER         25 /* percent */
#define HNA_JITTER           25 /* percent */
#define MID_JITTER           25 /* percent */
#define TC_JITTER            25 /* percent */

/*
 * Default Holding Time (for large scale community networks)
 */
#define NEIGHB_HOLD_TIME      10 * REFRESH_INTERVAL
#define TOP_HOLD_TIME         60 * TC_INTERVAL
#define DUP_HOLD_TIME         30
#define MID_HOLD_TIME         60 * MID_INTERVAL
#define HNA_HOLD_TIME         60 * HNA_INTERVAL

/*
 *Message Types (RFC 3626 par 18.4.)
 */
#define HELLO_MESSAGE         1
#define TC_MESSAGE            2
#define MID_MESSAGE           3
#define HNA_MESSAGE           4
#define MAX_MESSAGE           4

/*
 *Link Types (RFC 3626 par 18.5.)
 */
#define UNSPEC_LINK           0
#define ASYM_LINK             1
#define SYM_LINK              2
#define LOST_LINK             3
#define MAX_LINK              3

#define HIDE_LINK             4 /* Indicates a link that should not be advertised */

/*
 *Neighbor Types (RFC 3626 par 18.6.)
 */
#define NOT_NEIGH             0
#define SYM_NEIGH             1
#define MPR_NEIGH             2
#define MAX_NEIGH             2

/*
 *Neighbor status
 */
#define NOT_SYM               0
#define SYM                   1

/*
 *Link Hysteresis
 */
#define HYST_THRESHOLD_HIGH   0.8
#define HYST_THRESHOLD_LOW    0.3
#define HYST_SCALING          0.5

/*
 *Willingness
 */
#define WILL_NEVER            0
#define WILL_LOW              1
#define WILL_DEFAULT          3
#define WILL_HIGH             6
#define WILL_ALWAYS           7

/*
 *Redundancy defaults
 */
#define TC_REDUNDANCY         2
#define MPR_COVERAGE          7

/*
 *Misc. Constants
 */
#define MAXJITTER             HELLO_INTERVAL / 4
#define MAX_TTL               0xff

/*
 *Sequence numbering
 */

/* Seqnos are 16 bit values */

#define MAXVALUE 0xFFFF

/* Macro for checking seqnos "wraparound" */
#define SEQNO_GREATER_THAN(s1, s2)                \
        (((s1 > s2) && (s1 - s2 <= (MAXVALUE/2))) \
     || ((s2 > s1) && (s2 - s1 > (MAXVALUE/2))))

/*
 * Macros for creating and extracting the neighbor
 * and link type information from 8bit link_code
 * data as passed in HELLO messages
 */

#define CREATE_LINK_CODE(status, link) (link | (status<<2))

#define EXTRACT_NEIGHBOR_TYPE(link_code) ((link_code & 0xC)>>2)

#define EXTRACT_LINK_TYPE(link_code) (link_code & 0x3)

/***********************************************
 *           OLSR packet definitions           *
 ***********************************************/

/*
 * Multiple Interface Declaration message
 * See RFC 3626 par. 5.1.  MID Message Format
 */

/* MID message for IPv4 - RFC-compliant */
struct pkt_mid_msg {
  uint32_t mid_addresses[1]; /* List of OLSR Interface Addresses */
} __attribute__ ((packed));

/* MID message for IPv6 - not RFC-compliant */
struct pkt_mid_msg_ipv6 {
  struct in6_addr mid_addresses[1]; /* List of OLSR Interface Addresses */
} __attribute__ ((packed));

/*
 * Host and Network Association message
 * See RFC 3626 par. 12.1.  HNA Message Format
 */

/* HNA message for IPv4 - RFC-compliant */

struct pkt_hna_pair {
  uint32_t addr; /* Network Address */
  uint32_t netmask;
} __attribute__ ((packed));

struct pkt_hna_msg {
  struct pkt_hna_pair hna_pairs[1]; /* List of HNA pairs */
} __attribute__ ((packed));

/* HNA message for IPv6 - not RFC-compliant */

struct pkt_hna_pair_ipv6 {
  struct in6_addr addr; /* Network Address */
  struct in6_addr netmask;
} __attribute__ ((packed));

struct pkt_hna_msg_ipv6 {
  struct pkt_hna_pair_ipv6 hna_pairs[1]; /* List of HNA pairs */
} __attribute__ ((packed));

/*
 * Generic OLSR message (several can exist in one OLSR packet)
 * See also RFC 3626 par. 3.3.  Packet Format
 */

/* OLSR message for IPv4 - RFC-compliant */
struct pkt_olsr_message_v4 {
  uint8_t msgtype; /* Message Type */
  uint8_t vtime; /* Vtime */
  uint16_t msgsize; /* Message Size */
  uint32_t originator; /* Originator Address */
  uint8_t ttl; /* Time To Live */
  uint8_t hopcnt; /* Hop Count */
  uint16_t seqno; /* Message Sequence Number */

  union {
    struct pkt_hna_msg hna;
    struct pkt_mid_msg mid;
  } message;

} __attribute__ ((packed));

/* OLSR message for IPv6 - not RFC-compliant */
struct pkt_olsr_message_v6 {
  uint8_t msgtype; /* Message Type */
  uint8_t vtime; /* Vtime */
  uint16_t msgsize; /* Message Size */
  struct in6_addr originator; /* Originator Address */
  uint8_t ttl; /* Time To Live */
  uint8_t hopcnt; /* Hop Count */
  uint16_t seqno; /* Message Sequence Number */

  union {
    struct pkt_hna_msg_ipv6 hna;
    struct pkt_mid_msg_ipv6 mid;
  } message;

} __attribute__ ((packed));

union pkt_olsr_message {
  struct pkt_olsr_message_v4 v4;
  struct pkt_olsr_message_v6 v6;
} __attribute__ ((packed));

/*
 * Generic OLSR packet
 * See also RFC 3626 par. 3.3.  Packet Format .
 */

/* OLSR packet for IPv4 - RFC-compliant */
struct pkt_olsr_packet_v4 {
  uint16_t packlen; /* Packet Length */
  uint16_t seqno; /* Packet Sequence Number */
  struct pkt_olsr_message_v4 msg[1]; /* variable messages */
} __attribute__ ((packed));

/* OLSR packet for IPv6 - not RFC-compliant */
struct pkt_olsr_packet_v6 {
  uint16_t packlen; /* Packet Length */
  uint16_t seqno; /* Packet Sequence Number */
  struct pkt_olsr_message_v6 msg[1]; /* variable messages */
} __attribute__ ((packed));

union pkt_olsr_packet {
  struct pkt_olsr_packet_v4 v4;
  struct pkt_olsr_packet_v6 v6;
} __attribute__ ((packed));

#endif /* _OLSR_PROTOCOL_H */

/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
