
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

#include "defs.h"
#include "hysteresis.h" /* olsr_update_hysteresis_hello() */
#include "two_hop_neighbor_table.h" /* neighbor_list_entry */
#include "tc_set.h" /* olsr_input_tc() */
#include "mpr_selector_set.h" /* olsr_update_mprs_set() */
#include "mid_set.h" /* mid_lookup_main_addr() */
#include "olsr.h" /* olsr_calloc() */
#include "parser.h" /* olsr_parser_add_function() */
#include "scheduler.h" /* olsr_change_timer() */
#include "lq_plugin.h" /* LINK_COST_BROKEN */
#include "olsr_protocol.h" /* SYM_NEIGH */
#include "neighbor_table.h" /* olsr_lookup_my_neighbors() */
#include "link_set.h" /* get_best_link_to_neighbor() */
#include "lq_packet.h" /* destroy_lq_hello() */
#include "hna_set.h" /* olsr_input_hna () */
#include "process_packet.h"

/**
 *Links a one-hop neighbor with a 2-hop neighbor.
 *
 *@param neighbor the 1-hop neighbor
 *@param two_hop_neighbor the 2-hop neighbor
 *@return nada
 */
static void
linking_this_2_entries(struct neighbor_entry *neighbor, struct neighbor_2_entry *two_hop_neighbor, olsr_reltime vtime)
{
  struct neighbor_list_entry *list_of_1_neighbors = olsr_calloc(sizeof(struct neighbor_list_entry), "Link entries 1");
  struct neighbor_2_list_entry *list_of_2_neighbors = olsr_calloc(sizeof(struct neighbor_2_list_entry), "Link entries 2");

  list_of_1_neighbors->neighbor = neighbor;

  list_of_1_neighbors->path_link_cost = LINK_COST_BROKEN;
  list_of_1_neighbors->saved_path_link_cost = LINK_COST_BROKEN;
  list_of_1_neighbors->second_hop_link_cost = LINK_COST_BROKEN;

  /* Queue */
  two_hop_neighbor->neighbor_2_nblist.next->prev = list_of_1_neighbors;
  list_of_1_neighbors->next = two_hop_neighbor->neighbor_2_nblist.next;

  two_hop_neighbor->neighbor_2_nblist.next = list_of_1_neighbors;
  list_of_1_neighbors->prev = &two_hop_neighbor->neighbor_2_nblist;
  list_of_2_neighbors->neighbor_2 = two_hop_neighbor;
  list_of_2_neighbors->nbr2_nbr = neighbor;     /* XXX refcount */

  olsr_change_timer(list_of_2_neighbors->nbr2_list_timer, vtime, OLSR_NBR2_LIST_JITTER, OLSR_TIMER_ONESHOT);

  /* Queue */
  neighbor->neighbor_2_list.next->prev = list_of_2_neighbors;
  list_of_2_neighbors->next = neighbor->neighbor_2_list.next;
  neighbor->neighbor_2_list.next = list_of_2_neighbors;
  list_of_2_neighbors->prev = &neighbor->neighbor_2_list;

  /*increment the pointer counter */
  two_hop_neighbor->neighbor_2_pointer++;
}

/**
 *Processes an list of neighbors from an incoming HELLO message.
 *@param neighbor the neighbor who sent the message.
 *@param message the HELLO message
 *@return nada
 */
