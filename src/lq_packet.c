
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2003, Andreas Tonnesen (andreto@olsr.org)
 *               2004, Thomas Lopatic (thomas@lopatic.de)
 *               2006, for some fixups, sven-ola(gmx.de)
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
#include <stdlib.h> /* free() */

/* OLSRD includes */
#include "olsr_protocol.h" /* OLSR_HEADERSIZE */
#include "scheduler.h" /* TIMED_OUT */
#include "link_set.h" /* link_entry, OLSR_FOR_ALL_LINK_ENTRIES */
#include "neighbor_table.h" /* neighbor_entry */
#include "mpr_selector_set.h" /* get_local_ansn() */
#include "olsr.h" /* get_msg_seqno() */
#include "build_msg.h" /* set_empty_tc_timer() */
#include "net_olsr.h" /* net_outbuffer_bytes_left() */
#include "lq_plugin.h" /* olsr_get_hello_message_type() */
#include "lq_packet.h" /* lq_tc_neighbor */

static uint32_t msg_buffer_aligned[(MAXMESSAGESIZE - OLSR_HEADERSIZE) / sizeof(uint32_t) + 1];
static unsigned char *const msg_buffer = (unsigned char *)msg_buffer_aligned;

/**
 *Build an internal HELLO packet structure for this node. This MUST be done
 *for each network interface.
 *
 *@param lq_hello the lq_hello_message struct to fill with info
 *@param outif the interface to send the packet on - packets
 *       are created individually for each network interface!
 *
 *Note: see RFC 3626 par. 6.2.  HELLO Message Generation
 */
static void
create_lq_hello(struct lq_hello_message *lq_hello, struct network_interface *outif)
{
  struct link_entry *walker;

  /* Initialize the common fields. See also RFC 3626 par. 3.3.  Packet Format . */
  lq_hello->comm.type = olsr_get_hello_message_type();
  lq_hello->comm.vtime = me_to_reltime(outif->valtimes.hello);
  lq_hello->comm.size = 0; /* To be overwritten later during serialization */

  /* Set the main address of this node as the originator */
  lq_hello->comm.orig = olsr_cnf->main_addr;

  lq_hello->comm.ttl = 1;
  lq_hello->comm.hops = 0;

  /* Initialize LQ HELLO message headers fields. See also RFC 3626 par. 6.1.  HELLO Message Format . */
  lq_hello->htime = outif->hello_etime;
  lq_hello->will = olsr_cnf->willingness;

  /* Start with empty list of neighbors */
  lq_hello->neighbors = NULL;

  /* Walk through the link set. For each link, create an lq_hello_neighbor object and
   * add that to the neighbors list of lq_hello. */
  OLSR_FOR_ALL_LINK_ENTRIES(walker) {

    struct lq_hello_neighbor *neighbor;
    struct network_interface *neighborIf = if_ifwithaddr(&walker->local_iface_addr);

    if (neighborIf == NULL)
    {
      /* This is a link that is timing out because the local interface
       * address was changed. Don't advertise this link via the HELLO message. */ 
      continue; /* for */
    }

    // allocate a neighbour entry
    neighbor = olsr_calloc_lq_hello_neighbor("Build LQ_HELLO");

    if (!ipequal(&walker->local_iface_addr, &outif->ip_addr))
    {
      /* This neighbor interface IS NOT visible via the output interface.
       * Set link type to UNSPEC_LINK acc. RFC 3626 par 6.2. (page 31):
       *
       *   For each tuple in the Neighbor Set, for which no
       *   L_neighbor_iface_addr from an associated link tuple has been
       *   advertised by the previous algorithm,  N_neighbor_main_addr is
       *   advertised with:
       *
       *     - Link Type = UNSPEC_LINK,
       *   ... */
      neighbor->link_type = UNSPEC_LINK;
    }
    else
    {
      /* This neighbor interface IS visible via the output interface.
       * Set link type acc. RFC 3626 par. 6.2. section 1:
       *
       *   1    The Link Type set according to the following:
       *   ... */
      neighbor->link_type = lookup_link_type(walker);
    }

    // set the entry's link quality
    olsr_copy_link_lq_into_hello_lq(neighbor, walker);

    /* Set the entry's neighbour type (MPR_NEIGH, SYM_NEIGH or NOT_NEIGH)
     * acc. RFC 3626 par. 6.2. section 2:
     *
     *   2    The Neighbor Type is set according to the following:
     *   ... */
    if (walker->neighbor->is_mpr) {
      neighbor->neigh_type = MPR_NEIGH;
    }
    else if (walker->neighbor->N_status == SYM) {
      neighbor->neigh_type = SYM_NEIGH;
    }
    else if (walker->neighbor->N_status == NOT_SYM) {
      neighbor->neigh_type = NOT_NEIGH;
    }
    else {
      OLSR_PRINTF(0, "Error: neigh_type undefined");
      neighbor->neigh_type = NOT_NEIGH;
    }

    // set the entry's neighbor (remote) interface address
    neighbor->addr = walker->neighbor_iface_addr;

    // Add the neighbor entry to the list
    neighbor->next = lq_hello->neighbors;
    lq_hello->neighbors = neighbor;

  }
  OLSR_FOR_ALL_LINK_ENTRIES_END(walker);
}

