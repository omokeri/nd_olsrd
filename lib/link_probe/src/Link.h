#ifndef _LINKPROBE_LINK_H
#define _LINKPROBE_LINK_H

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
 * File       : Link.h
 * Description: 
 *
 * ------------------------------------------------------------------------- */

/* System includes */
#include <sys/types.h> /* u_int32_t */

/* Plugin includes */
#include "Packet.h" /* enum TProbeType */

/* The number of trip time measurements stored per probe type */
#define PROBE_HISTORY_SIZE 20

struct TProbedLink
{
  /* History of measured probe trip times (single trip times, not round-trip tipes).
   * Stored in microseconds. */
  int tripTimes[PtNTypes][PROBE_HISTORY_SIZE];
  int currIndex;
  int logSize;

  enum TProbeType probeType;
  u_int32_t probeId;

  int nextProbeAt; /* 0 means: probe now */

  /* Number of probe packets send and number of received replies */
  u_int32_t nSentProbes[PtNTypes];
  u_int32_t nReceivedReplies[PtNTypes];

  /* in Mbit/sec. <= 0 is unknown */
  float estimatedLinkSpeed;

  /* Mbit/sec, -1 = unknown */
  int roundedLinkSpeed;
};

/* Forward declaration of OLSR types */
struct link_entry;

void CreateProbedLink(struct link_entry* lnk);
void RemoveProbedLink(struct link_entry* lnk);

#endif /* _LINKPROBE_LINK_H */