static void
process_message_neighbors(struct neighbor_entry *neighbor, const struct lq_hello_message *message)
{
  struct lq_hello_neighbor *message_neighbors;

  for (message_neighbors = message->neighbors; message_neighbors != NULL; message_neighbors = message_neighbors->next) {
    union olsr_ip_addr *neigh_addr;
    struct neighbor_2_entry *two_hop_neighbor;

    /*
     *check all interfaces
     *so that we don't add ourselves to the
     *2 hop list
     *IMPORTANT!
     */
    if (if_ifwithaddr(&message_neighbors->addr) != NULL)
      continue;

    /* Get the main address */
    neigh_addr = mid_lookup_main_addr(&message_neighbors->addr);

    if (neigh_addr != NULL) {
      message_neighbors->addr = *neigh_addr;
    }

    if (message_neighbors->neigh_type == SYM_NEIGH
        || message_neighbors->neigh_type == MPR_NEIGH) {

      struct neighbor_2_list_entry *two_hop_neighbor_yet =
        olsr_lookup_my_neighbors(neighbor, &message_neighbors->addr);

      if (two_hop_neighbor_yet != NULL) {

        /* Update the holding time for this neighbor */
        /* TODO: Where is message->vtime set ?? */
        olsr_set_timer(
          &two_hop_neighbor_yet->nbr2_list_timer,
          message->vtime,
          OLSR_NBR2_LIST_JITTER,
          OLSR_TIMER_ONESHOT,
          &olsr_expire_nbr2_list,
          two_hop_neighbor_yet,
          0);
        two_hop_neighbor = two_hop_neighbor_yet->neighbor_2;

        /* For link quality OLSR, reset the neighbor_list_entry's path link cost here.
         * The path link cost will be calculated in the second pass, below.
         * Keep the neighbor_list_entry's saved_path_link_cost for reference. */

        if (olsr_cnf->lq_level > 0) {

          /* Loop through the one-hop neighbors that see this 'two_hop_neighbor' */

          struct neighbor_list_entry *walker;

          for (walker = two_hop_neighbor->neighbor_2_nblist.next;
               walker != &two_hop_neighbor->neighbor_2_nblist;
               walker = walker->next) {

            /* Have we found the one-hop neighbor that sent the
             * HELLO message that we're current processing? */
            if (walker->neighbor == neighbor) {
              walker->path_link_cost = LINK_COST_BROKEN;
            }
          }
        }
      } else {
        two_hop_neighbor = olsr_lookup_two_hop_neighbor_table(&message_neighbors->addr);
        if (two_hop_neighbor == NULL) {
          changes_neighborhood = true;
          changes_topology = true;

          two_hop_neighbor = olsr_calloc(sizeof(struct neighbor_2_entry), "Process HELLO");

          two_hop_neighbor->neighbor_2_nblist.next = &two_hop_neighbor->neighbor_2_nblist;

          two_hop_neighbor->neighbor_2_nblist.prev = &two_hop_neighbor->neighbor_2_nblist;

          two_hop_neighbor->neighbor_2_pointer = 0;

          two_hop_neighbor->neighbor_2_addr = message_neighbors->addr;

          olsr_insert_two_hop_neighbor_table(two_hop_neighbor);

          /* TODO: Where is message->vtime set ?? */
          linking_this_2_entries(neighbor, two_hop_neighbor, message->vtime);
        } else {
          /*
             linking to this two_hop_neighbor entry
           */
          changes_neighborhood = true;
          changes_topology = true;

          /* TODO: Where is message->vtime set ?? */
          linking_this_2_entries(neighbor, two_hop_neighbor, message->vtime);
        }
      }
    }
  }

  /* Separate, second pass for link quality OLSR */

  if (olsr_cnf->lq_level > 0)
  {
    olsr_linkcost first_hop_pathcost;
    struct link_entry *lnk = get_best_link_to_neighbor(&neighbor->N_neighbor_main_addr);

    if (lnk == NULL) {
      return;
    }

    /* calculate first hop path quality */
    first_hop_pathcost = lnk->link_cost;

    /* Second pass for link quality OLSR: calculate the best 2-hop
     * path costs to all the 2-hop neighbors indicated in the
     * HELLO message. Since the same 2-hop neighbor may be listed
     * more than once in the same HELLO message (each at a possibly
     * different quality) we want to select only the best one, not just
     * the last one listed in the HELLO message. */

    for (message_neighbors = message->neighbors;
         message_neighbors != NULL;
         message_neighbors = message_neighbors->next)
    {
      if (if_ifwithaddr(&message_neighbors->addr) != NULL) {
        continue;
      }

      if (message_neighbors->neigh_type == SYM_NEIGH
          || message_neighbors->neigh_type == MPR_NEIGH)
      {
        struct neighbor_list_entry *walker;
        struct neighbor_2_entry *two_hop_neighbor;
        struct neighbor_2_list_entry *two_hop_neighbor_yet = 
          olsr_lookup_my_neighbors(
            neighbor,
            &message_neighbors->addr);

        if (!two_hop_neighbor_yet) {
          continue;
        }

        two_hop_neighbor = two_hop_neighbor_yet->neighbor_2;

        /* Loop through the one-hop neighbors that see this 'two_hop_neighbor' */
        for (walker = two_hop_neighbor->neighbor_2_nblist.next;
             walker != &two_hop_neighbor->neighbor_2_nblist;
             walker = walker->next)
        {
          /* Have we found the one-hop neighbor that sent the
           * HELLO message that we're current processing? */
          if (walker->neighbor == neighbor)
          {
            // the link cost between the 1-hop neighbour and the
            // 2-hop neighbour

            olsr_linkcost new_second_hop_linkcost = message_neighbors->cost;

            // the total cost for the route
            // "us --- 1-hop --- 2-hop"

            olsr_linkcost new_path_link_cost = first_hop_pathcost + new_second_hop_linkcost;

            // Only copy the link cost if it is better than what we have
            // for this 2-hop neighbor
            if (new_path_link_cost < walker->path_link_cost) {
              walker->second_hop_link_cost = new_second_hop_linkcost;
              walker->path_link_cost = new_path_link_cost;

              walker->saved_path_link_cost = new_path_link_cost;

              changes_neighborhood = true;
              changes_topology = true;
            }
          }
        }
      }
    }
  }
}