void
destroy_lq_hello(struct lq_hello_message *lq_hello)
{
  struct lq_hello_neighbor *walker, *aux;

  // Walk through the list of neighbour entries and free them
  for (walker = lq_hello->neighbors; walker != NULL; walker = aux) {
    aux = walker->next;
    free(walker);
  }

  lq_hello->neighbors = NULL;
}

/**
 *Build an internal TC packet structure for this node.
 *
 *@param lq_tc the tc_message struct to fill with info
 *@param outif the interface to send the packet on - packets
 *       are created individually for each network interface!
 *
 *Note: see RFC 3626 par. 9.  Topology Discovery
 */
static void
create_lq_tc(struct lq_tc_message *lq_tc, struct network_interface *outif)
{
  struct neighbor_entry *walker;
  static int ttl_list[] = { 2, 8, 2, 16, 2, 8, 2, MAX_TTL };

  /* Initialize the common fields. See also RFC 3626 par. 3.3.  Packet Format . */
  lq_tc->comm.type = olsr_get_tc_message_type();
  lq_tc->comm.vtime = me_to_reltime(outif->valtimes.tc);
  lq_tc->comm.size = 0; /* To be overwritten later during serialization */

  lq_tc->comm.orig = olsr_cnf->main_addr;

  /* No fish-eye when RFC-compliant (link quality level 0) */
  if (olsr_cnf->lq_level > 0 && olsr_cnf->lq_fish > 0) {
    if (outif->ttl_index >= (int)(sizeof(ttl_list) / sizeof(ttl_list[0]))) {
      outif->ttl_index = 0;
    }

    lq_tc->comm.ttl = (0 <= outif->ttl_index ? ttl_list[outif->ttl_index] : MAX_TTL);
    outif->ttl_index++;

    OLSR_PRINTF(3, "Creating LQ TC with TTL %d.\n", lq_tc->comm.ttl);
  } else {
    lq_tc->comm.ttl = MAX_TTL;
  }

  lq_tc->comm.hops = 0;

  // initialize LQ TC message headers fields. See also RFC 3626 par. 9.1.  TC Message Format
  lq_tc->from = olsr_cnf->main_addr;
  lq_tc->ansn = get_local_ansn();

  /* Start with empty list of neighbors */
  lq_tc->neighbors = NULL;

  /* Walk through all neighbor entries */
  OLSR_FOR_ALL_NBR_ENTRIES(walker) {

    struct link_entry *lnk;
    struct lq_tc_neighbor *neighbor;

    /* Evaluate TC redundancy. See also RFC 3626 par. 15.1.  TC_REDUNDANCY Parameter . */

    /* TC redundancy 2: Only consider symmetric neighbors.
     * TODO: why advertise only symmetric neighbors? Please add a comment.
     * For now, adhere to the RFC: add all neighbors */
    //if (walker->N_status != SYM) {
    //  continue;
    //}

    /* TC redundancy 1: Only consider MPRs and MPR selectors */
    if (olsr_cnf->tc_redundancy == 1 
        && !walker->is_mpr
        && olsr_lookup_mprs_set(&walker->N_neighbor_main_addr) == NULL) {
      continue;
    }

    /* TC redundancy 0: Only consider MPR selectors */
    if (olsr_cnf->tc_redundancy == 0 &&
        olsr_lookup_mprs_set(&walker->N_neighbor_main_addr) == NULL) {
      continue;
    }

    lnk = get_best_link_to_neighbor(&walker->N_neighbor_main_addr);
    if (lnk == NULL) {
      /* Don't advertise neighbor entries without a corresponding link entry */
      continue;
    }

    if (lnk->link_cost >= LINK_COST_BROKEN) {
      /* Don't advertise neighbors with very low link quality */
      continue;
    }

    /* Allocate a neighbour entry. */
    neighbor = olsr_calloc_lq_tc_neighbor("Build LQ_TC");

    /* Set the entry's main address. */
    neighbor->address = walker->N_neighbor_main_addr;

    /* Set the entry's link quality */
    olsr_copy_link_lq_into_tc_lq(neighbor, lnk);

    /* Add the neighbour entry to the list */

    // TODO: ugly hack until neighbor list is ported to avl tree
    // TODO: Why must the neighbor list be kept sorted? Please add a comment.
    if (lq_tc->neighbors == NULL ||
        avl_comp_default(&lq_tc->neighbors->address, &neighbor->address) > 0)
    {
      neighbor->next = lq_tc->neighbors;
      lq_tc->neighbors = neighbor;
    }
    else
    {
      struct lq_tc_neighbor *last = lq_tc->neighbors, *n = last->next;

      while (n != NULL) {
        if (avl_comp_default(&n->address, &neighbor->address) > 0) {
          break;
        }
        last = n;
        n = n->next;
      }
      neighbor->next = n;
      last->next = neighbor;
    }

    // neighbor->next = lq_tc->neighbors;
    // lq_tc->neighbors = neighbor;

  }
  OLSR_FOR_ALL_NBR_ENTRIES_END(walker);
}

