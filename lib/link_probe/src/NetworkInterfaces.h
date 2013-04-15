#ifndef _LINKPROBE_NETWORKINTERFACES_H
#define _LINKPROBE_NETWORKINTERFACES_H

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
 * File       : NetworkInterfaces.h
 * Description: Functions to open and close network interfaces
 *
 * ------------------------------------------------------------------------- */

/* OLSRD includes */
#include "olsr_cfg.h" /* enum TMediumType */
#include "olsrd_plugin.h" /* union set_plugin_parameter_addon */

#define MAX_ESTIMATED_MEDIUM_SPEED 1000.0 /* in Mbits/sec */

#define UNKNOWN_MEDIUM_SPEED -1.0

struct TProbedInterface
{
  /* File descriptor of UDP (datagram) socket for probe packets */
  int skfd;
  
  /* OLSRs idea of this network interface */
  struct olsr_if* olsrIntf;

  /* Maximum transmission unit (bytes) = maximum number of UDP data bytes 
   * in one packet */
  int mtu;

  /* Size difference (in bits) between a large probe packet and a small
   * probe packet */
  int packetSizeDiffBits;

  int nextLinkTimeSlot;

  /* Next element in list */
  struct TProbedInterface* next; 
};

extern struct TProbedInterface* ProbedInterfaces;

extern int HighestSkfd;
extern fd_set InputSet;

extern int NextInterfaceTimeSlot;

int CreateInterfaces(struct network_interface* skipThisIntf);
void AddInterface(struct network_interface* newIntf);
void CloseInterfaces(void);
int AddNonProbedInterface(const char* ifName, void* data, set_plugin_parameter_addon addon);
int IsNonProbedInterface(const char* ifName);

#endif /* _LINKPROBE_NETWORKINTERFACES_H */
