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

#include "olsrd_jsoninfo.h"

#include <unistd.h>
#include <ctype.h>
#include <libgen.h>

#include "ipcalc.h"
#include "builddata.h"
#include "neighbor_table.h"
#include "mpr_selector_set.h"
#include "routing_table.h"
#include "mid_set.h"
#include "gateway.h"
#include "egressTypes.h"
#include "olsrd_jsoninfo_helpers.h"
#include "olsrd_plugin.h"
#include "../../info/info_types.h"

struct timeval start_time;

#ifdef __linux__
static bool isGwSelectable(struct gateway_entry * gw, bool ipv6) {
  if (!ipv6) {
    return gw->ipv4 //
        && ((olsr_cnf->ip_version == AF_INET) //
            || olsr_cnf->use_niit) //
        && (olsr_cnf->smart_gw_allow_nat //
            || !gw->ipv4nat);
  }

  return gw->ipv6 //
      && (olsr_cnf->ip_version == AF_INET6);
}
#endif /* __linux__ */

void plugin_init(const char *plugin_name) {
  /* Get start time */
  gettimeofday(&start_time, NULL);

  if (!strlen(uuidfile))
    strscpy(uuidfile, "uuid.txt", sizeof(uuidfile));
  read_uuid_from_file(plugin_name, uuidfile);
}

bool isCommand(const char *str, unsigned int siw) {
  switch (siw) {
    case SIW_OLSRD_CONF:
      return !strcmp(str, "/olsrd.conf");

    case SIW_ALL:
      return !strcmp(str, "/all");

    case SIW_RUNTIME_ALL:
      return !strcmp(str, "/runtime");

    case SIW_STARTUP_ALL:
      return !strcmp(str, "/startup");

    case SIW_NEIGHBORS:
      return !strcmp(str, "/neighbors");

    case SIW_LINKS:
      return !strcmp(str, "/links");

    case SIW_ROUTES:
      return !strcmp(str, "/routes");

    case SIW_HNA:
      return !strcmp(str, "/hna");

    case SIW_MID:
      return !strcmp(str, "/mid");

    case SIW_TOPOLOGY:
      return !strcmp(str, "/topology");

    case SIW_GATEWAYS:
      return !strcmp(str, "/gateways");

    case SIW_INTERFACES:
      return !strcmp(str, "/interfaces");

    case SIW_2HOP:
      return !strcmp(str, "/2hop");

    case SIW_SGW:
      return !strcmp(str, "/sgw");

    case SIW_VERSION:
      return !strcmp(str, "/version");

    case SIW_CONFIG:
      return !strcmp(str, "/config");

    case SIW_PLUGINS:
      return !strcmp(str, "/plugins");

    case SIW_NEIGHBORS_FREIFUNK:
      return !strcmp(str, "/neighbours");

    default:
      return false;
  }
}

const char * determine_mime_type(unsigned int send_what) {
  return (send_what & SIW_ALL) ? "application/json; charset=utf-8" : "text/plain; charset=utf-8";
}

void output_start(struct autobuf *abuf) {
  /* global variables for tracking when to put a comma in for JSON */
  abuf_json_reset_entry_number_and_depth();
  abuf_json_mark_output(true, abuf);

  abuf_json_int(abuf, "systemTime", time(NULL));
  abuf_json_int(abuf, "timeSinceStartup", now_times);
  if (*uuid)
    abuf_json_string(abuf, "uuid", uuid);
}

void output_end(struct autobuf *abuf) {
  abuf_json_mark_output(false, abuf);
  abuf_puts(abuf, "\n");
}

static void ipc_print_neighbors_internal(struct autobuf *abuf, bool list_2hop) {
  struct ipaddr_str buf1;
  struct neighbor_entry *neigh;
  struct neighbor_2_list_entry *list_2;

  if (!list_2hop)
    abuf_json_mark_object(true, true, abuf, "neighbors");
  else
    abuf_json_mark_object(true, true, abuf, "2hop");

  /* Neighbors */
  OLSR_FOR_ALL_NBR_ENTRIES(neigh)
      {
        abuf_json_mark_array_entry(true, abuf);

        abuf_json_string(abuf, "ipAddress", olsr_ip_to_string(&buf1, &neigh->neighbor_main_addr));
        abuf_json_boolean(abuf, "symmetric", (neigh->status == SYM));
        abuf_json_int(abuf, "willingness", neigh->willingness);
        abuf_json_boolean(abuf, "isMultiPointRelay", neigh->is_mpr);
        abuf_json_boolean(abuf, "wasMultiPointRelay", neigh->was_mpr);
        abuf_json_boolean(abuf, "multiPointRelaySelector", olsr_lookup_mprs_set(&neigh->neighbor_main_addr) != NULL);
        abuf_json_boolean(abuf, "skip", neigh->skip);
        abuf_json_int(abuf, "neighbor2nocov", neigh->neighbor_2_nocov);
        abuf_json_int(abuf, "linkcount", neigh->linkcount);

        if (!list_2hop) {
          int thop_cnt = 0;
          for (list_2 = neigh->neighbor_2_list.next; list_2 != &neigh->neighbor_2_list; list_2 = list_2->next) {
            thop_cnt++;
          }
          abuf_json_int(abuf, "twoHopNeighborCount", thop_cnt);
        } else {
          abuf_json_mark_object(true, true, abuf, "twoHopNeighbors");
          for (list_2 = neigh->neighbor_2_list.next; list_2 != &neigh->neighbor_2_list; list_2 = list_2->next) {
            abuf_json_mark_array_entry(true, abuf);
            abuf_json_string(abuf, "ipAddress", olsr_ip_to_string(&buf1, &list_2->neighbor_2->neighbor_2_addr));
            abuf_json_mark_array_entry(false, abuf);
          }
          abuf_json_mark_object(false, true, abuf, false);
        }

        abuf_json_mark_array_entry(false, abuf);
      }OLSR_FOR_ALL_NBR_ENTRIES_END(neigh);
  abuf_json_mark_object(false, true, abuf, NULL);
}

