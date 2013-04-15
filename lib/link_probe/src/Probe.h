#ifndef _LINKPROBE_PROBE_H
#define _LINKPROBE_PROBE_H

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
 * File       : Probe.h
 * Description: Common declarations for the Link Probing plugin
 *
 * ------------------------------------------------------------------------- */

/* Plugin data */
#define PLUGIN_NAME "OLSRD LINK PROBE plugin"
#define PLUGIN_NAME_SHORT "OLSRD LINK PROBE"
#define PLUGIN_VERSION "0.3 (" __DATE__ " " __TIME__ ")"
#define PLUGIN_COPYRIGHT "  (C) Erik Tromp"
#define PLUGIN_AUTHOR "  Erik Tromp (eriktromp@users.sourceforge.net)"
#define PLUGIN_WEBSITE "  Download latest version at http://sourceforge.net/projects/olsr-lc"
#define MOD_DESC PLUGIN_NAME " " PLUGIN_VERSION "\n" PLUGIN_COPYRIGHT "\n" PLUGIN_AUTHOR "\n" PLUGIN_WEBSITE
#define PLUGIN_INTERFACE_VERSION 5

/* UDP-Port used by link probe packets */
#define LINK_PROBE_PORT 51698

/* Basis for declaring probing interval (seconds). Make this an integer multiple
 * of the configuration value 'Pollrate' (which defaults to 0.05) */
#define PROBING_INTERVAL_BASE 0.05

/* Base * # slots = interval, e.g. 0.1 * 20 = 2 seconds */
#define N_TIME_SLOTS 20

/* The increment value must be a prime (1, 2, 3, 5, 7, 11, ...), and must be chosen
 * that it cannot be evenly divided into the number of time slots N_TIME_SLOTS (20).
 * The goal is to generate a mathematical 'Full Cycle'; see also
 * http://en.wikipedia.org/wiki/Full_cycle .
 * Each next link on an interface is 7 time slots separated from the
 * previous link. The first link on the first interface is 1 time slot separated
 * from the first link on the next interface. */
#define TIME_SLOT_INCREMENT_LINK 7
#define TIME_SLOT_INCREMENT_INTERFACE 1

/* Round trip time limits (usec). Measured values outside this range are discarded. */
#define RTT_MIN 10
#define RTT_MAX 10000000

/* Forward declaration of OLSR types */
struct network_interface;
struct link_entry;

void LinkProbePError(const char* format, ...) __attribute__((format(printf, 1, 2)));
void ProbeAllLinks(void*);
void InterfaceChange(int, struct network_interface*, enum olsr_ifchg_flag);
int LinkChange(struct link_entry* lnk, int action);
int InitLinkProbePlugin(struct network_interface* skipThisIntf);
void CloseLinkProbePlugin(void);

#endif /* _LINKPROBE_PROBE_H */
