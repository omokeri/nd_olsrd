
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

/* System includes */
#include <stddef.h> /* NULL */
#include <assert.h> /* assert() */
#include <stdlib.h> /* EXIT_FAILURE  */

/* OLSRD includes */
#include "defs.h" /* MAXMESSAGESIZE */
#include "olsr_protocol.h" /* OLSR_HEADERSIZE */
#include "log.h" /* olsr_syslog() */
#include "olsr.h" /* olsr_exit() */
#include "ipcalc.h" /* ipequal() */
#include "net_olsr.h" /* net_outbuffer_bytes_left() */
#include "gateway.h" /* olsr_modifiy_inetgw_netmask() */
#include "build_msg.h"

#define BMSG_DBGLVL 5

#define OLSR_IPV4_HDRSIZE          12
#define OLSR_IPV6_HDRSIZE          24

#define OLSR_HELLO_IPV4_HDRSIZE    (OLSR_IPV4_HDRSIZE + 4)
#define OLSR_HELLO_IPV6_HDRSIZE    (OLSR_IPV6_HDRSIZE + 4)
#define OLSR_TC_IPV4_HDRSIZE       (OLSR_IPV4_HDRSIZE + 4)
#define OLSR_TC_IPV6_HDRSIZE       (OLSR_IPV6_HDRSIZE + 4)
#define OLSR_MID_IPV4_HDRSIZE      OLSR_IPV4_HDRSIZE
#define OLSR_MID_IPV6_HDRSIZE      OLSR_IPV6_HDRSIZE
#define OLSR_HNA_IPV4_HDRSIZE      OLSR_IPV4_HDRSIZE
#define OLSR_HNA_IPV6_HDRSIZE      OLSR_IPV6_HDRSIZE

struct network_interface;

static void check_buffspace(int msgsize, int buffsize, const char *type);

/* All these functions share this buffer */

static uint32_t msg_buffer_align[(MAXMESSAGESIZE - OLSR_HEADERSIZE)/sizeof(uint32_t) + 1];
static uint8_t *msg_buffer = (uint8_t *)msg_buffer_align;

static uint32_t send_empty_tc;          /* TC empty message sending */

/* Prototypes for internal functions */

/* IPv4 */
static bool serialize_mid4(struct network_interface *);
static bool serialize_hna4(struct network_interface *);

/* IPv6 */
static bool serialize_mid6(struct network_interface *);
static bool serialize_hna6(struct network_interface *);

/**
 * Set the timer that controls the generation of
 * empty TC messages
 */
void
set_empty_tc_timer(uint32_t empty_tc_new)
{
  send_empty_tc = empty_tc_new;
}

/**
 * Get the timer that controls the generation of
 * empty TC messages
 */
uint32_t
get_empty_tc_timer(void)
{
  return send_empty_tc;
}

/**
 *Build a MID message to the outputbuffer
 *
 *@param ifn use this network interfaces address as main address
 *
 *@return true if there is a packet to be sent, otherwise false
 */

bool
queue_mid(struct network_interface * ifp)
{
#ifdef DEBUG
  OLSR_PRINTF(BMSG_DBGLVL, "Building MID on %s\n-------------------\n", ifp->int_name);
#endif

  switch (olsr_cnf->ip_version) {
  case (AF_INET):
    return serialize_mid4(ifp);
  case (AF_INET6):
    return serialize_mid6(ifp);
  }
  return false;
}

/**
 *Builds a HNA message in the outputbuffer
 *<b>NB! Not internal packetformat!</b>
 *
 *@param ifp the network interface to send on
 *
 *@return true if there is a packet to be sent, otherwise false
 */
bool
queue_hna(struct network_interface * ifp)
{
#ifdef DEBUG
  OLSR_PRINTF(BMSG_DBGLVL, "Building HNA on %s\n-------------------\n", ifp->int_name);
#endif

  switch (olsr_cnf->ip_version) {
  case (AF_INET):
    return serialize_hna4(ifp);
  case (AF_INET6):
    return serialize_hna6(ifp);
  }
  return false;
}

/*
 * Protocol specific versions
 */

static void
check_buffspace(int msgsize, int buffsize, const char *type)
{
  if (msgsize > buffsize) {
    OLSR_PRINTF(1, "%s build, outputbuffer to small(%d/%u)!\n", type, msgsize, buffsize);
    olsr_syslog(OLSR_LOG_ERR, "%s build, outputbuffer to small(%d/%u)!\n", type, msgsize, buffsize);
    olsr_exit(__func__, EXIT_FAILURE);
  }
}