void ipc_print_neighbors(struct autobuf *abuf) {
  ipc_print_neighbors_internal(abuf, false);
}

void ipc_print_links(struct autobuf *abuf) {
  struct ipaddr_str buf1, buf2;
  struct lqtextbuffer lqbuffer1;

  struct link_entry *my_link = NULL;

  abuf_json_mark_object(true, true, abuf, "links");

  OLSR_FOR_ALL_LINK_ENTRIES(my_link)
      {
        const char* lqs;
        int diff = (unsigned int) (my_link->link_timer->timer_clock - now_times);

        abuf_json_mark_array_entry(true, abuf);
        abuf_json_string(abuf, "localIP", olsr_ip_to_string(&buf1, &my_link->local_iface_addr));
        abuf_json_string(abuf, "remoteIP", olsr_ip_to_string(&buf2, &my_link->neighbor_iface_addr));
        abuf_json_int(abuf, "validityTime", diff);
        lqs = get_link_entry_text(my_link, '\t', &lqbuffer1);
        abuf_json_float(abuf, "linkQuality", atof(lqs));
        abuf_json_float(abuf, "neighborLinkQuality", atof(strrchr(lqs, '\t')));
        if (my_link->linkcost >= LINK_COST_BROKEN)
          abuf_json_int(abuf, "linkCost", LINK_COST_BROKEN);
        else
          abuf_json_int(abuf, "linkCost", my_link->linkcost);
        abuf_json_mark_array_entry(false, abuf);
      }OLSR_FOR_ALL_LINK_ENTRIES_END(my_link);
  abuf_json_mark_object(false, true, abuf, NULL);
}

void ipc_print_routes(struct autobuf *abuf) {
  struct ipaddr_str buf1, buf2;
  struct rt_entry *rt;

  abuf_json_mark_object(true, true, abuf, "routes");

  /* Walk the route table */
  OLSR_FOR_ALL_RT_ENTRIES(rt)
      {
        abuf_json_mark_array_entry(true, abuf);
        abuf_json_string(abuf, "destination", olsr_ip_to_string(&buf1, &rt->rt_dst.prefix));
        abuf_json_int(abuf, "genmask", rt->rt_dst.prefix_len);
        abuf_json_string(abuf, "gateway", olsr_ip_to_string(&buf2, &rt->rt_best->rtp_nexthop.gateway));
        abuf_json_int(abuf, "metric", rt->rt_best->rtp_metric.hops);
        if (rt->rt_best->rtp_metric.cost >= ROUTE_COST_BROKEN)
          abuf_json_int(abuf, "rtpMetricCost", ROUTE_COST_BROKEN);
        else
          abuf_json_int(abuf, "rtpMetricCost", rt->rt_best->rtp_metric.cost);
        abuf_json_string(abuf, "networkInterface", if_ifwithindex_name(rt->rt_best->rtp_nexthop.iif_index));
        abuf_json_mark_array_entry(false, abuf);
      }OLSR_FOR_ALL_RT_ENTRIES_END(rt);

  abuf_json_mark_object(false, true, abuf, NULL);
}

void ipc_print_topology(struct autobuf *abuf) {
  struct tc_entry *tc;

  abuf_json_mark_object(true, true, abuf, "topology");

  /* Topology */
  OLSR_FOR_ALL_TC_ENTRIES(tc)
      {
        struct tc_edge_entry *tc_edge;
        OLSR_FOR_ALL_TC_EDGE_ENTRIES(tc, tc_edge)
            {
              if (tc_edge->edge_inv) {
                struct ipaddr_str dstbuf, addrbuf;
                struct lqtextbuffer lqbuffer1;
                uint32_t vt = tc->validity_timer != NULL ? (tc->validity_timer->timer_clock - now_times) : 0;
                int diff = (int) (vt);
                const char* lqs;
                abuf_json_mark_array_entry(true, abuf);
                abuf_json_string(abuf, "destinationIP", olsr_ip_to_string(&dstbuf, &tc_edge->T_dest_addr));
                abuf_json_string(abuf, "lastHopIP", olsr_ip_to_string(&addrbuf, &tc->addr));
                lqs = get_tc_edge_entry_text(tc_edge, '\t', &lqbuffer1);
                abuf_json_float(abuf, "linkQuality", atof(lqs));
                abuf_json_float(abuf, "neighborLinkQuality", atof(strrchr(lqs, '\t')));
                if (tc_edge->cost >= LINK_COST_BROKEN)
                  abuf_json_int(abuf, "tcEdgeCost", LINK_COST_BROKEN);
                else
                  abuf_json_int(abuf, "tcEdgeCost", tc_edge->cost);
                abuf_json_int(abuf, "validityTime", diff);
                abuf_json_mark_array_entry(false, abuf);
              }
            }OLSR_FOR_ALL_TC_EDGE_ENTRIES_END(tc, tc_edge);
      }OLSR_FOR_ALL_TC_ENTRIES_END(tc);

  abuf_json_mark_object(false, true, abuf, NULL);
}

