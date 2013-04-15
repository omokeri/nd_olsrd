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
 * File       : Link.c
 * Description: 
 *
 * ------------------------------------------------------------------------- */

/* System includes */
#include <assert.h> /* assert() */
#include <stdlib.h> /* free() */

/* OLSRD includes */
#include "link_set.h" /* struct link_entry */
#include "olsr.h" /* olsr_calloc */
#include "ipcalc.h" /* ipequal */
#include "lq_plugin_ett.h" /* struct link_lq_ett */

/* Plugin includes */
#include "Packet.h" /* enum TProbeType */
#include "NetworkInterfaces.h" /* ProbedInterfaces */
#include "Link.h" /* struct TProbedLink */
#include "Probe.h" /* TIME_SLOT_INCREMENT_LINK */

/* Unknown medium speed */
#define UNKOWN_MEDIUM_SPEED -1

/* -------------------------------------------------------------------------
 * Function   : CreateProbedLink
 * Description: Create a new TProbedLink object and connect it to the passed
 *              link_entry object
 * Input      : link - link_entry object
 * Output     : none
 * Return     : none
 * Data Used  : none
 * ------------------------------------------------------------------------- */
void CreateProbedLink(struct link_entry* link_entry)
{
  int i,j;
  struct TProbedInterface* intf;
  struct TProbedLink* newPlink;
  enum TProbeType pt;
  int nextLinkTimeSlot = 0;
  struct link_lq_ett *lq_data = (struct link_lq_ett*) link_entry->linkquality;

  /* Check pre-conditions */
  assert(link_entry != NULL);

  /* Lookup the probed network interface belonging to this link */
  for (intf = ProbedInterfaces; intf != NULL; intf = intf->next)
  {
    if (ipequal(&intf->olsrIntf->interf->ip_addr, &link_entry->local_iface_addr))
    {
      nextLinkTimeSlot = intf->nextLinkTimeSlot; 
      intf->nextLinkTimeSlot = 
        (intf->nextLinkTimeSlot + TIME_SLOT_INCREMENT_LINK)
        % N_TIME_SLOTS;

      break; /* for */
    } /* if */
  } /* for */

  /* Allocate memory for object */
  newPlink = olsr_calloc(sizeof(struct TProbedLink), "Link Probe plugin - CreateProbedLink");
  if (newPlink == NULL)
  {
    return;
  }

  for (i = 0; i < PtNTypes; i++)
  {
    for (j = 0; j < PROBE_HISTORY_SIZE; j++)
    {
      newPlink->tripTimes[i][j] = 0;
    }
  }
  newPlink->currIndex = 0;
  newPlink->logSize = 0;

  newPlink->probeType = PtUnicastSmall;
  newPlink->probeId = 0;

  newPlink->nextProbeAt = nextLinkTimeSlot;

  for (pt = PtFirst; pt < PtNTypes; pt++)
  {
    newPlink->nSentProbes[pt] = 0;
    newPlink->nReceivedReplies[pt] = 0;
  }

  newPlink->estimatedLinkSpeed = UNKOWN_MEDIUM_SPEED;
  newPlink->roundedLinkSpeed = UNKOWN_MEDIUM_SPEED;

  lq_data->link_cost_data = newPlink;
} /* CreateProbedLink */

/* -------------------------------------------------------------------------
 * Function   : RemoveProbedLink
 * Description: Remove the TProbedLink object from the passed
 *              link_entry object
 * Input      : link - link_entry object
 * Output     : none
 * Return     : none
 * Data Used  : none
 * ------------------------------------------------------------------------- */
void RemoveProbedLink(struct link_entry* link_entry)
{
  struct link_lq_ett *lq_data = (struct link_lq_ett*) link_entry->linkquality;
  struct TProbedLink* plink = (struct TProbedLink*)lq_data->link_cost_data;
  free(plink);
  lq_data->link_cost_data = NULL;
} /* RemoveProbedLink */