/**
 * Check if a HELLO message states this node as a MPR.
 *
 * @param hello the HELLO message to check
 * @param in_if the network interface on which the HELLO packet was received
 *
 *@return 1 if we are selected as MPR 0 if not
 */
static bool
lookup_mpr_status(const struct lq_hello_message *hello, const struct network_interface *in_if)
{
  struct lq_hello_neighbor *neighbor;

  for (neighbor = hello->neighbors; neighbor != NULL; neighbor = neighbor->next) {
    if (neighbor->link_type != UNSPEC_LINK
        && (olsr_cnf->ip_version == AF_INET
            ? ip4equal(&neighbor->addr.v4, &in_if->ip_addr.v4)
            : ip6equal(&neighbor->addr.v6, &in_if->int6_addr.sin6_addr))) {

      if (neighbor->link_type == SYM_LINK && neighbor->neigh_type == MPR_NEIGH) {
        return true;
      }
      break;
    }
  }
  /* Not found */
  return false;
}

/* Return: 0 if successfull, 1 if failed */
static int
deserialize_hello(struct lq_hello_message *hello, const void *buffer)
{
  const unsigned char *curr, *limit;
  struct ipaddr_str buf;

  memset (hello, 0, sizeof(*hello));

  curr = buffer;

  /* Deserialize according to RFC 3626 par. 3.3.  Packet Format */
  pkt_get_u8(&curr, &hello->comm.type);
  if (hello->comm.type != olsr_get_hello_message_type()) {
    /* We don't understand this type of message. Cannot continue. */
    return 1;
  }
  pkt_get_reltime(&curr, &hello->comm.vtime);
  pkt_get_u16(&curr, &hello->comm.size);
  pkt_get_ipaddress(&curr, &hello->comm.orig);

  pkt_get_u8(&curr, &hello->comm.ttl);
  pkt_get_u8(&curr, &hello->comm.hops);
  pkt_ignore_u16(&curr); /* Ignore Message Sequence Number */

  /* Deserialize according to RFC 3626 par. 6.1.  HELLO Message Format */
  pkt_ignore_u16(&curr); /* Reserved */
  pkt_get_reltime(&curr, &hello->htime);
  pkt_get_u8(&curr, &hello->will);

  hello->neighbors = NULL;

  limit = ((const unsigned char *)buffer) + hello->comm.size;

  /* TODO: Much, much more checking should be done here. The OLSR daemon will probably crash
   * when someone sends a specially crafted packet in which "Message Size" is set to an
   * absurd high value. */
  while (curr < limit) {
    const unsigned char *limit2 = curr;
    uint8_t link_code;
    uint16_t link_message_size;

    pkt_get_u8(&curr, &link_code);
    pkt_ignore_u8(&curr); /* Reserved */
    pkt_get_u16(&curr, &link_message_size);

    limit2 += link_message_size;

    /* TODO: Much, much more checking should be done here. The OLSR daemon will probably crash
     * when someone sends a specially crafted packet in which "Link Message Size" is set to an
     * absurd high value. */
    while (curr < limit2) {
			struct lq_hello_neighbor *neighbor = olsr_calloc_lq_hello_neighbor("HELLO deserialization"); 
      pkt_get_ipaddress(&curr, &neighbor->addr);
      olsr_deserialize_hello_lq_data(&curr, neighbor);
      neighbor->link_type = EXTRACT_LINK_TYPE(link_code);
      neighbor->neigh_type = EXTRACT_NEIGHBOR_TYPE(link_code);

      neighbor->next = hello->neighbors;
      hello->neighbors = neighbor;
    }
  }
  return 0;
}