/**
 *IP version 4
 *
 *@param ifp the network interface to send the message on
 *
 *@return true if there is a packet to be sent, otherwise false
 */
static bool
serialize_mid4(struct network_interface *ifp)
{
  uint16_t remainsize, curr_size, needsize;

  /* Size needed: one IP address per network interface */
  uint16_t nBytesPerMidDeclaration = olsr_cnf->ipsize;

  /* Pointers into packet */
  union pkt_olsr_message *p_msg;
  uint32_t *p_addr;

  struct network_interface *ifs;

  assert(olsr_cnf->ip_version == AF_INET);

  /* No need to send an MID message if:
   * - no network interface passed,
   * - there are no network interfaces running the OLSR protocol, or
   * - there is only one network interface and its IP address is the main IP address */
  if (ifp == NULL
      || ifnet == NULL
      || (ifnet->int_next == NULL && ipequal(&olsr_cnf->main_addr, &ifnet->ip_addr))) {
    return false;
  }

  remainsize = net_outbuffer_bytes_left(ifp);

  p_msg = (union pkt_olsr_message *)msg_buffer;

  curr_size = OLSR_MID_IPV4_HDRSIZE;

  /* Calculate size needed for MID message: one IP address per network interface */
  needsize = curr_size;
  for (ifs = ifnet; ifs != NULL; ifs = ifs->int_next) {
    needsize += nBytesPerMidDeclaration;
  }

  /* Send pending packet if not enough space in buffer */
  if (needsize > remainsize) {
    net_output(ifp);
    remainsize = net_outbuffer_bytes_left(ifp);
  }
  check_buffspace(curr_size, remainsize, "MID");

  /* Serialize message according to RFC 3626 par. 3.3.  Packet Format */

  /* Fill message header */
  p_msg->v4.msgtype = MID_MESSAGE;
  p_msg->v4.vtime = ifp->valtimes.mid;
  /* p_msg->v4.msgsize is filled at the end of this function */

  /* Set main (first) address */
  p_msg->v4.originator = olsr_cnf->main_addr.v4.s_addr;
  p_msg->v4.ttl = MAX_TTL;
  p_msg->v4.hopcnt = 0;
  /* p_msg->v4.seqno is filled at the end of this function */

  p_addr = p_msg->v4.message.mid.mid_addresses;

  /* See RFC 3626 par. 5.1.  MID Message Format . */

  for (ifs = ifnet; ifs != NULL; ifs = ifs->int_next) {

#ifdef DEBUG
    struct ipaddr_str buf;
#endif

    /* Don't add the main address... it's already there */
    if (ipequal(&olsr_cnf->main_addr, &ifs->ip_addr)) {
      continue;
    }

    if ((curr_size + nBytesPerMidDeclaration) > remainsize) {
      /* Only add MID message if it contains data */
      if (curr_size > OLSR_MID_IPV4_HDRSIZE) {
#ifdef DEBUG
        OLSR_PRINTF(BMSG_DBGLVL, "Sending partial(size: %d, buff left:%d)\n", curr_size, remainsize);
#endif
        /* Complete the header */
        p_msg->v4.msgsize = htons(curr_size);
        p_msg->v4.seqno = htons(get_msg_seqno());

        /* Send partial packet */
        net_outbuffer_push(ifp, msg_buffer, curr_size);

        /* Re-initialize */
        p_addr = p_msg->v4.message.mid.mid_addresses;
        curr_size = OLSR_MID_IPV4_HDRSIZE;
      }
      net_output(ifp);
      remainsize = net_outbuffer_bytes_left(ifp);
      check_buffspace(curr_size, remainsize, "MID2");
    }
#ifdef DEBUG
    OLSR_PRINTF(BMSG_DBGLVL, "\t%s(%s)\n", olsr_ip_to_string(&buf, &ifs->ip_addr), ifs->int_name);
#endif

    /* Serialize one OLSR Interface Address acc. RFC 3626 par. 5.1. */
    *p_addr = ifs->ip_addr.v4.s_addr;
    p_addr++;

    curr_size += nBytesPerMidDeclaration;
  }

  /* Complete the header */
  p_msg->v4.seqno = htons(get_msg_seqno());
  p_msg->v4.msgsize = htons(curr_size);

  //printf("Sending MID (%d bytes)...\n", outputsize);
  if (curr_size > OLSR_MID_IPV4_HDRSIZE) {
    net_outbuffer_push(ifp, msg_buffer, curr_size);
  }

  return true;
}

