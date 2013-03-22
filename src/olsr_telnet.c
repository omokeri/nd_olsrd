/*
 * olsr_telnet.h
 *
 *  Created on: 22.03.2013
 *      Author: Christian Pointner <equinox@chaos-at-home.org>
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

#ifdef __linux__
#include <fcntl.h>
#endif /* __linux__ */

#include "olsr.h"
#include "olsr_types.h"
#include "scheduler.h"

#include "olsr_telnet.h"

#ifdef _WIN32
#define close(x) closesocket(x)
#endif /* _WIN32 */


static int get_port(struct telnet_server* s)
{
  return olsr_cnf->ip_version == AF_INET ? ntohs(s->sst.in4.sin_port) : ntohs(s->sst.in6.sin6_port);
}

void
olsr_telnet_create(struct telnet_server* s, union olsr_ip_addr listen_ip, int port)
{
  if(!s)
    return;

  s->fd = -1;
  s->prompt = NULL;
  s->clients = NULL;
  s->cmd_table = NULL;

      /* complete the socket structure */
  memset(&(s->sst), 0, sizeof(s->sst));
  if (olsr_cnf->ip_version == AF_INET) {
    s->sst.in4.sin_family = AF_INET;
    s->addrlen = sizeof(struct sockaddr_in);
#ifdef SIN6_LEN
    s->sst.in4.sin_len = s->addrlen;
#endif /* SIN6_LEN */
    s->sst.in4.sin_addr.s_addr = listen_ip.v4.s_addr;
    s->sst.in4.sin_port = htons(port);
  } else {
    s->sst.in6.sin6_family = AF_INET6;
    s->addrlen = sizeof(struct sockaddr_in6);
#ifdef SIN6_LEN
    s->sst.in6.sin6_len = s->addrlen;
#endif /* SIN6_LEN */
    s->sst.in6.sin6_addr = listen_ip.v6;
    s->sst.in6.sin6_port = htons(port);
  }
}


static void telnet_action(int, void *, unsigned int);

int
olsr_telnet_init(struct telnet_server* s)
{
  uint32_t yes = 1;

  if(!s)
    return 1;

  /* Init telnet socket */
  if ((s->fd = socket(olsr_cnf->ip_version, SOCK_STREAM, 0)) == -1) {
    OLSR_PRINTF(1, "(TELNET) socket()=%s\n", strerror(errno));
    return 1;
  }

  if (setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, (char *)&yes, sizeof(yes)) < 0) {
    OLSR_PRINTF(1, "(TELNET) setsockopt()=%s\n", strerror(errno));
    return 1;
  }
#if (defined __FreeBSD__ || defined __FreeBSD_kernel__) && defined SO_NOSIGPIPE
  if (setsockopt(s->fd, SOL_SOCKET, SO_NOSIGPIPE, (char *)&yes, sizeof(yes)) < 0) {
    perror("SO_REUSEADDR failed");
    return 1;
  }
#endif /* (defined __FreeBSD__ || defined __FreeBSD_kernel__) && defined SO_NOSIGPIPE */

      /* bind the socket to the port number */
  if (bind(s->fd, &(s->sst.in), s->addrlen) == -1) {
    OLSR_PRINTF(1, "(TELNET) bind()=%s\n", strerror(errno));
    return 1;
  }

      /* show that we are willing to listen */
  if (listen(s->fd, 1) == -1) {
    OLSR_PRINTF(1, "(TELNET) listen()=%s\n", strerror(errno));
    return 1;
  }

      /* Register with olsrd */
  add_olsr_socket(s->fd, &telnet_action, NULL, NULL, SP_PR_READ);

  OLSR_PRINTF(2, "(TELNET) listening on port %d\n", get_port(s));

  return 0; //telnet_client_init();
}

void
olsr_telnet_exit(struct telnet_server* s)
{
  if(!s)
    return;

//  telnet_client_cleanup();

  if(s->fd != -1) {
    remove_olsr_socket(s->fd, &telnet_action, NULL);
    close(s->fd);
    s->fd = -1;
  }
}


static void
telnet_action(int fd, void *data __attribute__ ((unused)), unsigned int flags __attribute__ ((unused)))
{
  union olsr_sockaddr pin;
  socklen_t addrlen = sizeof(pin);

  char addr[INET6_ADDRSTRLEN];
  int client_fd;

  if ((client_fd = accept(fd, &pin.in, &addrlen)) == -1) {
    OLSR_PRINTF(1, "(TELNET) accept()=%s\n", strerror(errno));
    return;
  }

  if (olsr_cnf->ip_version == AF_INET) {
    if (inet_ntop(olsr_cnf->ip_version, &pin.in4.sin_addr, addr, INET6_ADDRSTRLEN) == NULL)
      addr[0] = '\0';
  } else {
    if (inet_ntop(olsr_cnf->ip_version, &pin.in6.sin6_addr, addr, INET6_ADDRSTRLEN) == NULL)
      addr[0] = '\0';
  }

  OLSR_PRINTF(2, "(TELNET) Connect from %s (client: %d)\n", addr, client_fd);

  /* c = telnet_client_add(client_fd, pin); */
  close(client_fd);
}
