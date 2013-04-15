
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004, Thomas Lopatic (thomas@lopatic.de)
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

#include "defs.h" /* olsr_cnf */
#include "olsr_types.h" /* olsr_linkcost */
#include "olsr_protocol.h" /* NOT_SYM, WILL_ALWAYS */
#include "hashing.h" /* HASHSIZE */
#include "lq_plugin.h" /* LINK_COST_BROKEN */
#include "neighbor_table.h" /* neighbor_entry, OLSR_FOR_ALL_NBR_ENTRIES, olsr_lookup_neighbor_table() */
#include "two_hop_neighbor_table.h" /* neighbor_2_entry, neighbor_list_entry */
#include "link_set.h" /* link_entry, get_best_link_to_neighbor(), signal_link_changes() */
#include "lq_mpr.h" /* olsr_calculate_lq_mpr() */

void
olsr_calculate_lq_mpr(void)
{
  struct neighbor_2_entry *neigh2;
  int i, k;
  struct neighbor_entry *neigh;
  olsr_linkcost best, best_1hop;
  bool mpr_changes = false;

  OLSR_FOR_ALL_NBR_ENTRIES(neigh) {

    /* Memorize previous MPR status. */
    neigh->was_mpr = neigh->is_mpr;

    /* Clear current MPR status. */
    neigh->is_mpr = false;

    /* In this pass we are only interested in WILL_ALWAYS neighbours */
    if (neigh->N_status == NOT_SYM || neigh->N_willingness != WILL_ALWAYS) {
      continue;
    }

    neigh->is_mpr = true;

    if (neigh->is_mpr != neigh->was_mpr) {
      mpr_changes = true;
    }
  }
  OLSR_FOR_ALL_NBR_ENTRIES_END(neigh);

  for (i = 0; i < HASHSIZE; i++) {

    /* Walk through all 2-hop neighbours */
    for (
      neigh2 = two_hop_neighbortable[i].next;
      neigh2 != &two_hop_neighbortable[i];
      neigh2 = neigh2->next)
    {
      struct neighbor_list_entry *walker;

      best_1hop = LINK_COST_BROKEN;

      /* Check whether this 2-hop neighbour is also a 1-hop (direct) neighbour */
      neigh = olsr_lookup_neighbor_table(&neigh2->neighbor_2_addr);

      /* If it's a neighbour and also symmetric, then examine the link quality */
      if (neigh != NULL && neigh->N_status == SYM) {

        /* If the direct link is better than the best route via
         * an MPR, then prefer the direct link and do not select
         * an MPR for this 2-hop neighbour */

        /* Determine the link quality of the direct link */
        struct link_entry *lnk = get_best_link_to_neighbor(&neigh->N_neighbor_main_addr);

        if (lnk == NULL) {
          /* Don't consider neighbor entries without a corresponding link entry */
          continue;
        }

        best_1hop = lnk->link_cost;

        /* 2-hop neigbhor is also 1-hop neighbor. See whether we find a better route
         * via an MPR. */
        for (
          walker = neigh2->neighbor_2_nblist.next;
          walker != &neigh2->neighbor_2_nblist;
          walker = walker->next)
        {
          if (walker->path_link_cost < best_1hop)
          {
            /* Found a better 2-hop path */
            break;
          }
        }

        /* We've reached the end of the list, so we haven't found
         * a better route via an MPR - so, skip MPR selection for
         * this 1-hop neighbor */
        if (walker == &neigh2->neighbor_2_nblist)
        {
          continue;
        }
      }

      /* Find the connecting 1-hop neighbours with the best total link qualities */

      /* Mark all 1-hop neighbours as not selected */
      for (
        walker = neigh2->neighbor_2_nblist.next;
        walker != &neigh2->neighbor_2_nblist;
        walker = walker->next)
      {
        walker->neighbor->skip = false;
      }

      for (k = 0; k < olsr_cnf->mpr_coverage; k++) {

        /* Look for the best 1-hop neighbour that we haven't yet selected */
        neigh = NULL;
        best = LINK_COST_BROKEN;

        for (
          walker = neigh2->neighbor_2_nblist.next;
          walker != &neigh2->neighbor_2_nblist;
          walker = walker->next)
        {
          if (
            walker->neighbor->N_status == SYM &&
            !walker->neighbor->skip &&
            walker->path_link_cost < best)
          {
            neigh = walker->neighbor;
            best = walker->path_link_cost;
          }
        }

        /* Found a 1-hop neighbor that we haven't previously selected.
         * Use it as MPR only when the 2-hop path through it is better than
         * any existing 1-hop path. */
        if (neigh != NULL && best < best_1hop) {
          neigh->is_mpr = true;
          neigh->skip = true;

          if (neigh->is_mpr != neigh->was_mpr)
            mpr_changes = true;
        }

        /* No neighbour found => the requested MPR coverage cannot be satisfied => stop */
        else {
          break;
        }
      }
    }
  }

  if (mpr_changes && olsr_cnf->tc_redundancy > 0) {
    signal_link_changes(true);
  }
}

/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