/**
 *IP version 6
 *
 *@param ifp the network interface to send the message on
 *
 *@return true if there is a packet to be sent, otherwise false
 *
 *Note: not RFC-compliant
 */
static bool
serialize_mid6(struct network_interface *ifp)
{
  uint16_t remainsize, curr_size, needsize;

  /* Size needed: one IP address per network interface */
  uint16_t nBytesPerMidDeclaration = olsr_cnf->ipsize;

  /* Pointers into packet */
  union pkt_olsr_message *p_msg;
  struct in6_addr *p_addr;

  struct network_interface *ifs;

  assert(olsr_cnf->ip_version == AF_INET6);

  /* No need to send an MID message if:
   * - no network interface passed,
   * - there are no network interfaces running the OLSR protocol, or
   * - there is only one network interface and its IP address is the main IP address */
  if (ifp == NULL
      || ifnet == NULL
      || (ifnet->int_next == NULL && ipequal(&olsr_cnf->main_addr, &ifnet->ip_addr))) {
    return false;
  }

  remainsize = net_outbuffer_bytes_left(ifp);

  curr_size = OLSR_MID_IPV6_HDRSIZE;

  /* Calculate size needed for MID message: one IP address per network interface */
  needsize = curr_size;
  for (ifs = ifnet; ifs != NULL; ifs = ifs->int_next) {
    needsize += nBytesPerMidDeclaration;
  }

  /* Send pending packet if not enough space in buffer */
  if (needsize > remainsize) {
    net_output(ifp);
    remainsize = net_outbuffer_bytes_left(ifp);
  }
  check_buffspace(curr_size, remainsize, "MID");

  p_msg = (union pkt_olsr_message *)msg_buffer;

  /* Serialize message according to RFC 3626 par. 3.3.  Packet Format */

  /* Fill message header */
  p_msg->v6.msgtype = MID_MESSAGE;
  p_msg->v6.vtime = ifp->valtimes.mid;
  /* p_msg->v6.msgsize is filled at the end of this function */

  /* Set main (first) address */
  p_msg->v6.originator = olsr_cnf->main_addr.v6;
  p_msg->v6.ttl = MAX_TTL;
  p_msg->v6.hopcnt = 0;
  /* p_msg->v6.seqno is filled at the end of this function */

  p_addr = p_msg->v6.message.mid.mid_addresses;

  /* See RFC 3626 par. 5.1.  MID Message Format . */

  for (ifs = ifnet; ifs != NULL; ifs = ifs->int_next) {

#ifdef DEBUG
    struct ipaddr_str buf;
#endif

    /* Don't add the main address... it's already there */
    if (ipequal(&olsr_cnf->main_addr, &ifs->ip_addr)) {
      continue;
    }

    if ((curr_size + nBytesPerMidDeclaration) > remainsize) {
      /* Only add MID message if it contains data */
      if (curr_size > OLSR_MID_IPV6_HDRSIZE) {
#ifdef DEBUG
        OLSR_PRINTF(BMSG_DBGLVL, "Sending partial(size: %d, buff left:%d)\n", curr_size, remainsize);
#endif
        /* Complete the header */
        p_msg->v6.msgsize = htons(curr_size);
        p_msg->v6.seqno = htons(get_msg_seqno());

        /* Send partial packet */
        net_outbuffer_push(ifp, msg_buffer, curr_size);

        /* Re-initialize */
        p_addr = p_msg->v6.message.mid.mid_addresses;
        curr_size = OLSR_MID_IPV6_HDRSIZE;
      }
      net_output(ifp);
      remainsize = net_outbuffer_bytes_left(ifp);
      check_buffspace(curr_size + nBytesPerMidDeclaration, remainsize, "MID2");
    }
#ifdef DEBUG
    OLSR_PRINTF(BMSG_DBGLVL, "\t%s(%s)\n", olsr_ip_to_string(&buf, &ifs->ip_addr), ifs->int_name);
#endif

    /* Serialize one OLSR Interface Address acc. RFC 3626 par. 5.1. */
    *p_addr = ifs->ip_addr.v6;
    p_addr++;

    curr_size += nBytesPerMidDeclaration;
  }

  /* Complete the header */
  p_msg->v6.msgsize = htons(curr_size);
  p_msg->v6.seqno = htons(get_msg_seqno()); /* seqnumber */

  //printf("Sending MID (%d bytes)...\n", outputsize);
  if (curr_size > OLSR_MID_IPV6_HDRSIZE) {
    net_outbuffer_push(ifp, msg_buffer, curr_size);
  }

  return true;
}

