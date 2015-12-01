/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004
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

/*
 * Dynamic linked library for the olsr.org olsr daemon
 */

#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>

#include "ipcalc.h"
#include "olsr.h"
#include "scheduler.h"
#include "../../info/http_headers.h"
#include "txtinfo_printers.h"

#include "olsrd_txtinfo.h"

#ifdef _WIN32
#define close(x) closesocket(x)
#endif /* _WIN32 */

/* defines to make txtinfo and jsoninfo look alike */
#define PLUGIN_NAME "TXTINFO"
#define info_accept_ip txtinfo_accept_ip
#define info_listen_ip txtinfo_listen_ip
#define info_ipv6_only txtinfo_ipv6_only
#ifdef TXTINFO_ALLOW_LOCALHOST
#define INFO_ALLOW_LOCALHOST TXTINFO_ALLOW_LOCALHOST
#endif

static int ipc_socket;

/* IPC initialization function */
static int plugin_ipc_init(void);

static void send_info(unsigned int /*send_what*/, int /*socket*/);

static void ipc_action(int, void *, unsigned int);

#define TXT_IPC_BUFSIZE 256

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

#define MAX_CLIENTS 3

static char *outbuffer[MAX_CLIENTS];
static size_t outbuffer_size[MAX_CLIENTS];
static size_t outbuffer_written[MAX_CLIENTS];
static int outbuffer_socket[MAX_CLIENTS];
static int outbuffer_count = 0;

static struct timer_entry *writetimer_entry;

static void determine_action(unsigned int *send_what, char *requ) {
  if (strstr(requ, "/con"))
    *send_what |= SIW_OLSRD_CONF;
  else if (strstr(requ, "/all"))
    *send_what = SIW_ALL;
  else {
    // these are the two overarching categories
    if (strstr(requ, "/runtime"))
      *send_what |= SIW_RUNTIME_ALL;
    if (strstr(requ, "/startup"))
      *send_what |= SIW_STARTUP_ALL;

    // these are the individual sections
    if (strstr(requ, "/nei"))
      *send_what |= SIW_NEIGHBORS;
    if (strstr(requ, "/lin"))
      *send_what |= SIW_LINKS;
    if (strstr(requ, "/rou"))
      *send_what |= SIW_ROUTES;
    if (strstr(requ, "/hna"))
      *send_what |= SIW_HNA;
    if (strstr(requ, "/mid"))
      *send_what |= SIW_MID;
    if (strstr(requ, "/top"))
      *send_what |= SIW_TOPOLOGY;
    if (strstr(requ, "/gat"))
      *send_what |= SIW_GATEWAYS;
    if (strstr(requ, "/int"))
      *send_what |= SIW_INTERFACES;
    if (strstr(requ, "/2ho"))
      *send_what |= SIW_2HOP;
    if (strstr(requ, "/sgw"))
      *send_what |= SIW_SGW;

    // specials
    if (strstr(requ, "/ver"))
      *send_what |= SIW_VERSION;
    if (strstr(requ, "/config"))
      *send_what |= SIW_CONFIG;
    if (strstr(requ, "/plugins"))
      *send_what |= SIW_PLUGINS;

    /* To print out neighbours only on the Freifunk Status
     * page the normal output is somewhat lengthy. The
     * header parsing is sufficient for standard wget.
     */
    if (strstr(requ, "/neighbours"))
      *send_what = SIW_NEIGHBORS | SIW_LINKS;
  }
}

static void plugin_init(void) {
  /* nothing to do */
}

/**
 *Do initialization here
 *
 *This function is called by the my_init
 *function in uolsrd_plugin.c
 */
int olsrd_plugin_init(void) {
  /* Initial IPC value */
  ipc_socket = -1;

  plugin_init();

  plugin_ipc_init();
  return 1;
}

/**
 * destructor - called at unload
 */
void olsr_plugin_exit(void) {
  if (ipc_socket != -1)
    close(ipc_socket);
}