void ipc_print_hna(struct autobuf *abuf) {
  struct hna_entry *tmp_hna;
  struct hna_net *tmp_net;
  struct ipaddr_str buf, mainaddrbuf;

  abuf_json_mark_object(true, true, abuf, "hna");

  OLSR_FOR_ALL_HNA_ENTRIES(tmp_hna)
        {

          /* Check all networks */
          for (tmp_net = tmp_hna->networks.next; tmp_net != &tmp_hna->networks; tmp_net = tmp_net->next) {
            uint32_t vt = tmp_net->hna_net_timer != NULL ? (tmp_net->hna_net_timer->timer_clock - now_times) : 0;
            int diff = (int) (vt);
            abuf_json_mark_array_entry(true, abuf);
            abuf_json_string(abuf, "destination", olsr_ip_to_string(&buf, &tmp_net->hna_prefix.prefix)), abuf_json_int(abuf, "genmask",
                tmp_net->hna_prefix.prefix_len);
            abuf_json_string(abuf, "gateway", olsr_ip_to_string(&mainaddrbuf, &tmp_hna->A_gateway_addr));
            abuf_json_int(abuf, "validityTime", diff);
            abuf_json_mark_array_entry(false, abuf);
          }
        }OLSR_FOR_ALL_HNA_ENTRIES_END(tmp_hna);

  abuf_json_mark_object(false, true, abuf, NULL);
}

void ipc_print_mid(struct autobuf *abuf) {
  int idx;
  struct mid_entry *entry;
  struct mid_address *alias;

  abuf_json_mark_object(true, true, abuf, "mid");

  /* MID */
  for (idx = 0; idx < HASHSIZE; idx++) {
    entry = mid_set[idx].next;

    while (entry != &mid_set[idx]) {
      struct ipaddr_str buf, buf2;
      abuf_json_mark_array_entry(true, abuf);
      abuf_json_string(abuf, "ipAddress", olsr_ip_to_string(&buf, &entry->main_addr));

      abuf_json_mark_object(true, true, abuf, "aliases");
      alias = entry->aliases;
      while (alias) {
        uint32_t vt = alias->vtime - now_times;
        int diff = (int) (vt);

        abuf_json_mark_array_entry(true, abuf);
        abuf_json_string(abuf, "ipAddress", olsr_ip_to_string(&buf2, &alias->alias));
        abuf_json_int(abuf, "validityTime", diff);
        abuf_json_mark_array_entry(false, abuf);

        alias = alias->next_alias;
      }
      abuf_json_mark_object(false, true, abuf, NULL); // aliases
      abuf_json_mark_array_entry(false, abuf);
      entry = entry->next;
    }
  }
  abuf_json_mark_object(false, true, abuf, NULL); // mid
}

#ifdef __linux__

static void ipc_print_gateways_ipvx(struct autobuf *abuf, bool ipv6) {
  abuf_json_mark_object(true, true, abuf, ipv6 ? "ipv6" : "ipv4");

  if (olsr_cnf->smart_gw_active) {
    struct gateway_entry * current_gw = olsr_get_inet_gateway(ipv6);
    struct gateway_entry * gw;
    OLSR_FOR_ALL_GATEWAY_ENTRIES(gw)
        {
          if (!(!ipv6 && gw->ipv4) && !(ipv6 && gw->ipv6)) {
            continue;
          }

          abuf_json_mark_array_entry(true, abuf);
          {
            struct tc_entry* tc = olsr_lookup_tc_entry(&gw->originator);

            abuf_json_boolean(abuf, "selected", current_gw && (current_gw == gw));
            abuf_json_boolean(abuf, "selectable", isGwSelectable(gw, ipv6));
            abuf_json_ip_address(abuf, "originator", &gw->originator);
            abuf_json_ip_address(abuf, "prefix", &gw->external_prefix.prefix);
            abuf_json_int(abuf, "prefixLen", gw->external_prefix.prefix_len);
            abuf_json_int(abuf, "uplink", gw->uplink);
            abuf_json_int(abuf, "downlink", gw->downlink);
            abuf_json_int(abuf, "pathcost", !tc ? ROUTE_COST_BROKEN : tc->path_cost);
            abuf_json_boolean(abuf, "IPv4", gw->ipv4);
            abuf_json_boolean(abuf, "IPv4-NAT", gw->ipv4nat);
            abuf_json_boolean(abuf, "IPv6", gw->ipv6);
            abuf_json_int(abuf, "cost", gw->path_cost);
            abuf_json_int(abuf, "hops", !tc ? 0 : tc->hops);
          }
          abuf_json_mark_array_entry(false, abuf);
        }OLSR_FOR_ALL_GATEWAY_ENTRIES_END(gw)
  }

  abuf_json_mark_object(false, true, abuf, NULL);
}
#endif /* __linux__ */

void ipc_print_gateways(struct autobuf *abuf) {
#ifndef __linux__
  abuf_json_string(abuf, "error", "Gateway mode is only supported in Linux");
#else /* __linux__ */
  abuf_json_mark_object(true, false, abuf, "gateways");

  ipc_print_gateways_ipvx(abuf, false);
  ipc_print_gateways_ipvx(abuf, true);

  abuf_json_mark_object(false, false, abuf, NULL);
#endif /* __linux__ */
}

#ifdef __linux__

/** interface names for smart gateway tunnel interfaces, IPv4 */
extern struct interfaceName * sgwTunnel4InterfaceNames;

/** interface names for smart gateway tunnel interfaces, IPv6 */
extern struct interfaceName * sgwTunnel6InterfaceNames;

/**
 * Construct the sgw table for a given ip version
 *
 * @param abuf the string buffer
 * @param ipv6 true for IPv6, false for IPv4
 * @param fmtv the format for printing
 */