static void
destroy_lq_tc(struct lq_tc_message *lq_tc)
{
  struct lq_tc_neighbor *walker, *aux;

  // Walk through the list of neighbour entries and free them
  for (walker = lq_tc->neighbors; walker != NULL; walker = aux) {
    aux = walker->next;
    free(walker);
  }
}

static size_t
common_size(void)
{
  // return the size of the header shared by all OLSR messages

  return (olsr_cnf->ip_version == AF_INET) ? sizeof(struct pkt_olsr_header_v4) : sizeof(struct pkt_olsr_header_v6);
}

static void
serialize_common(struct olsr_common *comm)
{
  if (olsr_cnf->ip_version == AF_INET) {
    // serialize an IPv4 OLSR message header
    struct pkt_olsr_header_v4 *p_head = (struct pkt_olsr_header_v4 *)ARM_NOWARN_ALIGN(msg_buffer);

    p_head->type = comm->type;
    p_head->vtime = reltime_to_me(comm->vtime);
    p_head->size = htons(comm->size);

    p_head->orig = comm->orig.v4.s_addr;

    p_head->ttl = comm->ttl;
    p_head->hops = comm->hops;
    p_head->seqno = htons(get_msg_seqno());
  } else {
    // serialize an IPv6 OLSR message header
    struct pkt_olsr_header_v6 *p_head = (struct pkt_olsr_header_v6 *)ARM_NOWARN_ALIGN(msg_buffer);

    p_head->type = comm->type;
    p_head->vtime = reltime_to_me(comm->vtime);
    p_head->size = htons(comm->size);

    memcpy(&p_head->orig, &comm->orig.v6.s6_addr, sizeof(p_head->orig));

    p_head->ttl = comm->ttl;
    p_head->hops = comm->hops;
    p_head->seqno = htons(get_msg_seqno());
  }
}

/**
 * Serialize a lq_hello_message structure to the packet buffer.
 *
 * @param lq_hello the lq_hello_message struct containing the info to build the hello packet from.
 * @param outif the network interface to send the message on
 *
 * Note: see also RFC 3626 par. 3.3.  Packet Format and par. 6.1.  HELLO Message Format
 */
