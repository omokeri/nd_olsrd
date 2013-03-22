/*
 * olsr_telnet.h
 *
 *  Created on: 22.03.2013
 *      Author: Christian Pointner <equinox@chaos-at-home.org>
 */

#ifndef OLSR_TELNET_H_
#define OLSR_TELNET_H_

#include "olsr_types.h"

struct telnet_server {
  char* prompt;
  struct telnet_client* clients;
  struct telnet_cmd* cmd_table;

  union olsr_sockaddr sst;
  socklen_t addrlen;
  int fd;
};

void olsr_telnet_create(struct telnet_server*, union olsr_ip_addr, int);
int olsr_telnet_init(struct telnet_server*);
void olsr_telnet_exit(struct telnet_server*);

#endif /* OLSR_TELNET_H_ */