static void sgw_ipvx(struct autobuf *abuf, bool ipv6) {
  struct interfaceName * sgwTunnelInterfaceNames = !ipv6 ? sgwTunnel4InterfaceNames : sgwTunnel6InterfaceNames;

  abuf_json_mark_object(true, true, abuf, ipv6 ? "ipv6" : "ipv4");

  if (olsr_cnf->smart_gw_active && sgwTunnelInterfaceNames) {
    struct gateway_entry * current_gw = olsr_get_inet_gateway(ipv6);
    int i;
    for (i = 0; i < olsr_cnf->smart_gw_use_count; i++) {
      struct interfaceName * node = &sgwTunnelInterfaceNames[i];
      struct gateway_entry * gw = node->gw;

      if (!gw) {
        continue;
      }

      abuf_json_mark_array_entry(true, abuf);
      {
        struct tc_entry* tc = olsr_lookup_tc_entry(&gw->originator);

        abuf_json_boolean(abuf, "selected", current_gw && (current_gw == gw));
        abuf_json_boolean(abuf, "selectable", isGwSelectable(gw, ipv6));
        abuf_json_ip_address(abuf, "originator", &gw->originator);
        abuf_json_ip_address(abuf, "prefix", &gw->external_prefix.prefix);
        abuf_json_int(abuf, "prefixLen", gw->external_prefix.prefix_len);
        abuf_json_int(abuf, "uplink", gw->uplink);
        abuf_json_int(abuf, "downlink", gw->downlink);
        abuf_json_int(abuf, "pathcost", !tc ? ROUTE_COST_BROKEN : tc->path_cost);
        abuf_json_boolean(abuf, "IPv4", gw->ipv4);
        abuf_json_boolean(abuf, "IPv4-NAT", gw->ipv4nat);
        abuf_json_boolean(abuf, "IPv6", gw->ipv6);
        abuf_json_int(abuf, "cost", gw->path_cost);
        abuf_json_ip_address(abuf, "destination", &gw->originator);
        abuf_json_string(abuf, "tunnel", node->name);
        abuf_json_int(abuf, "tableNr", node->tableNr);
        abuf_json_int(abuf, "ruleNr", node->ruleNr);
        abuf_json_int(abuf, "bypassRuleNr", node->bypassRuleNr);
      }
      abuf_json_mark_array_entry(false, abuf);
    }
  }

  abuf_json_mark_object(false, true, abuf, NULL);
}
#endif /* __linux__ */

void ipc_print_sgw(struct autobuf *abuf) {
#ifndef __linux__
  abuf_json_string(abuf, "error", "Gateway mode is only supported in Linux");
#else
  abuf_json_mark_object(true, false, abuf, "sgw");

  sgw_ipvx(abuf, false);
  sgw_ipvx(abuf, true);

  abuf_json_mark_object(false, false, abuf, NULL);
#endif /* __linux__ */
}

void ipc_print_version(struct autobuf *abuf) {
  abuf_json_mark_object(true, false, abuf, "version");

  abuf_json_string(abuf, "version", olsrd_version);
  abuf_json_string(abuf, "date", build_date);
  abuf_json_string(abuf, "host", build_host);

  abuf_json_mark_object(false, false, abuf, NULL);
}

void ipc_print_olsrd_conf(struct autobuf *abuf) {
  olsrd_write_cnf_autobuf(abuf, olsr_cnf);
}

