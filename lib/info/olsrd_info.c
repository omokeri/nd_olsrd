/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2015
 *
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

#include <arpa/inet.h>
#include <unistd.h>
#include <assert.h>

#include "olsrd_info.h"
#include "olsr.h"
#include "scheduler.h"
#include "ipcalc.h"
#include "http_headers.h"

#ifdef _WIN32
#define close(x) closesocket(x)
#endif /* _WIN32 */

#define MAX_CLIENTS 3

typedef struct {
  int socket[MAX_CLIENTS];
  char *buffer[MAX_CLIENTS];
  size_t size[MAX_CLIENTS];
  size_t written[MAX_CLIENTS];
  int count;
} info_plugin_outbuffer_t;

static const char * name = NULL;

static info_plugin_functions_t *functions = NULL;

static info_plugin_config_t *config = NULL;

static int ipc_socket = -1;

static info_plugin_outbuffer_t outbuffer;

static struct timer_entry *writetimer_entry = NULL;

static unsigned int determine_action(char *requ) {
  if (!(*functions).is_command)
    return 0;

  if ((*(*functions).is_command)(requ, SIW_OLSRD_CONF))
    return SIW_OLSRD_CONF;

  if ((*(*functions).is_command)(requ, SIW_ALL))
    return SIW_ALL;

  // these are the two overarching categories
  if ((*(*functions).is_command)(requ, SIW_RUNTIME_ALL))
    return SIW_RUNTIME_ALL;

  if ((*(*functions).is_command)(requ, SIW_STARTUP_ALL))
    return SIW_STARTUP_ALL;

  // these are the individual sections

  if ((*(*functions).is_command)(requ, SIW_NEIGHBORS))
    return SIW_NEIGHBORS;

  if ((*(*functions).is_command)(requ, SIW_LINKS))
    return SIW_LINKS;

  if ((*(*functions).is_command)(requ, SIW_ROUTES))
    return SIW_ROUTES;

  if ((*(*functions).is_command)(requ, SIW_HNA))
    return SIW_HNA;

  if ((*(*functions).is_command)(requ, SIW_MID))
    return SIW_MID;

  if ((*(*functions).is_command)(requ, SIW_TOPOLOGY))
    return SIW_TOPOLOGY;

  if ((*(*functions).is_command)(requ, SIW_GATEWAYS))
    return SIW_GATEWAYS;

  if ((*(*functions).is_command)(requ, SIW_INTERFACES))
    return SIW_INTERFACES;

  if ((*(*functions).is_command)(requ, SIW_2HOP))
    return SIW_2HOP;

  if ((*(*functions).is_command)(requ, SIW_SGW))
    return SIW_SGW;

  // specials

  if ((*(*functions).is_command)(requ, SIW_VERSION))
    return SIW_VERSION;

  if ((*(*functions).is_command)(requ, SIW_CONFIG))
    return SIW_CONFIG;

  if ((*(*functions).is_command)(requ, SIW_PLUGINS))
    return SIW_PLUGINS;

  /* To print out neighbours only on the Freifunk Status
   * page the normal output is somewhat lengthy. The
   * header parsing is sufficient for standard wget.
   */

  if ((*(*functions).is_command)(requ, SIW_NEIGHBORS_FREIFUNK))
    return SIW_NEIGHBORS_FREIFUNK;

  return 0;
}