/*
 * Process an incoming HELLO packet.
 *
 * Returns: always false to indicate that the HELLO packet is not to be forwarded.
 */
bool
olsr_input_hello(union pkt_olsr_message* buffer, struct network_interface* inif, union olsr_ip_addr* from)
{
  struct lq_hello_message hello;

  if (buffer == NULL) {
    return false;
  }
  if (deserialize_hello(&hello, buffer) != 0) {
    return false;
  }
  olsr_hello_tap(&hello, inif, from);

  /* Yuch: calling a 'destroy...' function even though this is a variable on the stack! 
   * It is correct, though, since destroy_lq_hello destroys only the linked list starting
   * in hello. Typical example of 'we need to know the interal details before we can use
   * this function.' */
  destroy_lq_hello(&hello);

  /* Do not forward hello messages */
  return false;
}

/**
 *Initializing the parser functions we are using
 */
void
olsr_init_packet_process(void)
{
  olsr_parser_add_function(&olsr_input_hello, olsr_get_hello_message_type());
  olsr_parser_add_function(&olsr_input_tc, olsr_get_tc_message_type());

  olsr_parser_add_function(&olsr_input_mid, MID_MESSAGE);
  olsr_parser_add_function(&olsr_input_hna, HNA_MESSAGE);
}

void
olsr_hello_tap(struct lq_hello_message *hello, struct network_interface *in_if, const union olsr_ip_addr *from_addr)
{
  struct neighbor_entry *neighbor;

  /* Update link status */
  struct link_entry *lnk = update_link_entry(&in_if->ip_addr, from_addr, hello, in_if);

  /* The following does not seem to be compliant with RFC 3626. */

  /* Check if the originator address in the HELLO packet matches the source IP address as
   * read from the IP header of the UDP packet.
   *
   * Rationale: there is
   * - the source IP address
   * - the originator IP address which MUST be the main address of the originating node
   *   (see RFC 3626 par. 3.3. )
   * Now, the source IP address may or may not be the same as the main address of the
   * originating node. If it is not the same, we might as well check our MID table to
   * see if we have registered this address. This is assumed to speed up routing.
   * TODO: Why does this speed up routing? Please add a comment.
   *
   * The problem with these 'unofficially' added MID entries is that they will time out
   * whenever olsr_prune_aliases is called. This will lead to unstable routing tables
   * on the nodes in the network.
   */
//  if (!ipequal(&hello->comm.orig, from_addr)) {

    /* If main address of the originating node does not match the source IP address,
     * make sure there is an alias of the neighbor in the MID table.*/

//    if (olsr_validate_address(from_addr)) {

      /* Check if the alias is not already in the MID table.
       * Lookup the main address corresponding to the source IP address as read from the
       * IP header of the UDP packet. */
//      union olsr_ip_addr* main_addr = mid_lookup_main_addr(from_addr);

      /* If the corresponding main address could not be found, then insert a new entry
       * in the MID table.
       * If the main address is equal to the originator address in the HELLO packet,
       * (the MID entry is already there), then just refresh the entry.
       * Both actions are done with a call to insert_mid_alias().  */
//      if (main_addr == NULL || ipequal(&hello->comm.orig, main_addr)) {

        /*struct ipaddr_str srcbuf, origbuf;
        olsr_syslog(OLSR_LOG_INFO, "got hello from unknown alias ip of direct neighbour: ip: %s main-ip: %s",
                    olsr_ip_to_string(&origbuf,&message->source_addr),
                    olsr_ip_to_string(&srcbuf,from_addr));*/

        /* Add or refresh the source IP address as an alias of the main address
         * of the originating node. */
//        insert_mid_alias(&hello->comm.orig, from_addr, hello->comm.vtime);
//      }
//      else
//      {
//        struct ipaddr_str srcbuf, origbuf;
//        olsr_syslog(
//          OLSR_LOG_INFO,
//          "got hello with invalid from and originator adress pair (%s, %s) Duplicate Ips?\n",
//          olsr_ip_to_string(&origbuf, &hello->comm.orig),
//          olsr_ip_to_string(&srcbuf, from_addr));
//      }
//    }
//  }

  if (olsr_cnf->lq_level > 0) {
    struct lq_hello_neighbor *walker;

    /* just in case our neighbor has changed its HELLO interval */
    olsr_update_packet_loss_hello_int(lnk, hello->htime);

    /* find the input interface in the list of neighbors */
    for (walker = hello->neighbors; walker != NULL; walker = walker->next) {
      if (walker->link_type != UNSPEC_LINK
          && ipequal(&walker->addr, &in_if->ip_addr)) {
        break;
      }
    }

    /* Memorize our neighbour's idea of the link quality, so that we
     * know the link quality in both directions
     *
     * walker is NULL if the input interface was not included in
     * the message (or was included as an UNSPEC_LINK) */
    olsr_memorize_foreign_hello_lq(lnk, walker);

    lnk->link_cost = olsr_calc_link_cost(lnk, in_if);

    /* update packet loss for link quality calculation */
    olsr_received_hello_handler(lnk);
  }

  neighbor = lnk->neighbor;

  /*
   * Hysteresis
   */
  if (olsr_cnf->use_hysteresis) {
    /* Update HELLO timeout */
    /* printf("MESSAGE HTIME: %f\n", hello->htime); */
    olsr_update_hysteresis_hello(lnk, hello->htime);
  }

  /* Check if we are chosen as MPR */
  if (lookup_mpr_status(hello, in_if))
    /* source_addr is always the main addr of a node! */
    olsr_update_mprs_set(&hello->comm.orig, hello->comm.vtime);

  /* Check willingness */
  if (neighbor->N_willingness != hello->will) {
    struct ipaddr_str buf;
    OLSR_PRINTF(
      1,
      "Willingness for %s changed from %d to %d - UPDATING\n",
      olsr_ip_to_string(&buf, &neighbor->N_neighbor_main_addr),
      neighbor->N_willingness,
      hello->will);
    /*
     *If willingness changed - recalculate
     */
    neighbor->N_willingness = hello->will;
    changes_neighborhood = true;
    changes_topology = true;
  }

  /* Don't register neighbors of neighbors that announces WILL_NEVER */
  if (neighbor->N_willingness != WILL_NEVER)
    process_message_neighbors(neighbor, hello);

  /* Process changes immedeatly in case of MPR updates */
  olsr_process_changes();

  return;
}

/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