void ipc_print_interfaces(struct autobuf *abuf) {
#ifdef __linux__
  int linklen;
  char path[PATH_MAX], linkpath[PATH_MAX];
#endif /* __linux__ */
  char ipv6_buf[INET6_ADDRSTRLEN]; /* buffer for IPv6 inet_htop */
  struct olsr_lq_mult *mult;
  const struct olsr_if *ifs;
  abuf_json_mark_object(true, true, abuf, "interfaces");
  for (ifs = olsr_cnf->interfaces; ifs != NULL ; ifs = ifs->next) {
    const struct interface_olsr * const rifs = ifs->interf;
    abuf_json_mark_array_entry(true, abuf);
    abuf_json_string(abuf, "name", ifs->name);

    abuf_json_mark_object(true, true, abuf, "linkQualityMultipliers");
    for (mult = ifs->cnf->lq_mult; mult != NULL ; mult = mult->next) {
      abuf_json_mark_array_entry(true, abuf);
      abuf_json_string(abuf, "route", inet_ntop(olsr_cnf->ip_version, &mult->addr, ipv6_buf, sizeof(ipv6_buf)));
      abuf_json_float(abuf, "multiplier", mult->value / 65535.0);
      abuf_json_mark_array_entry(false, abuf);
    }
    abuf_json_mark_object(false, true, abuf, NULL);

    if (!rifs) {
      abuf_json_string(abuf, "state", "down");
    } else {
      abuf_json_string(abuf, "state", "up");
      abuf_json_string(abuf, "nameFromKernel", rifs->int_name);
      abuf_json_int(abuf, "interfaceMode", rifs->mode);
      abuf_json_boolean(abuf, "emulatedHostClientInterface", rifs->is_hcif);
      abuf_json_boolean(abuf, "sendTcImmediately", rifs->immediate_send_tc);
      abuf_json_int(abuf, "fishEyeTtlIndex", rifs->ttl_index);
      abuf_json_int(abuf, "olsrForwardingTimeout", rifs->fwdtimer);
      abuf_json_int(abuf, "olsrMessageSequenceNumber", rifs->olsr_seqnum);
      abuf_json_int(abuf, "olsrInterfaceMetric", rifs->int_metric);
      abuf_json_int(abuf, "olsrMTU", rifs->int_mtu);
      abuf_json_int(abuf, "helloEmissionInterval", rifs->hello_etime);
      abuf_json_int(abuf, "helloValidityTime", me_to_reltime(rifs->valtimes.hello));
      abuf_json_int(abuf, "tcValidityTime", me_to_reltime(rifs->valtimes.tc));
      abuf_json_int(abuf, "midValidityTime", me_to_reltime(rifs->valtimes.mid));
      abuf_json_int(abuf, "hnaValidityTime", me_to_reltime(rifs->valtimes.hna));
      abuf_json_boolean(abuf, "wireless", rifs->is_wireless);

#ifdef __linux__
      abuf_json_boolean(abuf, "icmpRedirect", rifs->nic_state.redirect);
      abuf_json_boolean(abuf, "spoofFilter", rifs->nic_state.spoof);
#endif /* __linux__ */

      if (olsr_cnf->ip_version == AF_INET) {
        struct ipaddr_str addrbuf, maskbuf, bcastbuf;
        abuf_json_string(abuf, "ipv4Address", ip4_to_string(&addrbuf, rifs->int_addr.sin_addr));
        abuf_json_string(abuf, "netmask", ip4_to_string(&maskbuf, rifs->int_netmask.sin_addr));
        abuf_json_string(abuf, "broadcast", ip4_to_string(&bcastbuf, rifs->int_broadaddr.sin_addr));
      } else {
        struct ipaddr_str addrbuf, maskbuf;
        abuf_json_string(abuf, "ipv6Address", ip6_to_string(&addrbuf, &rifs->int6_addr.sin6_addr));
        abuf_json_string(abuf, "multicast", ip6_to_string(&maskbuf, &rifs->int6_multaddr.sin6_addr));
      }
    }
#ifdef __linux__
    snprintf(path, PATH_MAX, "/sys/class/net/%s/device/driver/module", ifs->name);
    linklen = readlink(path, linkpath, PATH_MAX - 1);
    if (linklen > 1) {
      linkpath[linklen] = '\0';
      abuf_json_string(abuf, "kernelModule", basename(linkpath));
    }

    abuf_json_sys_class_net(abuf, "addressLength", ifs->name, "addr_len");
    abuf_json_sys_class_net(abuf, "carrier", ifs->name, "carrier");
    abuf_json_sys_class_net(abuf, "dormant", ifs->name, "dormant");
    abuf_json_sys_class_net(abuf, "features", ifs->name, "features");
    abuf_json_sys_class_net(abuf, "flags", ifs->name, "flags");
    abuf_json_sys_class_net(abuf, "linkMode", ifs->name, "link_mode");
    abuf_json_sys_class_net(abuf, "macAddress", ifs->name, "address");
    abuf_json_sys_class_net(abuf, "ethernetMTU", ifs->name, "mtu");
    abuf_json_sys_class_net(abuf, "operationalState", ifs->name, "operstate");
    abuf_json_sys_class_net(abuf, "txQueueLength", ifs->name, "tx_queue_len");
    abuf_json_sys_class_net(abuf, "collisions", ifs->name, "statistics/collisions");
    abuf_json_sys_class_net(abuf, "multicastPackets", ifs->name, "statistics/multicast");
    abuf_json_sys_class_net(abuf, "rxBytes", ifs->name, "statistics/rx_bytes");
    abuf_json_sys_class_net(abuf, "rxCompressed", ifs->name, "statistics/rx_compressed");
    abuf_json_sys_class_net(abuf, "rxCrcErrors", ifs->name, "statistics/rx_crc_errors");
    abuf_json_sys_class_net(abuf, "rxDropped", ifs->name, "statistics/rx_dropped");
    abuf_json_sys_class_net(abuf, "rxErrors", ifs->name, "statistics/rx_errors");
    abuf_json_sys_class_net(abuf, "rxFifoErrors", ifs->name, "statistics/rx_fifo_errors");
    abuf_json_sys_class_net(abuf, "rxFrameErrors", ifs->name, "statistics/rx_frame_errors");
    abuf_json_sys_class_net(abuf, "rxLengthErrors", ifs->name, "statistics/rx_length_errors");
    abuf_json_sys_class_net(abuf, "rxMissedErrors", ifs->name, "statistics/rx_missed_errors");
    abuf_json_sys_class_net(abuf, "rxOverErrors", ifs->name, "statistics/rx_over_errors");
    abuf_json_sys_class_net(abuf, "rxPackets", ifs->name, "statistics/rx_packets");
    abuf_json_sys_class_net(abuf, "txAbortedErrors", ifs->name, "statistics/tx_aborted_errors");
    abuf_json_sys_class_net(abuf, "txBytes", ifs->name, "statistics/tx_bytes");
    abuf_json_sys_class_net(abuf, "txCarrierErrors", ifs->name, "statistics/tx_carrier_errors");
    abuf_json_sys_class_net(abuf, "txCompressed", ifs->name, "statistics/tx_compressed");
    abuf_json_sys_class_net(abuf, "txDropped", ifs->name, "statistics/tx_dropped");
    abuf_json_sys_class_net(abuf, "txErrors", ifs->name, "statistics/tx_errors");
    abuf_json_sys_class_net(abuf, "txFifoErrors", ifs->name, "statistics/tx_fifo_errors");
    abuf_json_sys_class_net(abuf, "txHeartbeatErrors", ifs->name, "statistics/tx_heartbeat_errors");
    abuf_json_sys_class_net(abuf, "txPackets", ifs->name, "statistics/tx_packets");
    abuf_json_sys_class_net(abuf, "txWindowErrors", ifs->name, "statistics/tx_window_errors");
    abuf_json_sys_class_net(abuf, "beaconing", ifs->name, "wireless/beacon");
    abuf_json_sys_class_net(abuf, "encryptionKey", ifs->name, "wireless/crypt");
    abuf_json_sys_class_net(abuf, "fragmentationThreshold", ifs->name, "wireless/fragment");
    abuf_json_sys_class_net(abuf, "signalLevel", ifs->name, "wireless/level");
    abuf_json_sys_class_net(abuf, "linkQuality", ifs->name, "wireless/link");
    abuf_json_sys_class_net(abuf, "misc", ifs->name, "wireless/misc");
    abuf_json_sys_class_net(abuf, "noiseLevel", ifs->name, "wireless/noise");
    abuf_json_sys_class_net(abuf, "nwid", ifs->name, "wireless/nwid");
    abuf_json_sys_class_net(abuf, "wirelessRetries", ifs->name, "wireless/retries");
    abuf_json_sys_class_net(abuf, "wirelessStatus", ifs->name, "wireless/status");
#endif /* __linux__ */
    abuf_json_mark_array_entry(false, abuf);
  }
  abuf_json_mark_object(false, true, abuf, NULL);
}