static void write_data(void *foo __attribute__ ((unused))) {
  fd_set set;
  int result, i, max;
  struct timeval tv;

  if (outbuffer.count <= 0) {
    /* exit early if there is nothing to send */
    return;
  }

  FD_ZERO(&set);
  max = 0;
  for (i = 0; i < outbuffer.count; i++) {
    if (outbuffer.socket[i] < 0) {
      continue;
    }

    /* And we cast here since we get a warning on Win32 */
    FD_SET((unsigned int ) (outbuffer.socket[i]), &set);

    if (outbuffer.socket[i] > max) {
      max = outbuffer.socket[i];
    }
  }

  tv.tv_sec = 0;
  tv.tv_usec = 0;

  result = select(max + 1, NULL, &set, NULL, &tv);
  if (result <= 0) {
    /* exit early if any of the sockets is not ready for writing */
    return;
  }

  for (i = 0; i < MAX_CLIENTS; i++) {
    if (outbuffer.socket[i] < 0) {
      continue;
    }

    result = send(outbuffer.socket[i], outbuffer.buffer[i] + outbuffer.written[i], outbuffer.size[i] - outbuffer.written[i], 0);
    if (result > 0) {
      outbuffer.written[i] += result;
    }

    if ((result <= 0) || (outbuffer.written[i] >= outbuffer.size[i])) {
      /* close this socket and cleanup*/
      close(outbuffer.socket[i]);
      outbuffer.socket[i] = -1;
      free(outbuffer.buffer[i]);
      outbuffer.buffer[i] = NULL;
      outbuffer.size[i] = 0;
      outbuffer.written[i] = 0;

      outbuffer.count--;
    }
  }

  if (!outbuffer.count) {
    olsr_stop_timer(writetimer_entry);
  }
}

static void send_info(unsigned int send_what, int the_socket) {
  struct autobuf abuf;

  const char *content_type = ((*functions).determine_mime_type) ? (*(*functions).determine_mime_type)(send_what) : "text/plain; charset=utf-8";
  int contentLengthIndex = 0;
  int headerLength = 0;

  abuf_init(&abuf, 2 * 4096);

  if (config->http_headers) {
    http_header_build(name, HTTP_200, content_type, &abuf, &contentLengthIndex);
    headerLength = abuf.len;
  }

  // only add if normal format
  if (send_what & SIW_ALL) {
    if ((*functions).output_start)
      (*(*functions).output_start)(&abuf);

    if ((send_what & SIW_LINKS) && (*functions).links)
      (*(*functions).links)(&abuf);
    if ((send_what & SIW_NEIGHBORS) && (*functions).neighbors)
      (*(*functions).neighbors)(&abuf);
    if ((send_what & SIW_TOPOLOGY) && (*functions).topology)
      (*(*functions).topology)(&abuf);
    if ((send_what & SIW_HNA) && (*functions).hna)
      (*(*functions).hna)(&abuf);
    if ((send_what & SIW_SGW) && (*functions).sgw)
      (*(*functions).sgw)(&abuf);
    if ((send_what & SIW_MID) && (*functions).mid)
      (*(*functions).mid)(&abuf);
    if ((send_what & SIW_ROUTES) && (*functions).routes)
      (*(*functions).routes)(&abuf);
    if ((send_what & SIW_GATEWAYS) && (*functions).gateways)
      (*(*functions).gateways)(&abuf);
    if ((send_what & SIW_CONFIG) && (*functions).config)
      (*(*functions).config)(&abuf);
    if ((send_what & SIW_INTERFACES) && (*functions).interfaces)
      (*(*functions).interfaces)(&abuf);
    if ((send_what & SIW_2HOP) && (*functions).twohop)
      (*(*functions).twohop)(&abuf);
    if ((send_what & SIW_VERSION) && (*functions).version)
      (*(*functions).version)(&abuf);
    if ((send_what & SIW_PLUGINS) && (*functions).plugins)
      (*(*functions).plugins)(&abuf);

    if ((*functions).output_end)
      (*(*functions).output_end)(&abuf);
  } else if ((send_what & SIW_OLSRD_CONF) && (*functions).olsrd_conf) {
    /* this outputs the olsrd.conf text directly, not normal format */
    (*(*functions).olsrd_conf)(&abuf);
  }

  if (config->http_headers) {
    http_header_adjust_content_length(&abuf, contentLengthIndex, abuf.len - headerLength);
  }

  /* avoid a memcpy: just move the abuf.buf pointer and clear abuf */
  outbuffer.buffer[outbuffer.count] = abuf.buf;
  outbuffer.size[outbuffer.count] = abuf.len;
  outbuffer.written[outbuffer.count] = 0;
  outbuffer.socket[outbuffer.count] = the_socket;
  abuf.buf = NULL;
  abuf.len = 0;
  abuf.size = 0;

  outbuffer.count++;

  if (outbuffer.count == 1) {
    writetimer_entry = olsr_start_timer(100, 0, OLSR_TIMER_PERIODIC, &write_data, NULL, 0);
  }

  abuf_free(&abuf);
}