static void
serialize_lq_hello(struct lq_hello_message *lq_hello, struct network_interface *outif)
{
  static const int LINK_ORDER[] = { SYM_LINK, UNSPEC_LINK, ASYM_LINK, LOST_LINK };
  size_t rem, size;
//  size_t expected_size = 0;

  /* Pointer into packet buffer */
  struct pkt_lq_hello_info_header *p_info_head;

  unsigned char *buff;
  bool is_first;
  int i;

  // leave space for the OLSR header
  size_t off = common_size();

  /* Initialize the LQ_HELLO header. See also RFC 3626 par 6.1.  HELLO Message Format . */
  struct pkt_lq_hello_header *p_head = (struct pkt_lq_hello_header *)ARM_NOWARN_ALIGN(msg_buffer + off);
  p_head->reserved = htons(0);
  p_head->htime = reltime_to_me(lq_hello->htime);
  p_head->will = lq_hello->will;

  // 'off' is the offset of the byte following the LQ_HELLO header
  off += sizeof(struct pkt_lq_hello_header);

  // our work buffer starts at 'off'...
  buff = msg_buffer + off;

  // ... that's why we start with a 'size' of 0 and subtract 'off' from
  // the remaining bytes in the output buffer
  size = 0;
  rem = net_outbuffer_bytes_left(outif) - off;

  /* Count the number of expected bytes needed in the packet. We want to put all
   * lq_hello data as much as possible into one packet. The goal is to prevent
   * message fragementation which is suspected to result in instable links.
   * TODO: Why is message fragementation suspected to result in instable links?
   * Large networks will have large messages, which will be fragmented anyway. So
   * this means that large networks will always have instable links?
   * Please add a comment. */
//  if (net_output_pending(outif) > 0) {
//    for (i = 0; i <= MAX_NEIGH; i++) {
//      unsigned int j;
//      for (j = 0; j < sizeof(LINK_ORDER) / sizeof(LINK_ORDER[0]); j++) {
//        is_first = true;
//        struct lq_hello_neighbor *neighbor;
//        for (neighbor = lq_hello->neighbors; neighbor != NULL; neighbor = neighbor->next) {
//          if (i == 0 && j == 0) {

              /* TODO: What?? Only increase expected_size if i == 0 and j == 0 ? 
               * Come on, this can't be right */

//            expected_size += olsr_cnf->ipsize + olsr_sizeof_lqdata_in_lqhello_packet();
//          }
//          if (neighbor->neigh_type == i && neighbor->link_type == LINK_ORDER[j]) {
//            if (is_first) {
//              expected_size += sizeof(struct pkt_lq_hello_info_header);
//              is_first = false;
//            }
//          }
//        }
//      }
//    }
//  }
//
//  if (rem < expected_size) {
//    net_output(outif);
//    rem = net_outbuffer_bytes_left(outif) - off;
//  }

  p_info_head = NULL;

  /* The neighbor list is grouped by neighbor type, and for each neighbor type it
   * is grouped by link type.
   * Iterate through all neighbor types ('i') and all link types ('j')/ */
  for (i = 0; i <= MAX_NEIGH; i++) {

    unsigned int j;

    for (j = 0; j < sizeof(LINK_ORDER) / sizeof(LINK_ORDER[0]); j++) {

      struct lq_hello_neighbor *neighbor;
      is_first = true;

      // Walk through neighbors
      for (neighbor = lq_hello->neighbors; neighbor != NULL; neighbor = neighbor->next) {
        size_t req;

        if (neighbor->neigh_type != i || neighbor->link_type != LINK_ORDER[j]) {
          continue;
        }

        // We need space for one IP address plus optional link quality information
        req = olsr_cnf->ipsize + olsr_sizeof_lqdata_in_lqhello_packet();

        if (is_first) {
          /* If this is the first neighbor with the current neighor type and link type,
           * we also need space for an info header. */
          req += sizeof(struct pkt_lq_hello_info_header);
        }

        /* Is there enough space left in the packet? */
        if (size + req > rem) {

          /* No, there is not enough space left */

          // finalize the OLSR header
          lq_hello->comm.size = size + off;

          serialize_common(&lq_hello->comm);

          // finalize the info header
          p_info_head->size = htons(buff + size - (unsigned char *)p_info_head);

          /* output the current (partial) packet */
          net_outbuffer_push(outif, msg_buffer, size + off);
          net_output(outif);

          // move to the beginning of the buffer
          size = 0;
          rem = net_outbuffer_bytes_left(outif) - off;

          // we need a new info header
          is_first = true;
        }

        if (is_first) {

          // create a new info header
          p_info_head = (struct pkt_lq_hello_info_header *)ARM_NOWARN_ALIGN(buff + size);
          size += sizeof(struct pkt_lq_hello_info_header);

          /* Set link and status for the current group of neighbors */
          p_info_head->link_code = CREATE_LINK_CODE(i, LINK_ORDER[j]);

          p_info_head->reserved = 0;
        }

        /* Serialize the current Neighbor Interface Address acc. RFC 3626 par. 6.1. */
        genipcopy(buff + size, &neighbor->addr);
        size += olsr_cnf->ipsize;

        /* Serialize the link quality data of this neighbor */
        size += olsr_serialize_hello_lq_data(buff + size, neighbor);

        is_first = false;
      }

      /* If there are any neighbors with the current neighbor type and link type,
       * keep track of the Link Message Size in the info header. */
      if (!is_first) {
        p_info_head->size = htons(buff + size - (unsigned char *)p_info_head);
      }
    }
  }

  // finalize the OLSR header
  lq_hello->comm.size = size + off;

  serialize_common(&lq_hello->comm);

  // move the message to the output buffer
  net_outbuffer_push(outif, msg_buffer, size + off);
}