/**
 *IP version 4
 *
 *@param ifp the network interface to send the message on
 *
 *@return true if there is a packet to be sent, otherwise false
 */
static bool
serialize_hna4(struct network_interface *ifp)
{
  uint16_t remainsize, curr_size, needsize;

  /* Size needed: two IP addresses per HNA declaration */
  uint16_t nBytesPerHnaDeclaration = olsr_cnf->ipsize * 2;

  /* Pointers into packet */
  union pkt_olsr_message *p_msg;
  struct pkt_hna_pair *p_pair;

  struct ip_prefix_list *h;

  assert(olsr_cnf->ip_version == AF_INET);

  /* No need to send an HNA message if:
   * - no network interface passed,
   * - there are no HNA entries configured */
  if (ifp == NULL || olsr_cnf->hna_entries == NULL) {
    return false;
  }

  remainsize = net_outbuffer_bytes_left(ifp);

  curr_size = OLSR_HNA_IPV4_HDRSIZE;

  /* Calculate size needed for HNA message */
  needsize = curr_size;
  for (h = olsr_cnf->hna_entries; h != NULL; h = h->next) {
    needsize += nBytesPerHnaDeclaration;
  }

  /* Send pending packet if not enough space in buffer */
  if (needsize > remainsize) {
    net_output(ifp);
    remainsize = net_outbuffer_bytes_left(ifp);
  }
  check_buffspace(curr_size, remainsize, "HNA");

  p_msg = (union pkt_olsr_message *)msg_buffer;

  /* Serialize message according to RFC 3626 par. 3.3.  Packet Format */

  /* Fill message header */
  p_msg->v4.msgtype = HNA_MESSAGE;
  p_msg->v4.vtime = ifp->valtimes.hna;
  /* p_msg->v4.msgsize is filled at the end of this function */

  /* Set main (first) address */
  p_msg->v4.originator = olsr_cnf->main_addr.v4.s_addr;
  p_msg->v4.ttl = MAX_TTL;
  p_msg->v4.hopcnt = 0;
  /* p_msg->v4.seqno is filled at the end of this function */

  p_pair = p_msg->v4.message.hna.hna_pairs;

  /* See RFC 3626 par. 12.1.  HNA Message Format . */

  for (h = olsr_cnf->hna_entries; h != NULL; h = h->next) {
    union olsr_ip_addr ip_addr;
    if ((curr_size + nBytesPerHnaDeclaration) > remainsize) {
      /* Only add HNA message if it contains data */
      if (curr_size > OLSR_HNA_IPV4_HDRSIZE) {
#ifdef DEBUG
        OLSR_PRINTF(BMSG_DBGLVL, "Sending partial(size: %d, buff left:%d)\n", curr_size, remainsize);
#endif
        /* Complete the header */
        p_msg->v4.seqno = htons(get_msg_seqno());
        p_msg->v4.msgsize = htons(curr_size);

        /* Send partial packet */
        net_outbuffer_push(ifp, msg_buffer, curr_size);

        /* Re-initialize */
        p_pair = p_msg->v4.message.hna.hna_pairs;
        curr_size = OLSR_HNA_IPV4_HDRSIZE;
      }
      net_output(ifp);
      remainsize = net_outbuffer_bytes_left(ifp);
      check_buffspace(curr_size + nBytesPerHnaDeclaration, remainsize, "HNA2");
    }
#ifdef DEBUG
    OLSR_PRINTF(BMSG_DBGLVL, "\tNet: %s\n", olsr_ip_prefix_to_string(&h->net));
#endif

    olsr_prefix_to_netmask(&ip_addr, h->net.prefix_len);
#ifdef LINUX_NETLINK_ROUTING
    if (olsr_cnf->smart_gw_active && is_prefix_inetgw(&h->net)) {
      /* this is the default route, overwrite it with the smart gateway */
      olsr_modifiy_inetgw_netmask(&ip_addr, h->net.prefix_len);
    }
#endif

    /* Serialize one Network Address - Netmask pair acc. RFC 3626 par. 12.1.  */
    p_pair->addr = h->net.prefix.v4.s_addr;
    p_pair->netmask = ip_addr.v4.s_addr;
    p_pair++;

    curr_size += nBytesPerHnaDeclaration;
  }

  /* Complete the header */
  p_msg->v4.seqno = htons(get_msg_seqno());
  p_msg->v4.msgsize = htons(curr_size);

  net_outbuffer_push(ifp, msg_buffer, curr_size);

  return true;
}