static void ipc_action(int fd, void *data __attribute__ ((unused)), unsigned int flags __attribute__ ((unused))) {
#ifndef NODEBUG
  char addr[INET6_ADDRSTRLEN];
#endif /* NODEBUG */

  union olsr_sockaddr sock_addr;
  socklen_t sock_addr_len = sizeof(sock_addr);
  fd_set rfds;
  struct timeval tv;
  unsigned int send_what = 0;
  int ipc_connection = -1;

  if (outbuffer.count >= MAX_CLIENTS) {
    return;
  }

  if ((ipc_connection = accept(fd, &sock_addr.in, &sock_addr_len)) == -1) {
#ifndef NODEBUG
    olsr_printf(1, "(%s) accept()=%s\n", name, strerror(errno));
#endif /* NODEBUG */
    return;
  }

#ifndef NODEBUG
  if (inet_ntop( //
      olsr_cnf->ip_version, //
      (olsr_cnf->ip_version == AF_INET) ? (void *) &sock_addr.in4.sin_addr : (void *) &sock_addr.in6.sin6_addr, //
      addr, //
      sizeof(addr)) == NULL) {
    addr[0] = '\0';
  }
#endif /* NODEBUG */

  tv.tv_sec = tv.tv_usec = 0;
  if (olsr_cnf->ip_version == AF_INET) {
    if (!ip4equal(&sock_addr.in4.sin_addr, &config->accept_ip.v4) && config->accept_ip.v4.s_addr != INADDR_ANY) {
      if (!config->allow_localhost || ntohl(sock_addr.in4.sin_addr.s_addr) != INADDR_LOOPBACK) {
#ifndef NODEBUG
        olsr_printf(1, "(%s) From host(%s) not allowed!\n", name, addr);
#endif /* NODEBUG */
        close(ipc_connection);
        return;
      }
    }
  } else {
    /* Use in6addr_any (::) in olsr.conf to allow anybody. */
    if (!ip6equal(&in6addr_any, &config->accept_ip.v6) && !ip6equal(&sock_addr.in6.sin6_addr, &config->accept_ip.v6)) {
#ifndef NODEBUG
      olsr_printf(1, "(%s) From host(%s) not allowed!\n", name, addr);
#endif /* NODEBUG */
      close(ipc_connection);
      return;
    }
  }

#ifndef NODEBUG
  olsr_printf(2, "(%s) Connect from %s\n", name, addr);
#endif /* NODEBUG */

  /* purge read buffer to prevent blocking on linux */
  FD_ZERO(&rfds);
  FD_SET((unsigned int ) ipc_connection, &rfds); /* Win32 needs the cast here */
  if (0 <= select(ipc_connection + 1, &rfds, NULL, NULL, &tv)) {
    char requ[1024];
    ssize_t s = recv(ipc_connection, (void *) &requ, sizeof(requ) - 1, 0); /* Win32 needs the cast here */

    if (s == sizeof(requ) - 1) {
      /* input was much too long, just skip the rest */
      char dummy[1024];

      while (recv(ipc_connection, (void *) &dummy, sizeof(dummy), 0) == sizeof(dummy))
        ;
    }

    if (0 < s) {
      requ[s] = 0;
      send_what = determine_action(requ);
    }

    if (!send_what)
      send_what = SIW_ALL;
  }

  send_info(send_what, ipc_connection);
}