void ipc_print_twohop(struct autobuf *abuf) {
  ipc_print_neighbors_internal(abuf, true);
}

void ipc_print_config(struct autobuf *abuf) {
  struct ip_prefix_list *hna;
  struct ipaddr_str buf, mainaddrbuf;
  struct ip_prefix_list *ipcn;
  struct olsr_lq_mult *mult;
  char ipv6_buf[INET6_ADDRSTRLEN]; /* buffer for IPv6 inet_htop */

  abuf_json_mark_object(true, false, abuf, "config");

  abuf_json_int(abuf, "olsrPort", olsr_cnf->olsrport);
  abuf_json_int(abuf, "debugLevel", olsr_cnf->debug_level);
  abuf_json_boolean(abuf, "noFork", olsr_cnf->no_fork);
  abuf_json_boolean(abuf, "hostEmulation", olsr_cnf->host_emul);
  abuf_json_int(abuf, "ipVersion", olsr_cnf->ip_version);
  abuf_json_boolean(abuf, "allowNoInterfaces", olsr_cnf->allow_no_interfaces);
  abuf_json_int(abuf, "typeOfService", olsr_cnf->tos);
  abuf_json_int(abuf, "rtProto", olsr_cnf->rt_proto);
  abuf_json_int(abuf, "rtTable", olsr_cnf->rt_table);
  abuf_json_int(abuf, "rtTableDefault", olsr_cnf->rt_table_default);
  abuf_json_int(abuf, "rtTableTunnel", olsr_cnf->rt_table_tunnel);
  abuf_json_int(abuf, "rtTablePriority", olsr_cnf->rt_table_pri);
  abuf_json_int(abuf, "rtTableTunnelPriority", olsr_cnf->rt_table_tunnel_pri);
  abuf_json_int(abuf, "rtTableDefauiltOlsrPriority", olsr_cnf->rt_table_defaultolsr_pri);
  abuf_json_int(abuf, "rtTableDefaultPriority", olsr_cnf->rt_table_default_pri);
  abuf_json_int(abuf, "willingness", olsr_cnf->willingness);
  abuf_json_boolean(abuf, "willingnessAuto", olsr_cnf->willingness_auto);

  abuf_json_int(abuf, "brokenLinkCost", LINK_COST_BROKEN);
  abuf_json_int(abuf, "brokenRouteCost", ROUTE_COST_BROKEN);

  abuf_json_string(abuf, "fibMetrics", FIB_METRIC_TXT[olsr_cnf->fib_metric]);

  abuf_json_string(abuf, "defaultIpv6Multicast", inet_ntop(AF_INET6, &olsr_cnf->interface_defaults->ipv6_multicast.v6, ipv6_buf, sizeof(ipv6_buf)));
  if (olsr_cnf->interface_defaults->ipv4_multicast.v4.s_addr)
    abuf_json_string(abuf, "defaultIpv4Broadcast", inet_ntoa(olsr_cnf->interface_defaults->ipv4_multicast.v4));
  else
    abuf_json_string(abuf, "defaultIpv4Broadcast", "auto");

  if (olsr_cnf->interface_defaults->mode == IF_MODE_ETHER)
    abuf_json_string(abuf, "defaultInterfaceMode", "ether");
  else
    abuf_json_string(abuf, "defaultInterfaceMode", "mesh");

  abuf_json_float(abuf, "defaultHelloEmissionInterval", olsr_cnf->interface_defaults->hello_params.emission_interval);
  abuf_json_float(abuf, "defaultHelloValidityTime", olsr_cnf->interface_defaults->hello_params.validity_time);
  abuf_json_float(abuf, "defaultTcEmissionInterval", olsr_cnf->interface_defaults->tc_params.emission_interval);
  abuf_json_float(abuf, "defaultTcValidityTime", olsr_cnf->interface_defaults->tc_params.validity_time);
  abuf_json_float(abuf, "defaultMidEmissionInterval", olsr_cnf->interface_defaults->mid_params.emission_interval);
  abuf_json_float(abuf, "defaultMidValidityTime", olsr_cnf->interface_defaults->mid_params.validity_time);
  abuf_json_float(abuf, "defaultHnaEmissionInterval", olsr_cnf->interface_defaults->hna_params.emission_interval);
  abuf_json_float(abuf, "defaultHnaValidityTime", olsr_cnf->interface_defaults->hna_params.validity_time);
  abuf_json_boolean(abuf, "defaultAutoDetectChanges", olsr_cnf->interface_defaults->autodetect_chg);

  abuf_json_mark_object(true, true, abuf, "defaultLinkQualityMultipliers");
  for (mult = olsr_cnf->interface_defaults->lq_mult; mult != NULL ; mult = mult->next) {
    abuf_json_mark_array_entry(true, abuf);
    abuf_json_string(abuf, "route", inet_ntop(olsr_cnf->ip_version, &mult->addr, ipv6_buf, sizeof(ipv6_buf)));
    abuf_json_float(abuf, "multiplier", mult->value / 65535.0);
    abuf_json_mark_array_entry(false, abuf);
  }
  abuf_json_mark_object(false, true, abuf, NULL);

  abuf_json_mark_object(true, true, abuf, "hna");
  for (hna = olsr_cnf->hna_entries; hna != NULL ; hna = hna->next) {
    abuf_json_mark_array_entry(true, abuf);
    abuf_json_string(abuf, "destination", olsr_ip_to_string(&buf, &hna->net.prefix));
    abuf_json_int(abuf, "genmask", hna->net.prefix_len);
    abuf_json_string(abuf, "gateway", olsr_ip_to_string(&mainaddrbuf, &olsr_cnf->main_addr));
    abuf_json_mark_array_entry(false, abuf);
  }
  abuf_json_mark_object(false, true, abuf, NULL);

  abuf_json_int(abuf, "totalIpcConnectionsAllowed", olsr_cnf->ipc_connections);
  abuf_json_mark_object(true, true, abuf, "ipcAllowedAddresses");
  if (olsr_cnf->ipc_connections) {
    for (ipcn = olsr_cnf->ipc_nets; ipcn != NULL ; ipcn = ipcn->next) {
      abuf_json_mark_array_entry(true, abuf);
      abuf_json_string(abuf, "ipAddress", olsr_ip_to_string(&mainaddrbuf, &ipcn->net.prefix));
      abuf_json_int(abuf, "netmask", ipcn->net.prefix_len);
      abuf_json_mark_array_entry(false, abuf);
    }
  }
  abuf_json_mark_object(false, true, abuf, NULL);

  // keep all time in ms, so convert these two, which are in seconds
  abuf_json_int(abuf, "pollRate", olsr_cnf->pollrate * 1000);
  abuf_json_int(abuf, "nicChangePollInterval", olsr_cnf->nic_chgs_pollrate * 1000);
  abuf_json_boolean(abuf, "clearScreen", olsr_cnf->clear_screen);
  abuf_json_int(abuf, "tcRedundancy", olsr_cnf->tc_redundancy);
  abuf_json_int(abuf, "mprCoverage", olsr_cnf->mpr_coverage);

  if (!olsr_cnf->lq_level) {
    abuf_json_boolean(abuf, "useHysteresis", olsr_cnf->use_hysteresis);
    if (olsr_cnf->use_hysteresis) {
      abuf_json_float(abuf, "hysteresisScaling", olsr_cnf->hysteresis_param.scaling);
      abuf_json_float(abuf, "hysteresisLowThreshold", olsr_cnf->hysteresis_param.thr_low);
      abuf_json_float(abuf, "hysteresisHighThreshold", olsr_cnf->hysteresis_param.thr_high);
    }
  }
  abuf_json_int(abuf, "linkQualityLevel", olsr_cnf->lq_level);
  abuf_json_float(abuf, "linkQualityAging", olsr_cnf->lq_aging);
  abuf_json_boolean(abuf, "linkQualityFisheye", olsr_cnf->lq_fish);
  abuf_json_string(abuf, "linkQualityAlgorithm", olsr_cnf->lq_algorithm);
  // keep all time in ms, so convert this from seconds
  abuf_json_int(abuf, "minTcValidTime", olsr_cnf->min_tc_vtime * 1000);
  abuf_json_boolean(abuf, "setIpForward", olsr_cnf->set_ip_forward);
  abuf_json_string(abuf, "lockFile", olsr_cnf->lock_file);
  abuf_json_boolean(abuf, "useNiit", olsr_cnf->use_niit);

#ifdef __linux__
  abuf_json_boolean(abuf, "smartGateway", olsr_cnf->smart_gw_active);
  if (olsr_cnf->smart_gw_active) {
    abuf_json_boolean(abuf, "smartGatewayAlwaysRemoveServerTunnel", olsr_cnf->smart_gw_always_remove_server_tunnel);
    abuf_json_int(abuf, "smartGatewayUseCount", olsr_cnf->smart_gw_use_count);
    abuf_json_string(abuf, "smartGatewayInstanceId", olsr_cnf->smart_gw_instance_id);
    abuf_json_string(abuf, "smartGatewayPolicyRoutingScript", olsr_cnf->smart_gw_policyrouting_script);
    {
      struct autobuf egressbuf;
      struct sgw_egress_if * egressif = olsr_cnf->smart_gw_egress_interfaces;

      abuf_init(&egressbuf, (olsr_cnf->smart_gw_egress_interfaces_count * IFNAMSIZ) /* interface names */
      + (olsr_cnf->smart_gw_egress_interfaces_count - 1) /* commas */);
      while (egressif) {
        if (egressbuf.len) {
          abuf_puts(&egressbuf, ",");
        }
        abuf_appendf(&egressbuf, "%s", egressif->name);
        egressif = egressif->next;
      }
      abuf_json_string(abuf, "smartGatewayEgressInterfaces", egressbuf.buf);
      abuf_free(&egressbuf);
    }
    abuf_json_int(abuf, "smartGatewayTablesOffset", olsr_cnf->smart_gw_offset_tables);
    abuf_json_int(abuf, "smartGatewayRulesOffset", olsr_cnf->smart_gw_offset_rules);
    abuf_json_boolean(abuf, "smartGatewayAllowNat", olsr_cnf->smart_gw_allow_nat);
    abuf_json_boolean(abuf, "smartGatewayUplinkNat", olsr_cnf->smart_gw_uplink_nat);
    abuf_json_int(abuf, "smartGatewayPeriod", olsr_cnf->smart_gw_period);
    abuf_json_int(abuf, "smartGatewayStableCount", olsr_cnf->smart_gw_stablecount);
    abuf_json_int(abuf, "smartGatewayThreshold", olsr_cnf->smart_gw_thresh);
    abuf_json_int(abuf, "smartGatewayUplink", olsr_cnf->smart_gw_uplink);
    abuf_json_int(abuf, "smartGatewayDownlink", olsr_cnf->smart_gw_downlink);
    abuf_json_int(abuf, "smartGatewayType", olsr_cnf->smart_gw_type);
    abuf_json_string(abuf, "smartGatewayPrefix", olsr_ip_to_string(&mainaddrbuf, &olsr_cnf->smart_gw_prefix.prefix));
    abuf_json_int(abuf, "smartGatewayPrefixLength", olsr_cnf->smart_gw_prefix.prefix_len);
  }
#endif /* __linux__ */

  abuf_json_string(abuf, "mainIpAddress", olsr_ip_to_string(&mainaddrbuf, &olsr_cnf->main_addr));
  abuf_json_string(abuf, "unicastSourceIpAddress", olsr_ip_to_string(&mainaddrbuf, &olsr_cnf->unicast_src_ip));

  abuf_json_boolean(abuf, "useSourceIpRoutes", olsr_cnf->use_src_ip_routes);

  abuf_json_int(abuf, "maxPrefixLength", olsr_cnf->maxplen);
  abuf_json_int(abuf, "ipSize", olsr_cnf->ipsize);
  abuf_json_boolean(abuf, "deleteInternetGatewaysAtStartup", olsr_cnf->del_gws);
  // keep all time in ms, so convert this from seconds
  abuf_json_int(abuf, "willingnessUpdateInterval", olsr_cnf->will_int * 1000);
  abuf_json_float(abuf, "maxSendMessageJitter", olsr_cnf->max_jitter);
  abuf_json_int(abuf, "exitValue", olsr_cnf->exit_value);
  // keep all time in ms, so convert this from seconds
  abuf_json_int(abuf, "maxTcValidTime", olsr_cnf->max_tc_vtime * 1000);

  abuf_json_int(abuf, "niit4to6InterfaceIndex", olsr_cnf->niit4to6_if_index);
  abuf_json_int(abuf, "niit6to4InterfaceIndex", olsr_cnf->niit6to4_if_index);

  abuf_json_boolean(abuf, "hasIpv4Gateway", olsr_cnf->has_ipv4_gateway);
  abuf_json_boolean(abuf, "hasIpv6Gateway", olsr_cnf->has_ipv6_gateway);

  abuf_json_int(abuf, "ioctlSocket", olsr_cnf->ioctl_s);
#ifdef __linux__
  abuf_json_int(abuf, "routeNetlinkSocket", olsr_cnf->rtnl_s);
  abuf_json_int(abuf, "routeMonitorSocket", olsr_cnf->rt_monitor_socket);
#endif /* __linux__ */

#if defined __FreeBSD__ || defined __FreeBSD_kernel__ || defined __APPLE__ || defined __NetBSD__ || defined __OpenBSD__
  abuf_json_int(abuf, "routeChangeSocket", olsr_cnf->rts);
#endif /* defined __FreeBSD__ || defined __FreeBSD_kernel__ || defined __APPLE__ || defined __NetBSD__ || defined __OpenBSD__ */
  abuf_json_float(abuf, "linkQualityNatThreshold", olsr_cnf->lq_nat_thresh);

  abuf_json_string(abuf, "olsrdVersion", olsrd_version);
  abuf_json_string(abuf, "olsrdBuildDate", build_date);
  abuf_json_string(abuf, "olsrdBuildHost", build_host);

#if defined _WIN32 || defined _WIN64
  abuf_json_string(abuf, "os", "Windows");
#elif defined __gnu_linux__
  abuf_json_string(abuf, "os", "GNU/Linux");
#elif defined __ANDROID__
  abuf_json_string(abuf, "os", "Android");
#elif defined __APPLE__
  abuf_json_string(abuf, "os", "Mac OS X");
#elif defined __NetBSD__
  abuf_json_string(abuf, "os", "NetBSD");
#elif defined __OpenBSD__
  abuf_json_string(abuf, "os", "OpenBSD");
#elif defined __FreeBSD__ || defined __FreeBSD_kernel__
  abuf_json_string(abuf, "os", "FreeBSD");
#else /* OS detection */
  abuf_json_string(abuf, "os", "Undefined");
#endif /* OS detection */

  abuf_json_int(abuf, "startTime", start_time.tv_sec);

  abuf_json_mark_object(false, false, abuf, NULL);
}

void ipc_print_plugins(struct autobuf *abuf) {
  struct plugin_entry *pentry;
  struct plugin_param *pparam;
  abuf_json_mark_object(true, true, abuf, "plugins");
  if (olsr_cnf->plugins)
    for (pentry = olsr_cnf->plugins; pentry; pentry = pentry->next) {
      abuf_json_mark_array_entry(true, abuf);
      abuf_json_string(abuf, "plugin", pentry->name);
      for (pparam = pentry->params; pparam; pparam = pparam->next) {
        int i, keylen = strlen(pparam->key);
        char key[keylen + 1];
        long value;
        char valueTest[256];
        strcpy(key, pparam->key);
        for (i = 0; i < keylen; i++)
          key[i] = tolower(key[i]);

        // test if a int/long and set as such in JSON
        value = atol(pparam->value);
        snprintf(valueTest, 255, "%li", value);
        if (!strcmp(valueTest, pparam->value))
          abuf_json_int(abuf, key, value);
        else
          abuf_json_string(abuf, key, pparam->value);
      }
      abuf_json_mark_array_entry(false, abuf);
    }
  abuf_json_mark_object(false, true, abuf, NULL);
}