/**
 *IP version 6
 *
 *@param ifp the network interface to send the message on
 *
 *@return true if there is a packet to be sent, otherwise false
 *
 *Note: not RFC-compliant
 */
static bool
serialize_hna6(struct network_interface *ifp)
{
  uint16_t remainsize, curr_size, needsize;

  /* Size needed: two IP addresses per HNA declaration */
  uint16_t nBytesPerHnaDeclaration = olsr_cnf->ipsize * 2;

  /* Pointers into packet */
  union pkt_olsr_message *p_msg;
  struct pkt_hna_pair_ipv6 *p_pair;

  struct ip_prefix_list *h;

  assert(olsr_cnf->ip_version == AF_INET6);

  /* No need to send an HNA message if:
   * - no network interface passed,
   * - there are no HNA entries configured */
  if (ifp == NULL || olsr_cnf->hna_entries == NULL) {
    return false;
  }

  remainsize = net_outbuffer_bytes_left(ifp);

  curr_size = OLSR_HNA_IPV6_HDRSIZE;

  /* Calculate size needed for HNA message */
  needsize = curr_size;
  for (h = olsr_cnf->hna_entries; h != NULL; h = h->next) {
    needsize += nBytesPerHnaDeclaration;
  }

  h = olsr_cnf->hna_entries;

  /* Send pending packet if not enough space in buffer */
  if (needsize > remainsize) {
    net_output(ifp);
    remainsize = net_outbuffer_bytes_left(ifp);
  }
  check_buffspace(curr_size, remainsize, "HNA");

  p_msg = (union pkt_olsr_message *)msg_buffer;

  /* Serialize message according to RFC 3626 par. 3.3.  Packet Format */

  /* Fill message header */
  p_msg->v6.msgtype = HNA_MESSAGE;
  p_msg->v6.vtime = ifp->valtimes.hna;
  /* p_msg->v6.msgsize is filled at the end of this function */

  /* Set main (first) address */
  p_msg->v6.originator = olsr_cnf->main_addr.v6;
  p_msg->v6.ttl = MAX_TTL;
  p_msg->v6.hopcnt = 0;
  /* p_msg->v6.seqno is filled at the end of this function */

  p_pair = p_msg->v6.message.hna.hna_pairs;

  /* See RFC 3626 par. 12.1.  HNA Message Format . */

  for (h = olsr_cnf->hna_entries; h != NULL; h = h->next) {
    union olsr_ip_addr tmp_netmask;
    if ((curr_size + nBytesPerHnaDeclaration) > remainsize) {
      /* Only add HNA message if it contains data */
      if (curr_size > OLSR_HNA_IPV6_HDRSIZE) {
#ifdef DEBUG
        OLSR_PRINTF(BMSG_DBGLVL, "Sending partial(size: %d, buff left:%d)\n", curr_size, remainsize);
#endif
        /* Complete the header */
        p_msg->v6.seqno = htons(get_msg_seqno());
        p_msg->v6.msgsize = htons(curr_size);

        /* Send partial packet */
        net_outbuffer_push(ifp, msg_buffer, curr_size);

        /* Re-initialize */
        p_pair = p_msg->v6.message.hna.hna_pairs;
        curr_size = OLSR_HNA_IPV6_HDRSIZE;
      }
      net_output(ifp);
      remainsize = net_outbuffer_bytes_left(ifp);
      check_buffspace(curr_size + nBytesPerHnaDeclaration, remainsize, "HNA2");
    }
#ifdef DEBUG
    OLSR_PRINTF(BMSG_DBGLVL, "\tNet: %s\n", olsr_ip_prefix_to_string(&h->net));
#endif
    olsr_prefix_to_netmask(&tmp_netmask, h->net.prefix_len);
#ifdef LINUX_NETLINK_ROUTING
    if (olsr_cnf->smart_gw_active && is_prefix_inetgw(&h->net)) {
      /* this is the default gateway, so overwrite it with the smart one */
      olsr_modifiy_inetgw_netmask(&tmp_netmask, h->net.prefix_len);
    }
#endif

    /* Serialize one Network Address - Netmask pair acc. RFC 3626 par. 12.1.  */
    p_pair->addr = h->net.prefix.v6;
    p_pair->netmask = tmp_netmask.v6;
    p_pair++;

    curr_size += nBytesPerHnaDeclaration;
  }

  /* Complete the header */
  p_msg->v6.msgsize = htons(curr_size);
  p_msg->v6.seqno = htons(get_msg_seqno());

  net_outbuffer_push(ifp, msg_buffer, curr_size);

  return true;

}

/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
