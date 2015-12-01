/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004, Andreas Tonnesen(andreto@olsr.org)
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

#include "olsrd_plugin.h"
#include "../../info/olsrd_info.h"
#include "jsoninfo_printers.h"

#define PLUGIN_NAME "JSONINFO"
#define PLUGIN_TITLE    "OLSRD jsoninfo plugin"
#define PLUGIN_VERSION "0.0"
#define PLUGIN_AUTHOR   "Hans-Christoph Steiner"
#define MOD_DESC PLUGIN_TITLE " " PLUGIN_VERSION " by " PLUGIN_AUTHOR
#define PLUGIN_INTERFACE_VERSION 5

static info_plugin_functions_t functions = { //
    //
        .init = &plugin_init, //
        .is_command = &isCommand, //
        .determine_mime_type = &determine_mime_type, //
        .output_start = &output_start, //
        .output_end = &output_end, //
        .neighbors = &ipc_print_neighbors, //
        .links = &ipc_print_links, //
        .routes = &ipc_print_routes, //
        .topology = &ipc_print_topology, //
        .hna = &ipc_print_hna, //
        .mid = &ipc_print_mid, //
        .gateways = &ipc_print_gateways, //
        .sgw = &ipc_print_sgw, //
        .version = &ipc_print_version, //
        .olsrd_conf = &ipc_print_olsrd_conf, //
        .interfaces = &ipc_print_interfaces, //
        .config = &ipc_print_config, //
        .plugins = &ipc_print_plugins //
    };


info_plugin_config_t config;
char uuidfile[FILENAME_MAX];

static void my_init(void) __attribute__ ((constructor));
static void my_fini(void) __attribute__ ((destructor));

/**
 *Constructor
 */
static void my_init(void) {
  /* Print plugin info to stdout */
  printf("%s\n", MOD_DESC);

  /* defaults for parameters */
  config.ipc_port = 9090;
  config.http_headers = true;
  config.allow_localhost = false;
  config.ipv6_only = false;

  if (olsr_cnf->ip_version == AF_INET) {
    config.accept_ip.v4.s_addr = htonl(INADDR_LOOPBACK);
    config.listen_ip.v4.s_addr = htonl(INADDR_ANY);
  } else {
    config.accept_ip.v6 = in6addr_loopback;
    config.listen_ip.v6 = in6addr_any;
  }

  /* highlite neighbours by default */
  config.nompr = 0;

  memset(uuidfile, 0, sizeof(uuidfile));
}

/**
 *Destructor
 */
static void my_fini(void) {
  /* Calls the destruction function
   * olsr_plugin_exit()
   * This function should be present in your
   * sourcefile and all data destruction
   * should happen there - NOT HERE!
   */
  olsr_plugin_exit();
}

/**
 *Do initialization here
 *
 *This function is called by the my_init
 *function in uolsrd_plugin.c
 */
int olsrd_plugin_init(void) {
  return info_plugin_init(PLUGIN_NAME, &functions, &config);
}

/**
 * destructor - called at unload
 */
void olsr_plugin_exit(void) {
  info_plugin_exit();
}

int olsrd_plugin_interface_version(void) {
  return PLUGIN_INTERFACE_VERSION;
}

static const struct olsrd_plugin_parameters plugin_parameters[] = { //
    //
        { .name = "port", .set_plugin_parameter = &set_plugin_port, .data = &config.ipc_port }, //
        { .name = "accept", .set_plugin_parameter = &set_plugin_ipaddress, .data = &config.accept_ip }, //
        { .name = "listen", .set_plugin_parameter = &set_plugin_ipaddress, .data = &config.listen_ip }, //
        { .name = "httpheaders", .set_plugin_parameter = &set_plugin_boolean, .data = &config.http_headers }, //
        { .name = "allowlocalhost", .set_plugin_parameter = &set_plugin_boolean, .data = &config.allow_localhost }, //
        { .name = "ipv6only", .set_plugin_parameter = &set_plugin_boolean, .data = &config.ipv6_only }, //
        { .name = "uuidfile", .set_plugin_parameter = &set_plugin_string, .data = uuidfile, .addon = { .ui = FILENAME_MAX - 1 } } //
    };

void olsrd_get_plugin_parameters(const struct olsrd_plugin_parameters **params, int *size) {
  *params = plugin_parameters;
  *size = sizeof(plugin_parameters) / sizeof(*plugin_parameters);
}

/*
 * Local Variables:
 * mode: c
 * style: linux
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
