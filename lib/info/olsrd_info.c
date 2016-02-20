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
#include <ctype.h>

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

static char sink_buffer[4096];

static const char * name = NULL;

static info_plugin_functions_t *functions = NULL;

static info_plugin_config_t *config = NULL;

static int ipc_socket = -1;

static info_plugin_outbuffer_t outbuffer;

static struct timer_entry *writetimer_entry = NULL;

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

static unsigned int determine_action(char *requ) {
  static unsigned int SIW_ENTRIES[] = {
  //
      SIW_OLSRD_CONF,//
      SIW_ALL, //
      //
      // these are the two overarching categories
      SIW_RUNTIME_ALL,//
      SIW_STARTUP_ALL, //
      //
      // these are the individual sections
      SIW_NEIGHBORS,//
      SIW_LINKS, //
      SIW_ROUTES, //
      SIW_HNA, //
      SIW_MID, //
      SIW_TOPOLOGY, //
      SIW_GATEWAYS, //
      SIW_INTERFACES, //
      SIW_2HOP, //
      SIW_SGW, //
      //
      // specials
      SIW_VERSION,//
      SIW_CONFIG, //
      SIW_PLUGINS, //
      //
      // Freifunk special
      SIW_NEIGHBORS_FREIFUNK //
      };

  unsigned int i;

  if (!functions->is_command)
    return 0;

  for (i = 0; i < ARRAY_SIZE(SIW_ENTRIES); ++i) {
    unsigned int siw = SIW_ENTRIES[i];
    if (functions->is_command(requ, siw))
      return siw;
  }

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

    if ((result < 0) || (outbuffer.written[i] >= outbuffer.size[i])) {
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

  const char *content_type = functions->determine_mime_type ? functions->determine_mime_type(send_what) : "text/plain; charset=utf-8";
  int contentLengthIndex = 0;
  int headerLength = 0;

  abuf_init(&abuf, 2 * 4096);

  if (config->http_headers) {
    http_header_build(name, HTTP_200, content_type, &abuf, &contentLengthIndex);
    headerLength = abuf.len;
  }

  // only add if normal format
  if (send_what & SIW_ALL) {
    typedef struct {
      unsigned int siw;
      printer_generic func;
    } SiwLookupTableEntry;

    SiwLookupTableEntry funcs[] = {
      { SIW_NEIGHBORS , functions->neighbors  }, //
      { SIW_LINKS     , functions->links      }, //
      { SIW_ROUTES    , functions->routes     }, //
      { SIW_HNA       , functions->hna        }, //
      { SIW_MID       , functions->mid        }, //
      { SIW_TOPOLOGY  , functions->topology   }, //
      { SIW_GATEWAYS  , functions->gateways   }, //
      { SIW_INTERFACES, functions->interfaces }, //
      { SIW_2HOP      , functions->twohop     }, //
      { SIW_SGW       , functions->sgw        }, //
      //
      { SIW_VERSION, functions->version }, //
      { SIW_CONFIG, functions->config }, //
      { SIW_PLUGINS, functions->plugins } //
      };

    unsigned int i;

    if (functions->output_start) {
      functions->output_start(&abuf);
    }

    for (i = 0; i < ARRAY_SIZE(funcs); i++) {
      if (send_what & funcs[i].siw) {
        printer_generic func = funcs[i].func;
        if (func) {
          func(&abuf);
        }
      }
    }

    if (functions->output_end) {
      functions->output_end(&abuf);
    }
  } else if ((send_what & SIW_OLSRD_CONF) && functions->olsrd_conf) {
    /* this outputs the olsrd.conf text directly, not normal format */
    functions->olsrd_conf(&abuf);
  }

  if (!abuf.len) {
    /* wget can't handle output of zero length */
    abuf_puts(&abuf, "\n");
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

static char * skipLeadingWhitespace(char * requ, size_t *len) {
  while (isspace(*requ) && (*requ != '\0')) {
    *len = *len - 1;
    requ++;
  }
  return requ;
}

static char * stripEOLs(char * requ, size_t *len) {
  while (isspace(requ[*len - 1]) && (requ[*len - 1] != '\0')) {
    *len = *len - 1;
    requ[*len] = '\0';
  }
  return requ;
}

static char * cutAtFirstEOL(char * requ, size_t *len) {
  char * s = requ;
  size_t l = 0;
  while (!((*s == '\n') || (*s == '\r')) && (*s != '\0')) {
    s++;
    l++;
  }
  if ((*s == '\n') || (*s == '\r')) {
    *s = '\0';
  }
  *len = l;
  return requ;
}

static char * parseRequest(char * requ, size_t *len) {
  char * req = requ;

  if (!req || !*len) {
    return requ;
  }

  req = skipLeadingWhitespace(req, len);
  req = stripEOLs(req, len);

  /* HTTP request: GET whitespace URI whitespace HTTP/1.1 */
  if (*len < (3 + 1 + 1 + 1 + 8)) {
    return req;
  }

  if (strncasecmp(req, "GET", 3) || !isspace(req[3])) {
    /* does not start with 'GET ' */
    return req;
  }

  /* skip 'GET ' and further leading whitespace */
  req = skipLeadingWhitespace(&req[4], len);
  if (!*len) return req;

  /* cut req at the first '\n' */
  req = cutAtFirstEOL(req, len);
  if (!*len) return req;

  /* strip req of trailing EOL and whitespace */
  req = stripEOLs(req, len);
  if (*len < 9) return req;

  if (!isspace(req[*len - 9]) //
      || strncasecmp(&req[*len - 8], "HTTP/1.", 7) //
      || ((req[*len - 1] != '1') && (req[*len - 1] != '0'))) {
    return req;
  }
  *len = *len - 8;
  req[*len] = '\0';
  if (!*len) return req;

  /* strip req of trailing EOL and whitespace */
  req = stripEOLs(req, len);

  return req;
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
  if (!inet_ntop( //
      olsr_cnf->ip_version, //
      (olsr_cnf->ip_version == AF_INET) ? (void *) &sock_addr.in4.sin_addr : (void *) &sock_addr.in6.sin6_addr, //
      addr, //
      sizeof(addr))) {
    addr[0] = '\0';
  }
#endif /* NODEBUG */

  tv.tv_sec = tv.tv_usec = 0;
  if (olsr_cnf->ip_version == AF_INET) {
    if (!ip4equal(&sock_addr.in4.sin_addr, &config->accept_ip.v4) && (config->accept_ip.v4.s_addr != INADDR_ANY) //
        && (!config->allow_localhost || (ntohl(sock_addr.in4.sin_addr.s_addr) != INADDR_LOOPBACK))) {
#ifndef NODEBUG
      olsr_printf(1, "(%s) From host(%s) not allowed!\n", name, addr);
#endif /* NODEBUG */
      close(ipc_connection);
      return;
    }
  } else {
    /* Use in6addr_any (::) in olsr.conf to allow anybody. */
    if (!ip6equal(&sock_addr.in6.sin6_addr, &config->accept_ip.v6) && !ip6equal(&config->accept_ip.v6, &in6addr_any)) {
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

    if (s >= (ssize_t) (sizeof(requ) - 1)) {
      /* input was much too long, just skip the rest */
      while (recv(ipc_connection, (void *) &sink_buffer, sizeof(sink_buffer), 0) == sizeof(sink_buffer))
        ;
      s = 0;
    }

    if (0 < s) {
      char * req = requ;
      req[s] = '\0';
      req = parseRequest(req, (size_t*)&s);
      send_what = determine_action(req);
    }

    if (!send_what)
      send_what = SIW_ALL;
  }

  send_info(send_what, ipc_connection);
}

static int plugin_ipc_init(void) {
  union olsr_sockaddr sock_addr;
  uint32_t yes = 1;
  socklen_t sock_addr_len;

  /* Init ipc socket */
  if ((ipc_socket = socket(olsr_cnf->ip_version, SOCK_STREAM, 0)) == -1) {
#ifndef NODEBUG
    olsr_printf(1, "(%s) socket()=%s\n", name, strerror(errno));
#endif /* NODEBUG */
    goto error_out;
  }

  if (setsockopt(ipc_socket, SOL_SOCKET, SO_REUSEADDR, (char *) &yes, sizeof(yes)) < 0) {
#ifndef NODEBUG
    olsr_printf(1, "(%s) setsockopt()=%s\n", name, strerror(errno));
#endif /* NODEBUG */
    goto error_out;
  }

#if (defined __FreeBSD__ || defined __FreeBSD_kernel__) && defined SO_NOSIGPIPE
  if (setsockopt(ipc_socket, SOL_SOCKET, SO_NOSIGPIPE, (char *) &yes, sizeof(yes)) < 0) {
    perror("SO_NOSIGPIPE failed");
    goto error_out;
  }
#endif /* (defined __FreeBSD__ || defined __FreeBSD_kernel__) && defined SO_NOSIGPIPE */

#if defined __linux__ && defined IPV6_V6ONLY
  if (config->ipv6_only && (olsr_cnf->ip_version == AF_INET6) //
      && (setsockopt(ipc_socket, IPPROTO_IPV6, IPV6_V6ONLY, (char *) &yes, sizeof(yes)) < 0)) {
    perror("IPV6_V6ONLY failed");
    goto error_out;
  }
#endif /* defined __linux__ && defined IPV6_V6ONLY */

  /* complete the socket structure */
  memset(&sock_addr, 0, sizeof(sock_addr));
  if (olsr_cnf->ip_version == AF_INET) {
    sock_addr.in4.sin_family = AF_INET;
    sock_addr_len = sizeof(struct sockaddr_in);
#ifdef SIN6_LEN
    sock_addr.in4.sin_len = sock_addr_len;
#endif /* SIN6_LEN */
    sock_addr.in4.sin_addr.s_addr = config->listen_ip.v4.s_addr;
    sock_addr.in4.sin_port = htons(config->ipc_port);
  } else {
    sock_addr.in6.sin6_family = AF_INET6;
    sock_addr_len = sizeof(struct sockaddr_in6);
#ifdef SIN6_LEN
    sock_addr.in6.sin6_len = sock_addr_len;
#endif /* SIN6_LEN */
    sock_addr.in6.sin6_addr = config->listen_ip.v6;
    sock_addr.in6.sin6_port = htons(config->ipc_port);
  }

  /* bind the socket to the port number */
  if (bind(ipc_socket, &sock_addr.in, sock_addr_len) == -1) {
#ifndef NODEBUG
    olsr_printf(1, "(%s) bind()=%s\n", name, strerror(errno));
#endif /* NODEBUG */
    goto error_out;
  }

  /* show that we are willing to listen */
  if (listen(ipc_socket, 1) == -1) {
#ifndef NODEBUG
    olsr_printf(1, "(%s) listen()=%s\n", name, strerror(errno));
#endif /* NODEBUG */
    goto error_out;
  }

  /* Register with olsrd */
  add_olsr_socket(ipc_socket, &ipc_action, NULL, NULL, SP_PR_READ);

#ifndef NODEBUG
  olsr_printf(2, "(%s) listening on port %d\n", name, config->ipc_port);
#endif /* NODEBUG */

  return 1;

  error_out: //
  if (ipc_socket >= 0) {
    close(ipc_socket);
    ipc_socket = -1;
  }
  return 0;
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

  if (functions->init) {
    functions->init(name);
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
