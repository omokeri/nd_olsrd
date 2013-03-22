/*
 * olsr_telnet.h
 *
 *  Created on: 22.03.2013
 *      Author: Christian Pointner <equinox@chaos-at-home.org>
 */

struct telnet_server {
  short port;
  int fd;
  char* prompt;
  struct telnet_client* clients;
  struct telnet_cmd* cmd_table;
};