static uint8_t
calculate_border_flag(void *lower_border, void *higher_border)
{
  uint8_t *lower = lower_border;
  uint8_t *higher = higher_border;
  uint8_t bitmask;
  uint8_t part, bitpos;

  for (part = 0; part < olsr_cnf->ipsize; part++) {
    if (lower[part] != higher[part]) {
      break;
    }
  }

  if (part == olsr_cnf->ipsize) {       // same IPs ?
    return 0;
  }
  // look for first bit of difference
  bitmask = 0xfe;
  for (bitpos = 0; bitpos < 8; bitpos++, bitmask <<= 1) {
    if ((lower[part] & bitmask) == (higher[part] & bitmask)) {
      break;
    }
  }

  bitpos += 8 * (olsr_cnf->ipsize - part - 1);
  return bitpos + 1;
}

/**
 * Serialize a lq_tc_message structure to the packet buffer.
 *
 * @param lq_tc the lq_tc_message struct containing the info to build the tc packet from.
 * @param outif the network interface to send the message on
 *
 * Note: see also RFC 3626 par. 3.3.  Packet Format and par. 9.1.  TC Message Format
 */
static void
serialize_lq_tc(struct lq_tc_message *lq_tc, struct network_interface *outif)
{
  size_t rem, size;
//  size_t expected_size = 0;

  /* Pointer into packet buffer */
  struct pkt_lq_tc_header *p_head;

  unsigned char *buff;
  struct lq_tc_neighbor *neighbor;
  union olsr_ip_addr *last_ip = NULL;
  uint8_t left_border_flag = 0xff;

  // leave space for the OLSR header
  size_t off = common_size();

  /* Initialize the LQ_TC header. See also RFC 3626 par 9.1.  TC Message Format . */
  p_head = (struct pkt_lq_tc_header *)ARM_NOWARN_ALIGN(msg_buffer + off);
  p_head->ansn = htons(lq_tc->ansn);
  if (olsr_cnf->lq_level > 0) {
    /* Not RFC-compliant */
    p_head->reserved.henning_special.lower_border = 0;
    p_head->reserved.henning_special.upper_border = 0;
  } else {
    p_head->reserved.reserved = htons(0);
  }

  // 'off' is the offset of the byte following the LQ_TC header
  off += sizeof(struct pkt_lq_tc_header);

  // our work buffer starts at 'off'...
  buff = msg_buffer + off;

  // ... that's why we start with a 'size' of 0 and subtract 'off' from
  // the remaining bytes in the output buffer
  size = 0;
  rem = net_outbuffer_bytes_left(outif) - off;

  /* Count the number of expected bytes needed in the packet. We want to put all
   * lq_tc data as much as possible into one packet. The goal is to prevent
   * message fragementation which is suspected to result in instable links.
   * TODO: Why is message fragementation suspected to result in instable links?
   * Large networks will have large messages, which will be fragmented anyway. So
   * this means that large networks will always have instable links?
   * Please add a comment. */
//  if (0 < net_output_pending(outif)) {
//    for (neighbor = lq_tc->neighbors; neighbor != NULL; neighbor = neighbor->next) {
//      expected_size += olsr_cnf->ipsize + olsr_sizeof_lqdata_in_lqtc_packet();
//    }
//  }

//  if (rem < expected_size) {
//    net_output(outif);
//    rem = net_outbuffer_bytes_left(outif) - off;
//  }

  // Walk through neighbors
  for (neighbor = lq_tc->neighbors; neighbor != NULL; neighbor = neighbor->next) {

    /* We need space for an IP address plus optional link quality information */
    size_t req = olsr_cnf->ipsize + olsr_sizeof_lqdata_in_lqtc_packet();

    /* Is there enough space left in the packet? */
    if (size + req > rem) {

      /* No, there is not enough space left */

      if (olsr_cnf->lq_level > 0) {
        /* Not RFC-compliant */
        /* TODO: What is happening here? Please add a comment. */
        p_head->reserved.henning_special.lower_border = left_border_flag;
        p_head->reserved.henning_special.upper_border = calculate_border_flag(last_ip, &neighbor->address);
        left_border_flag = p_head->reserved.henning_special.upper_border;
      }

      // finalize the OLSR header
      lq_tc->comm.size = size + off;

      serialize_common(&lq_tc->comm);

      /* Output the current (partial) packet */
      net_outbuffer_push(outif, msg_buffer, size + off);
      net_output(outif);

      // move to the beginning of the buffer
      size = 0;
      rem = net_outbuffer_bytes_left(outif) - off;
    }

    /* Serialize the current Advertised Neighbor Main Address acc. RFC 3626 par. 9.1. */
    genipcopy(buff + size, &neighbor->address);

    // remember last ip
    /* TODO: What is happening here? Please add a comment. */
    last_ip = (union olsr_ip_addr *)ARM_NOWARN_ALIGN(buff + size);

    size += olsr_cnf->ipsize;

    /* Serialize the link quality data of this neighbor */
    size += olsr_serialize_tc_lq_data(buff + size, neighbor);
  }

  if (olsr_cnf->lq_level > 0) {
    /* Not RFC-compliant */
    /* TODO: What is happening here? Please add a comment. */
    p_head->reserved.henning_special.lower_border = left_border_flag;
    p_head->reserved.henning_special.upper_border = 0xff;
  }

  // finalize the OLSR header
  lq_tc->comm.size = size + off;

  serialize_common(&lq_tc->comm);

  // move the message to the output buffer
  net_outbuffer_push(outif, msg_buffer, size + off);
}

