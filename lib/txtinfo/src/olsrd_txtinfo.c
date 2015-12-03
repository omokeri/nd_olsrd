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

#include "olsrd_txtinfo.h"

#include <unistd.h>

#include "ipcalc.h"
#include "builddata.h"
#include "neighbor_table.h"
#include "mpr_selector_set.h"
#include "mid_set.h"
#include "routing_table.h"
#include "lq_plugin.h"
#include "gateway.h"
#include "olsrd_plugin.h"
#include "../../info/info_types.h"

bool isCommand(const char *str, unsigned int siw) {
  switch (siw) {
    case SIW_OLSRD_CONF:
      return strstr(str, "/con");

    case SIW_ALL:
      return strstr(str, "/all");

    case SIW_RUNTIME_ALL:
      return strstr(str, "/runtime");

    case SIW_STARTUP_ALL:
      return strstr(str, "/startup");

    case SIW_NEIGHBORS:
      return strstr(str, "/nei");

    case SIW_LINKS:
      return strstr(str, "/lin");

    case SIW_ROUTES:
      return strstr(str, "/rou");

    case SIW_HNA:
      return strstr(str, "/hna");

    case SIW_MID:
      return strstr(str, "/mid");

    case SIW_TOPOLOGY:
      return strstr(str, "/top");

    case SIW_GATEWAYS:
      return strstr(str, "/gat");

    case SIW_INTERFACES:
      return strstr(str, "/int");

    case SIW_2HOP:
      return strstr(str, "/2ho");

    case SIW_SGW:
      return strstr(str, "/sgw");

    case SIW_VERSION:
      return strstr(str, "/ver");

    case SIW_CONFIG:
      return strstr(str, "/config");

    case SIW_PLUGINS:
      return strstr(str, "/plugins");

    case SIW_NEIGHBORS_FREIFUNK:
      return strstr(str, "/neighbours");

    default:
      return false;
  }
}

static void ipc_print_neighbors_internal(struct autobuf *abuf, bool list_2hop) {
  struct ipaddr_str buf1;
  struct neighbor_entry *neigh;
  struct neighbor_2_list_entry *list_2;
  int thop_cnt;

  abuf_puts(abuf, "Table: Neighbors\nIP address\tSYM\tMPR\tMPRS\tWill.");
  if (list_2hop)
    abuf_puts(abuf, "\n\t2hop interface adrress\n");
  else
    abuf_puts(abuf, "\t2 Hop Neighbors\n");

  /* Neighbors */
  OLSR_FOR_ALL_NBR_ENTRIES(neigh)
      {
        abuf_appendf(abuf, "%s\t%s\t%s\t%s\t%d\t", olsr_ip_to_string(&buf1, &neigh->neighbor_main_addr), (neigh->status == SYM) ? "YES" : "NO",
            neigh->is_mpr ? "YES" : "NO", olsr_lookup_mprs_set(&neigh->neighbor_main_addr) ? "YES" : "NO", neigh->willingness);
        thop_cnt = 0;

        for (list_2 = neigh->neighbor_2_list.next; list_2 != &neigh->neighbor_2_list; list_2 = list_2->next) {
          if (list_2hop)
            abuf_appendf(abuf, "\t%s\n", olsr_ip_to_string(&buf1, &list_2->neighbor_2->neighbor_2_addr));
          else
            thop_cnt++;
        }
        if (!list_2hop) {
          abuf_appendf(abuf, "%d\n", thop_cnt);
        }
      }OLSR_FOR_ALL_NBR_ENTRIES_END(neigh);
  abuf_puts(abuf, "\n");
}

void ipc_print_neighbors(struct autobuf *abuf) {
  ipc_print_neighbors_internal(abuf, false);
}

