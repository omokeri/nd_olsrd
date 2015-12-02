/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004, Andreas Tonnesen(andreto@olsr.org)
 *                     includes code by Bruno Randolf
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

#ifndef _OLSRD_INFO_TYPES_H
#define _OLSRD_INFO_TYPES_H

#include <stdbool.h>

#include "common/autobuf.h"

/* these provide all of the runtime status info */
#define SIW_NEIGHBORS 0x0001
#define SIW_LINKS 0x0002
#define SIW_ROUTES 0x0004
#define SIW_HNA 0x0008
#define SIW_MID 0x0010
#define SIW_TOPOLOGY 0x0020
#define SIW_GATEWAYS 0x0040
#define SIW_INTERFACES 0x0080
#define SIW_2HOP 0x0100
#define SIW_SGW 0x0200
#define SIW_RUNTIME_ALL (SIW_NEIGHBORS | SIW_LINKS | SIW_ROUTES | SIW_HNA | SIW_MID | SIW_TOPOLOGY | SIW_GATEWAYS | SIW_INTERFACES | SIW_2HOP | SIW_SGW)

/* these only change at olsrd startup */
#define SIW_VERSION 0x0400
#define SIW_CONFIG 0x0800
#define SIW_PLUGINS 0x1000
#define SIW_STARTUP_ALL (SIW_VERSION | SIW_CONFIG | SIW_PLUGINS)

/* this is everything in normal format */
#define SIW_ALL (SIW_RUNTIME_ALL | SIW_STARTUP_ALL)

/* this data is not normal format but olsrd.conf format */
#define SIW_OLSRD_CONF 0x2000

typedef void (*init_plugin)(const char *plugin_name);
typedef const char * (*mime_type)(unsigned int send_what);
typedef void (*printer_neighbors)(struct autobuf *abuf, bool list_2hop);
typedef void (*printer_generic)(struct autobuf *abuf);

typedef struct {
    init_plugin init;
    mime_type determine_mime_type;
    printer_neighbors neighbors;
    printer_generic links;
    printer_generic routes;
    printer_generic topology;
    printer_generic hna;
    printer_generic mid;
    printer_generic gateways;
    printer_generic sgw;
    printer_generic version;
    printer_generic olsrd_conf;
    printer_generic interfaces;
    printer_generic config;
    printer_generic plugins;
} printer_functions_t;

#define MAX_CLIENTS 3

typedef struct {
  char *buffer[MAX_CLIENTS];
  size_t size[MAX_CLIENTS];
  size_t written[MAX_CLIENTS];
  int socket[MAX_CLIENTS];
  int count;
} outbuffer_t;

#endif /* _OLSRD_INFO_TYPES_H */