static int plugin_ipc_init(void) {
  union olsr_sockaddr sst;
  uint32_t yes = 1;
  socklen_t addrlen;

  /* Init ipc socket */
  if ((ipc_socket = socket(olsr_cnf->ip_version, SOCK_STREAM, 0)) == -1) {
#ifndef NODEBUG
    olsr_printf(1, "("PLUGIN_NAME") socket()=%s\n", strerror(errno));
#endif /* NODEBUG */
    return 0;
  } else {
    if (setsockopt(ipc_socket, SOL_SOCKET, SO_REUSEADDR, (char *) &yes, sizeof(yes)) < 0) {
#ifndef NODEBUG
      olsr_printf(1, "("PLUGIN_NAME") setsockopt()=%s\n", strerror(errno));
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
    if (info_ipv6_only && olsr_cnf->ip_version == AF_INET6) {
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
      sst.in4.sin_addr.s_addr = info_listen_ip.v4.s_addr;
      sst.in4.sin_port = htons(ipc_port);
    } else {
      sst.in6.sin6_family = AF_INET6;
      addrlen = sizeof(struct sockaddr_in6);
#ifdef SIN6_LEN
      sst.in6.sin6_len = addrlen;
#endif /* SIN6_LEN */
      sst.in6.sin6_addr = info_listen_ip.v6;
      sst.in6.sin6_port = htons(ipc_port);
    }

    /* bind the socket to the port number */
    if (bind(ipc_socket, &sst.in, addrlen) == -1) {
#ifndef NODEBUG
      olsr_printf(1, "("PLUGIN_NAME") bind()=%s\n", strerror(errno));
#endif /* NODEBUG */
      return 0;
    }

    /* show that we are willing to listen */
    if (listen(ipc_socket, 1) == -1) {
#ifndef NODEBUG
      olsr_printf(1, "("PLUGIN_NAME") listen()=%s\n", strerror(errno));
#endif /* NODEBUG */
      return 0;
    }

    /* Register with olsrd */
    add_olsr_socket(ipc_socket, &ipc_action, NULL, NULL, SP_PR_READ);

#ifndef NODEBUG
    olsr_printf(2, "("PLUGIN_NAME") listening on port %d\n", ipc_port);
#endif /* NODEBUG */
  }
  return 1;
}

static void ipc_action(int fd, void *data __attribute__ ((unused)), unsigned int flags __attribute__ ((unused))) {
  union olsr_sockaddr pin;

  char addr[INET6_ADDRSTRLEN];
  fd_set rfds;
  struct timeval tv;
  unsigned int send_what = 0;
  int ipc_connection;

  socklen_t addrlen = sizeof(pin);

  if (outbuffer_count >= MAX_CLIENTS) {
    return;
  }

  if ((ipc_connection = accept(fd, &pin.in, &addrlen)) == -1) {
#ifndef NODEBUG
    olsr_printf(1, "("PLUGIN_NAME") accept()=%s\n", strerror(errno));
#endif /* NODEBUG */
    return;
  }

  tv.tv_sec = tv.tv_usec = 0;
  if (olsr_cnf->ip_version == AF_INET) {
    if (inet_ntop(olsr_cnf->ip_version, &pin.in4.sin_addr, addr, INET6_ADDRSTRLEN) == NULL)
      addr[0] = '\0';
    if (!ip4equal(&pin.in4.sin_addr, &info_accept_ip.v4) && info_accept_ip.v4.s_addr != INADDR_ANY) {
#ifdef INFO_ALLOW_LOCALHOST
      if (ntohl(pin.in4.sin_addr.s_addr) != INADDR_LOOPBACK) {
#endif /* INFO_ALLOW_LOCALHOST */
        olsr_printf(1, "("PLUGIN_NAME") From host(%s) not allowed!\n", addr);
        close(ipc_connection);
        return;
#ifdef INFO_ALLOW_LOCALHOST
      }
#endif /* INFO_ALLOW_LOCALHOST */
    }
  } else {
    if (inet_ntop(olsr_cnf->ip_version, &pin.in6.sin6_addr, addr, INET6_ADDRSTRLEN) == NULL)
      addr[0] = '\0';
    /* Use in6addr_any (::) in olsr.conf to allow anybody. */
    if (!ip6equal(&in6addr_any, &info_accept_ip.v6) && !ip6equal(&pin.in6.sin6_addr, &info_accept_ip.v6)) {
      olsr_printf(1, "("PLUGIN_NAME") From host(%s) not allowed!\n", addr);
      close(ipc_connection);
      return;
    }
  }

#ifndef NODEBUG
  olsr_printf(2, "("PLUGIN_NAME") Connect from %s\n", addr);
#endif /* NODEBUG */

  /* purge read buffer to prevent blocking on linux */
  FD_ZERO(&rfds);
  FD_SET((unsigned int )ipc_connection, &rfds); /* Win32 needs the cast here */
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
      determine_action(&send_what, requ);
    }

    if (!send_what)
      send_what = SIW_ALL;
  }

  send_info(send_what, ipc_connection);
}