void ipc_print_links(struct autobuf *abuf) {
  struct ipaddr_str buf1, buf2;
  struct lqtextbuffer lqbuffer1, lqbuffer2;

  struct link_entry *my_link = NULL;

  if (vtime)
    abuf_puts(abuf, "Table: Links\nLocal IP\tRemote IP\tVTime\tLQ\tNLQ\tCost\n");
  else
    abuf_puts(abuf, "Table: Links\nLocal IP\tRemote IP\tHyst.\tLQ\tNLQ\tCost\n");

  /* Link set */
  OLSR_FOR_ALL_LINK_ENTRIES(my_link)
      {
        if (vtime) {
          int diff = (unsigned int) (my_link->link_timer->timer_clock - now_times);

          abuf_appendf(abuf, "%s\t%s\t%d.%03d\t%s\t%s\t\n", olsr_ip_to_string(&buf1, &my_link->local_iface_addr),
              olsr_ip_to_string(&buf2, &my_link->neighbor_iface_addr),
              diff / 1000, abs(diff % 1000),
              get_link_entry_text(my_link, '\t', &lqbuffer1),
              get_linkcost_text(my_link->linkcost, false, &lqbuffer2));
        } else {
          abuf_appendf(abuf, "%s\t%s\t0.00\t%s\t%s\t\n", olsr_ip_to_string(&buf1, &my_link->local_iface_addr),
              olsr_ip_to_string(&buf2, &my_link->neighbor_iface_addr), get_link_entry_text(my_link, '\t', &lqbuffer1),
              get_linkcost_text(my_link->linkcost, false, &lqbuffer2));
        }
      }OLSR_FOR_ALL_LINK_ENTRIES_END(my_link);

  abuf_puts(abuf, "\n");
}

void ipc_print_routes(struct autobuf *abuf) {
  struct ipaddr_str buf1, buf2;
  struct rt_entry *rt;
  struct lqtextbuffer lqbuffer;

  abuf_puts(abuf, "Table: Routes\nDestination\tGateway IP\tMetric\tETX\tInterface\n");

  /* Walk the route table */
  OLSR_FOR_ALL_RT_ENTRIES(rt)
      {
        abuf_appendf(abuf, "%s/%d\t%s\t%d\t%s\t%s\t\n", olsr_ip_to_string(&buf1, &rt->rt_dst.prefix), rt->rt_dst.prefix_len,
            olsr_ip_to_string(&buf2, &rt->rt_best->rtp_nexthop.gateway), rt->rt_best->rtp_metric.hops,
            get_linkcost_text(rt->rt_best->rtp_metric.cost, true, &lqbuffer), if_ifwithindex_name(rt->rt_best->rtp_nexthop.iif_index));
      }OLSR_FOR_ALL_RT_ENTRIES_END(rt);

  abuf_puts(abuf, "\n");

}

void ipc_print_topology(struct autobuf *abuf) {
  struct tc_entry *tc;

  if (vtime)
    abuf_puts(abuf, "Table: Topology\nDest. IP\tLast hop IP\tLQ\tNLQ\tCost\tVTime\n");
  else
    abuf_puts(abuf, "Table: Topology\nDest. IP\tLast hop IP\tLQ\tNLQ\tCost\n");

  /* Topology */
  OLSR_FOR_ALL_TC_ENTRIES(tc)
      {
        struct tc_edge_entry *tc_edge;
        OLSR_FOR_ALL_TC_EDGE_ENTRIES(tc, tc_edge)
            {
              if (tc_edge->edge_inv) {
                struct ipaddr_str dstbuf, addrbuf;
                struct lqtextbuffer lqbuffer1, lqbuffer2;
                if (vtime) {
                  uint32_t vt = tc->validity_timer != NULL ? (tc->validity_timer->timer_clock - now_times) : 0;
                  int diff = (int) (vt);
                  abuf_appendf(abuf, "%s\t%s\t%s\t%s\t%d.%03d\n", olsr_ip_to_string(&dstbuf, &tc_edge->T_dest_addr),
                      olsr_ip_to_string(&addrbuf, &tc->addr),
                      get_tc_edge_entry_text(tc_edge, '\t', &lqbuffer1),
                      get_linkcost_text(tc_edge->cost, false, &lqbuffer2),
                      diff / 1000, diff % 1000);
                } else {
                  abuf_appendf(abuf, "%s\t%s\t%s\t%s\n", olsr_ip_to_string(&dstbuf, &tc_edge->T_dest_addr), olsr_ip_to_string(&addrbuf, &tc->addr),
                      get_tc_edge_entry_text(tc_edge, '\t', &lqbuffer1), get_linkcost_text(tc_edge->cost, false, &lqbuffer2));
                }
              }
            }OLSR_FOR_ALL_TC_EDGE_ENTRIES_END(tc, tc_edge);
      }OLSR_FOR_ALL_TC_ENTRIES_END(tc);

  abuf_puts(abuf, "\n");
}

