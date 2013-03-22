
/*
 * Copyright (c) 2005, Bruno Randolf <bruno.randolf@4g-systems.biz>
 * Copyright (c) 2004, Andreas Tonnesen(andreto-at-olsr.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the UniK olsr daemon nor the names of its contributors
 *   may be used to endorse or promote products derived from this software
 *   without specific prior written permission.
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
 *
 */

 /*
  * Example plugin for olsrd.org OLSR daemon
  * Only the bare minimum
  */

#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>

#include "olsrd_plugin.h"
#include "olsr.h"
#include "plugin_util.h"

#include "olsr_telnet.h"

#define PLUGIN_NAME    "telnet plugin"
#define PLUGIN_VERSION "0.1"
#define PLUGIN_AUTHOR   "Christian Pointner"
#define MOD_DESC PLUGIN_NAME " " PLUGIN_VERSION " by " PLUGIN_AUTHOR
#define PLUGIN_INTERFACE_VERSION 5

struct telnet_server server;

union olsr_ip_addr telnet_listen_ip;
int telnet_port;

struct string_list {
  char* string;
  struct string_list* next;
};
struct string_list* telnet_enabled_commands;

/****************************************************************************
 *                Functions that the plugin MUST provide                    *
 ****************************************************************************/

/**
 * Plugin interface version
 * Used by main olsrd to check plugin interface version
 */
int
olsrd_plugin_interface_version(void)
{
  return PLUGIN_INTERFACE_VERSION;
}

static int
add_plugin_string_list(const char *value, void *data, set_plugin_parameter_addon addon __attribute__ ((unused)))
{
  struct string_list **en = data;
  struct string_list* s = olsr_malloc(sizeof(struct string_list), __func__);
  s->string = strdup(value);
  if (!s->string) {
    printf("\n (TELNET PLUGIN) register param enable out of memory!\n");
    olsr_exit(__func__, EXIT_FAILURE);
  }
  s->next = *en;
  *en = s;

  return 0;
}

/**
 * Register parameters from config file
 * Called for all plugin parameters
 */
static const struct olsrd_plugin_parameters plugin_parameters[] = {
  {.name = "port",.set_plugin_parameter = &set_plugin_port,.data = &telnet_port},
  {.name = "listen",.set_plugin_parameter = &set_plugin_ipaddress,.data = &telnet_listen_ip},
  {.name = "enable",.set_plugin_parameter = &add_plugin_string_list,.data = &telnet_enabled_commands},
};

void
olsrd_get_plugin_parameters(const struct olsrd_plugin_parameters **params, int *size)
{
  *params = plugin_parameters;
  *size = sizeof(plugin_parameters) / sizeof(*plugin_parameters);
}

/**
 * Initialize plugin
 * Called after all parameters are passed
 */
int
olsrd_plugin_init(void)
{
  olsr_telnet_create(&server, telnet_listen_ip, telnet_port);
  return olsr_telnet_init(&server);
}

/****************************************************************************
 *              private constructor and destructor functions                *
 ****************************************************************************/

/* attention: make static to avoid name clashes */

static void my_init(void) __attribute__ ((constructor));
static void my_fini(void) __attribute__ ((destructor));

/**
 * Private Constructor
 */
static void
my_init(void)
{
      /* Print plugin info to stdout */
  printf("%s\n", MOD_DESC);

      /* defaults for parameters */
  telnet_port = 2023;
  if (olsr_cnf->ip_version == AF_INET) {
    telnet_listen_ip.v4.s_addr = htonl(INADDR_ANY);
  } else {
    telnet_listen_ip.v6 = in6addr_any;
  }
  telnet_enabled_commands = NULL;
}

/**
 * Private Destructor
 */
static void
my_fini(void)
{
  struct string_list* s;

  olsr_telnet_exit(&server);

  s = telnet_enabled_commands;
  while(s) {
    struct string_list* deletee = s;
    s = s->next;
    free(deletee->string);
    free(deletee);
  }
}