static void info_write_data(void *foo __attribute__ ((unused))) {
  fd_set set;
  int result, i, j, max;
  struct timeval tv;

  FD_ZERO(&set);
  max = 0;
  for (i = 0; i < outbuffer_count; i++) {
    /* And we cast here since we get a warning on Win32 */
    FD_SET((unsigned int )(outbuffer_socket[i]), &set);

    if (outbuffer_socket[i] > max) {
      max = outbuffer_socket[i];
    }
  }

  tv.tv_sec = 0;
  tv.tv_usec = 0;

  result = select(max + 1, NULL, &set, NULL, &tv);
  if (result <= 0) {
    return;
  }

  for (i = 0; i < outbuffer_count; i++) {
    if (FD_ISSET(outbuffer_socket[i], &set)) {
      result = send(outbuffer_socket[i], outbuffer[i] + outbuffer_written[i], outbuffer_size[i] - outbuffer_written[i], 0);
      if (result > 0) {
        outbuffer_written[i] += result;
      }

      if (result <= 0 || outbuffer_written[i] == outbuffer_size[i]) {
        /* close this socket and cleanup*/
        close(outbuffer_socket[i]);
        free(outbuffer[i]);

        for (j = i + 1; j < outbuffer_count; j++) {
          outbuffer[j - 1] = outbuffer[j];
          outbuffer_size[j - 1] = outbuffer_size[j];
          outbuffer_socket[j - 1] = outbuffer_socket[j];
          outbuffer_written[j - 1] = outbuffer_written[j];
        }
        outbuffer_count--;
      }
    }
  }
  if (!outbuffer_count) {
    olsr_stop_timer(writetimer_entry);
  }
}

static void send_info(unsigned int send_what, int the_socket) {
  struct autobuf abuf;

  const char *content_type = "text/plain; charset=utf-8";
  int contentLengthPlaceholderStart = 0;
  int headerLength = 0;

  abuf_init(&abuf, 2 * 4096);

  if (http_headers) {
    build_http_header(PLUGIN_NAME, HTTP_200, content_type, &abuf, &contentLengthPlaceholderStart);
    headerLength = abuf.len;
  }

  // only add if normal format
  if (send_what & SIW_ALL) {
    if (send_what & SIW_LINKS)
      ipc_print_links(&abuf);
    if (send_what & SIW_NEIGHBORS)
      ipc_print_neighbors(&abuf, false);
    if (send_what & SIW_TOPOLOGY)
      ipc_print_topology(&abuf);
    if (send_what & SIW_HNA)
      ipc_print_hna(&abuf);
    if (send_what & SIW_SGW)
      ipc_print_sgw(&abuf);
    if (send_what & SIW_MID)
      ipc_print_mid(&abuf);
    if (send_what & SIW_ROUTES)
      ipc_print_routes(&abuf);
    if (send_what & SIW_GATEWAYS)
      ipc_print_gateways(&abuf);
    if (send_what & SIW_CONFIG) {
      /* not supported */
    }
    if (send_what & SIW_INTERFACES)
      ipc_print_interfaces(&abuf);
    if (send_what & SIW_2HOP)
      ipc_print_neighbors(&abuf, true);
    if (send_what & SIW_VERSION)
      ipc_print_version(&abuf);
    if (send_what & SIW_PLUGINS) {
      /* not supported */
    }
  } else if (send_what & SIW_OLSRD_CONF) {
    /* this outputs the olsrd.conf text directly, not normal format */
    ipc_print_olsrd_conf(&abuf);
  }

  if (http_headers) {
    http_header_adjust_content_length(&abuf, contentLengthPlaceholderStart, abuf.len - headerLength);
  }

  /* avoid a memcpy: just move the abuf.buf pointer and clear abuf */
  outbuffer[outbuffer_count] = abuf.buf;
  outbuffer_size[outbuffer_count] = abuf.len;
  outbuffer_written[outbuffer_count] = 0;
  outbuffer_socket[outbuffer_count] = the_socket;
  abuf.buf = NULL;
  abuf.len = 0;
  abuf.size = 0;

  outbuffer_count++;

  if (outbuffer_count == 1) {
    writetimer_entry = olsr_start_timer(100, 0, OLSR_TIMER_PERIODIC, &info_write_data, NULL, 0);
  }

  abuf_free(&abuf);
}

/*
 * Local Variables:
 * mode: c
 * style: linux
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
