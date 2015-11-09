/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2009, the olsr.org team - see HISTORY file
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

#include "lock_file.h"

#include <stdio.h>
#include <string.h>

#if defined __ANDROID__
  #define DEFAULT_LOCKFILE_PREFIX "/data/local/olsrd"
#elif defined linux || defined __FreeBSD__ || defined __NetBSD__ || defined __OpenBSD__
  #define DEFAULT_LOCKFILE_PREFIX "/var/run/olsrd"
#elif defined _WIN32
  #define DEFAULT_LOCKFILE_PREFIX "C:\\olsrd"
#else /* defined _WIN32 */
  #define DEFAULT_LOCKFILE_PREFIX "olsrd"
#endif /* defined _WIN32 */

/**
 * @param cnf the olsrd configuration
 * @param ip_version the ip version
 * @return a malloc-ed string for the default lock file name
 */
char * olsrd_get_default_lockfile(struct olsrd_config *cnf) {
  char buf[FILENAME_MAX];
  int ipv = (cnf->ip_version == AF_INET) ? 4 : 6;

#ifndef DEFAULT_LOCKFILE_PREFIX
  snprintf(buf, sizeof(buf), "%s-ipv%d.lock", cnf->configuration_file ? cnf->configuration_file : "olsrd", ipv);
#else
  snprintf(buf, sizeof(buf), "%s-ipv%d.lock", DEFAULT_LOCKFILE_PREFIX, ipv);
#endif /* DEFAULT_LOCKFILE_PREFIX */

  return strdup(buf);
}