void ipc_print_hna(struct autobuf *abuf) {
  struct ip_prefix_list *hna;
  struct hna_entry *tmp_hna;
  struct hna_net *tmp_net;
  struct ipaddr_str buf, mainaddrbuf;

  if (vtime)
    abuf_puts(abuf, "Table: HNA\nDestination\tGateway\tVTime\n");
  else
    abuf_puts(abuf, "Table: HNA\nDestination\tGateway\n");

  /* Announced HNA entries */
  for (hna = olsr_cnf->hna_entries; hna != NULL ; hna = hna->next) {
    abuf_appendf(abuf, "%s/%d\t%s\n", olsr_ip_to_string(&buf, &hna->net.prefix), hna->net.prefix_len, olsr_ip_to_string(&mainaddrbuf, &olsr_cnf->main_addr));
  }

  /* HNA entries */
  OLSR_FOR_ALL_HNA_ENTRIES(tmp_hna)
        {

          /* Check all networks */
          for (tmp_net = tmp_hna->networks.next; tmp_net != &tmp_hna->networks; tmp_net = tmp_net->next) {
            if (vtime) {
              uint32_t vt = tmp_net->hna_net_timer != NULL ? (tmp_net->hna_net_timer->timer_clock - now_times) : 0;
              int diff = (int) (vt);
              abuf_appendf(abuf, "%s/%d\t%s\t\%d.%03d\n", olsr_ip_to_string(&buf, &tmp_net->hna_prefix.prefix),
                  tmp_net->hna_prefix.prefix_len, olsr_ip_to_string(&mainaddrbuf, &tmp_hna->A_gateway_addr),
                  diff / 1000, abs(diff % 1000));
            } else {
              abuf_appendf(abuf, "%s/%d\t%s\n", olsr_ip_to_string(&buf, &tmp_net->hna_prefix.prefix), tmp_net->hna_prefix.prefix_len,
                  olsr_ip_to_string(&mainaddrbuf, &tmp_hna->A_gateway_addr));
            }
          }
        }OLSR_FOR_ALL_HNA_ENTRIES_END(tmp_hna);

  abuf_puts(abuf, "\n");
}

void ipc_print_mid(struct autobuf *abuf) {
  int idx;
  unsigned short is_first;
  struct mid_entry *entry;
  struct mid_address *alias;
  if (vtime)
    abuf_puts(abuf, "Table: MID\nIP address\tAlias\tVTime\n");
  else
    abuf_puts(abuf, "Table: MID\nIP address\tAliases\n");

  /* MID */
  for (idx = 0; idx < HASHSIZE; idx++) {
    entry = mid_set[idx].next;

    while (entry != &mid_set[idx]) {
      struct ipaddr_str buf, buf2;
      if (!vtime)
        abuf_puts(abuf, olsr_ip_to_string(&buf, &entry->main_addr));

      alias = entry->aliases;
      is_first = 1;

      while (alias) {
        if (vtime) {
          uint32_t vt = alias->vtime - now_times;
          int diff = (int) (vt);

          abuf_appendf(abuf, "%s\t%s\t%d.%03d\n",
              olsr_ip_to_string(&buf, &entry->main_addr),
              olsr_ip_to_string(&buf2, &alias->alias),
              diff / 1000, abs(diff % 1000));
        } else {
          abuf_appendf(abuf, "%s%s", (is_first ? "\t" : ";"), olsr_ip_to_string(&buf, &alias->alias));
        }
        alias = alias->next_alias;
        is_first = 0;
      }
      entry = entry->next;
      if (!vtime)
        abuf_puts(abuf, "\n");
    }
  }
  abuf_puts(abuf, "\n");
}