void
olsr_output_lq_hello(void *para)
{
  struct lq_hello_message lq_hello;
  struct network_interface *outif = para;

  if (outif == NULL) {
    return;
  }
  // create LQ_HELLO in internal format
  create_lq_hello(&lq_hello, outif);

  // convert internal format into transmission format, send it
  serialize_lq_hello(&lq_hello, outif);

  // destroy internal format
  destroy_lq_hello(&lq_hello);

  if (net_output_pending(outif)) {
    if (outif->immediate_send_tc) {
      if (TIMED_OUT(outif->fwdtimer))
        set_buffer_timer(outif);
    } else {
      net_output(outif);
    }
  }
}

void
olsr_output_lq_tc(void *para)
{
  static int prev_empty = 1;
  struct lq_tc_message lq_tc;
  struct network_interface *outif = para;

  if (outif == NULL) {
    return;
  }

  // create LQ_TC in internal format
  create_lq_tc(&lq_tc, outif);

  if (lq_tc.neighbors != NULL) {

    // a) the message is not empty

    prev_empty = 0;

    // convert internal format into transmission format, send it
    serialize_lq_tc(&lq_tc, outif);

  } else if (prev_empty == 0) {

    // b) this is the first empty message

    // initialize timer
    set_empty_tc_timer(GET_TIMESTAMP(olsr_cnf->max_tc_vtime * 3 * MSEC_PER_SEC));

    prev_empty = 1;

    // convert internal format into transmission format, send it
    serialize_lq_tc(&lq_tc, outif);

  } else if (!TIMED_OUT(get_empty_tc_timer())) {

    // c) this is not the first empty message, send if timer hasn't fired

    serialize_lq_tc(&lq_tc, outif);
  }

  // destroy internal format
  destroy_lq_tc(&lq_tc);

  if (net_output_pending(outif)) {
    if (!outif->immediate_send_tc) {
      if (TIMED_OUT(outif->fwdtimer))
        set_buffer_timer(outif);
    } else {
      net_output(outif);
    }
  }
}

/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
