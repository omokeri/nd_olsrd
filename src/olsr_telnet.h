/*
 * olsr_telnet.h
 *
 *  Created on: 22.03.2013
 *      Author: Christian Pointner <equinox@chaos-at-home.org>
 */

#ifndef OLSR_TELNET_H_
#define OLSR_TELNET_H_

#include <stdarg.h>
#include "olsr_types.h"

struct telnet_server {
  char* prompt;
  int default_client_buf_size;
  struct telnet_client* clients;
  struct telnet_cmd* cmd_table;

  union olsr_sockaddr sst;
  socklen_t addrlen;
  int fd;
};

void olsr_telnet_prepare(struct telnet_server*, union olsr_ip_addr, int);
int olsr_telnet_init(struct telnet_server*);
void olsr_telnet_exit(struct telnet_server*);

struct telnet_client {
  int fd;
  struct autobuf out;
  struct autobuf in;
  struct telnet_server* server;
  struct telnet_client* next;
};

void olsr_telnet_client_quit(struct telnet_client*);
void olsr_telnet_client_printf(struct telnet_client*, const char*, ...) __attribute__ ((format (printf, 2, 3)));

#endif /* OLSR_TELNET_H_ */