void ipc_print_gateways(struct autobuf *abuf) {
#ifndef __linux__
  abuf_puts(abuf, "Gateway mode is only supported in linux\n");
#else /* __linux__ */
  static const char IPV4[] = "ipv4";
  static const char IPV4_NAT[] = "ipv4(n)";
  static const char IPV6[] = "ipv6";
  static const char NONE[] = "-";

  struct ipaddr_str buf;
  struct gateway_entry *gw;
  struct lqtextbuffer lqbuf;

  // Status IP ETX Hopcount Uplink-Speed Downlink-Speed ipv4/ipv4-nat/- ipv6/- ipv6-prefix/-
  abuf_puts(abuf, "Table: Gateways\nStatus\tGateway IP\tETX\tHopcnt\tUplink\tDownlnk\tIPv4\tIPv6\tPrefix\n");
  OLSR_FOR_ALL_GATEWAY_ENTRIES(gw)
      {
        char v4, v6;
        const char *v4type, *v6type;
        struct tc_entry *tc;

        if ((tc = olsr_lookup_tc_entry(&gw->originator)) == NULL) {
          continue;
        }

        if (gw == olsr_get_inet_gateway(false)) {
          v4 = 's';
        } else if (gw->ipv4 && (olsr_cnf->ip_version == AF_INET || olsr_cnf->use_niit) && (olsr_cnf->smart_gw_allow_nat || !gw->ipv4nat)) {
          v4 = 'u';
        } else {
          v4 = '-';
        }

        if (gw == olsr_get_inet_gateway(true)) {
          v6 = 's';
        } else if (gw->ipv6 && olsr_cnf->ip_version == AF_INET6) {
          v6 = 'u';
        } else {
          v6 = '-';
        }

        if (gw->ipv4) {
          v4type = gw->ipv4nat ? IPV4_NAT : IPV4;
        } else {
          v4type = NONE;
        }
        if (gw->ipv6) {
          v6type = IPV6;
        } else {
          v6type = NONE;
        }

        abuf_appendf(abuf, "%c%c\t%s\t%s\t%d\t%u\t%u\t%s\t%s\t%s\n", //
            v4, //
            v6, //
            olsr_ip_to_string(&buf, &gw->originator), //
            get_linkcost_text(tc->path_cost, true, &lqbuf), //
            tc->hops, //
            gw->uplink, //
            gw->downlink, //
            v4type, //
            v6type, //
            !gw->external_prefix.prefix_len ? NONE : olsr_ip_prefix_to_string(&gw->external_prefix));
      }OLSR_FOR_ALL_GATEWAY_ENTRIES_END (gw)
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
static void sgw_ipvx(struct autobuf *abuf, bool ipv6, const char * fmth, const char * fmtv) {
  struct interfaceName * sgwTunnelInterfaceNames = !ipv6 ? sgwTunnel4InterfaceNames : sgwTunnel6InterfaceNames;

  abuf_appendf(abuf, "# Table: Smart Gateway IPv%d\n", ipv6 ? 6 : 4);
  abuf_appendf(abuf, fmth, "#", "Originator", "Prefix", "Uplink", "Downlink", "PathCost", "IPv4", "IPv4-NAT", "IPv6", "Tunnel-Name", "Destination", "Cost");

  if (olsr_cnf->smart_gw_active && sgwTunnelInterfaceNames) {
    struct gateway_entry * current_gw = olsr_get_inet_gateway(ipv6);
    int i;
    for (i = 0; i < olsr_cnf->smart_gw_use_count; i++) {
      struct interfaceName * node = &sgwTunnelInterfaceNames[i];
      struct gateway_entry * gw = node->gw;

      if (!gw) {
        continue;
      }

      {
        struct tc_entry* tc = olsr_lookup_tc_entry(&gw->originator);

        struct ipaddr_str originatorStr;
        const char * originator = olsr_ip_to_string(&originatorStr, &gw->originator);
        struct ipaddr_str prefixStr;
        const char * prefix = olsr_ip_to_string(&prefixStr, &gw->external_prefix.prefix);
        union olsr_ip_addr netmask = { { 0 } };
        struct ipaddr_str prefixMaskStr;
        const char * prefixMASKStr;
        char prefixAndMask[INET6_ADDRSTRLEN * 2];

        if (!ipv6) {
          prefix_to_netmask((uint8_t *) &netmask, sizeof(netmask.v4), gw->external_prefix.prefix_len);
          prefixMASKStr = olsr_ip_to_string(&prefixMaskStr, &netmask);
          snprintf(prefixAndMask, sizeof(prefixAndMask), "%s/%s", prefix, prefixMASKStr);
        } else {
          snprintf(prefixAndMask, sizeof(prefixAndMask), "%s/%d", prefix, gw->external_prefix.prefix_len);
        }

        abuf_appendf(abuf, fmtv, //
            (current_gw && (current_gw == gw)) ? "*" : " ", // selected
            originator, // Originator
            prefixAndMask, // 4: IP/Mask, 6: IP/Length
            gw->uplink, // Uplink
            gw->downlink, // Downlink
            !tc ? ROUTE_COST_BROKEN : tc->path_cost, // PathCost
            gw->ipv4 ? "Y" : "N", // IPv4
            gw->ipv4nat ? "Y" : "N", // IPv4-NAT
            gw->ipv6 ? "Y" : "N", // IPv6
            node->name, // Tunnel-Name
            originator, // Destination
            gw->path_cost // Cost
            );
      }
    }
  }
}
#endif /* __linux__ */

void ipc_print_sgw(struct autobuf *abuf) {
#ifndef __linux__
  abuf_puts(abuf, "Gateway mode is only supported in Linux\n");
#else

  static const char * fmth4 = "%s%-15s %-31s %-9s %-9s %-10s %-4s %-8s %-4s %-15s %-15s %s\n";
  static const char * fmtv4 = "%s%-15s %-31s %-9u %-9u %-10u %-4s %-8s %-4s %-15s %-15s %lld\n";
#if 0
  static const char * fmth6 = "%s%-45s %-49s %-9s %-9s %-10s %-4s %-8s %-4s %-15s %-45s %s\n";
  static const char * fmtv6 = "%s%-45s %-49s %-9u %-9u %-10u %-4s %-8s %-4s %-15s %-45s %lld\n";
#endif

  sgw_ipvx(abuf, false, fmth4, fmtv4);
  abuf_puts(abuf, "\n");
#if 0
  sgw_ipvx(abuf, true, fmth6, fmtv6);
  abuf_puts(abuf, "\n");
#endif
#endif /* __linux__ */
}

void ipc_print_version(struct autobuf *abuf) {
  abuf_appendf(abuf, "Version: %s (built on %s on %s)\n", olsrd_version, build_date, build_host);
}

void ipc_print_olsrd_conf(struct autobuf *abuf) {
  olsrd_write_cnf_autobuf(abuf, olsr_cnf);
}

void ipc_print_interfaces(struct autobuf *abuf) {
  const struct olsr_if *ifs;
  abuf_puts(abuf, "Table: Interfaces\nName\tState\tMTU\tWLAN\tSrc-Adress\tMask\tDst-Adress\n");
  for (ifs = olsr_cnf->interfaces; ifs != NULL ; ifs = ifs->next) {
    const struct interface_olsr * const rifs = ifs->interf;
    abuf_appendf(abuf, "%s\t", ifs->name);
    if (!rifs) {
      abuf_puts(abuf, "DOWN\n");
      continue;
    }
    abuf_appendf(abuf, "UP\t%d\t%s\t", rifs->int_mtu, rifs->is_wireless ? "Yes" : "No");

    if (olsr_cnf->ip_version == AF_INET) {
      struct ipaddr_str addrbuf, maskbuf, bcastbuf;
      abuf_appendf(abuf, "%s\t%s\t%s\n", ip4_to_string(&addrbuf, rifs->int_addr.sin_addr), ip4_to_string(&maskbuf, rifs->int_netmask.sin_addr),
          ip4_to_string(&bcastbuf, rifs->int_broadaddr.sin_addr));
    } else {
      struct ipaddr_str addrbuf, maskbuf;
      abuf_appendf(abuf, "%s\t\t%s\n", ip6_to_string(&addrbuf, &rifs->int6_addr.sin6_addr), ip6_to_string(&maskbuf, &rifs->int6_multaddr.sin6_addr));
    }
  }
  abuf_puts(abuf, "\n");
}

void ipc_print_twohop(struct autobuf *abuf) {
  ipc_print_neighbors_internal(abuf, true);
}