static int plugin_ipc_init(void) {
  union olsr_sockaddr sst;
  uint32_t yes = 1;
  socklen_t addrlen;

  /* Init ipc socket */
  if ((ipc_socket = socket(olsr_cnf->ip_version, SOCK_STREAM, 0)) == -1) {
#ifndef NODEBUG
    olsr_printf(1, "(%s) socket()=%s\n", name, strerror(errno));
#endif /* NODEBUG */
    return 0;
  } else {
    if (setsockopt(ipc_socket, SOL_SOCKET, SO_REUSEADDR, (char *) &yes, sizeof(yes)) < 0) {
#ifndef NODEBUG
      olsr_printf(1, "(%s) setsockopt()=%s\n", name, strerror(errno));
#endif /* NODEBUG */
      return 0;
    }
#if (defined __FreeBSD__ || defined __FreeBSD_kernel__) && defined SO_NOSIGPIPE
    if (setsockopt(ipc_socket, SOL_SOCKET, SO_NOSIGPIPE, (char *) &yes, sizeof(yes)) < 0) {
      perror("SO_REUSEADDR failed");
      return 0;
    }
#endif /* (defined __FreeBSD__ || defined __FreeBSD_kernel__) && defined SO_NOSIGPIPE */
#if defined linux && defined IPV6_V6ONLY
    if (config->ipv6_only && olsr_cnf->ip_version == AF_INET6) {
      if (setsockopt(ipc_socket, IPPROTO_IPV6, IPV6_V6ONLY, (char *) &yes, sizeof(yes)) < 0) {
        perror("IPV6_V6ONLY failed");
        return 0;
      }
    }
#endif /* defined linux && defined IPV6_V6ONLY */
    /* Bind the socket */

    /* complete the socket structure */
    memset(&sst, 0, sizeof(sst));
    if (olsr_cnf->ip_version == AF_INET) {
      sst.in4.sin_family = AF_INET;
      addrlen = sizeof(struct sockaddr_in);
#ifdef SIN6_LEN
      sst.in4.sin_len = addrlen;
#endif /* SIN6_LEN */
      sst.in4.sin_addr.s_addr = config->listen_ip.v4.s_addr;
      sst.in4.sin_port = htons(config->ipc_port);
    } else {
      sst.in6.sin6_family = AF_INET6;
      addrlen = sizeof(struct sockaddr_in6);
#ifdef SIN6_LEN
      sst.in6.sin6_len = addrlen;
#endif /* SIN6_LEN */
      sst.in6.sin6_addr = config->listen_ip.v6;
      sst.in6.sin6_port = htons(config->ipc_port);
    }

    /* bind the socket to the port number */
    if (bind(ipc_socket, &sst.in, addrlen) == -1) {
#ifndef NODEBUG
      olsr_printf(1, "(%s) bind()=%s\n", name, strerror(errno));
#endif /* NODEBUG */
      return 0;
    }

    /* show that we are willing to listen */
    if (listen(ipc_socket, 1) == -1) {
#ifndef NODEBUG
      olsr_printf(1, "(%s) listen()=%s\n", name, strerror(errno));
#endif /* NODEBUG */
      return 0;
    }

    /* Register with olsrd */
    add_olsr_socket(ipc_socket, &ipc_action, NULL, NULL, SP_PR_READ);

#ifndef NODEBUG
    olsr_printf(2, "(%s) listening on port %d\n", name, config->ipc_port);
#endif /* NODEBUG */
  }
  return 1;
}

int info_plugin_init(const char * plugin_name, info_plugin_functions_t *plugin_functions, info_plugin_config_t *plugin_config) {
  int i;

  assert(plugin_name);
  assert(plugin_functions);
  assert(plugin_config);

  name = plugin_name;
  functions = plugin_functions;
  config = plugin_config;

  memset(&outbuffer, 0, sizeof(outbuffer));
  for (i = 0; i < MAX_CLIENTS; ++i) {
    outbuffer.socket[i] = -1;
  }

  ipc_socket = -1;

  if ((*functions).init) {
    (*(*functions).init)(name);
  }

  plugin_ipc_init();
  return 1;
}

void info_plugin_exit(void) {
  int i;

  if (ipc_socket != -1) {
    close(ipc_socket);
    ipc_socket = -1;
  }
  for (i = 0; i < MAX_CLIENTS; ++i) {
    if (outbuffer.buffer[i]) {
      free(outbuffer.buffer[i]);
      outbuffer.buffer[i] = NULL;
    }
    outbuffer.size[i] = 0;
    outbuffer.written[i] = 0;
    if (outbuffer.socket[i]) {
      close(outbuffer.socket[i]);
      outbuffer.socket[i] = -1;
    }
  }
  outbuffer.count = 0;
}
