/*
 * OLSR ARPROAMING PLUGIN
 * http://www.olsr.org
 *
 * Copyright (c) 2004, Andreas Tonnesen(andreto@olsr.org)
 * Copyright (c) 2010, amadeus (amadeus@chemnitz.freifunk.net)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in
 *	 the documentation and/or other materials provided with the
 *	 distribution.
 * * Neither the name of olsrd, olsr.org nor the names of its
 *	 contributors may be used to endorse or promote products derived
 *	 from this software without specific prior written permission.
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
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <malloc.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <linux/rtnetlink.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "olsr.h"
#include "defs.h"
#include "olsr_types.h"
#include "olsr_logging.h"
#include "scheduler.h"
#include "plugin_util.h"
#include "olsr_ip_prefix_list.h"
#include "net_olsr.h"

#define PLUGIN_DESCR	"Arproaming olsrd plugin v0.1"
#define PLUGIN_AUTHOR "amadeus"

static int arproaming_parameter_set(const char *value, void *data, set_plugin_parameter_addon addon);
static bool arproaming_init(void);
static void arproaming_schedule_event(void *);

static struct olsr_timer_info *timer_info;
static char arproaming_parameter_interface[25];
static char arproaming_parameter_timeout[25];

int arproaming_socketfd_netlink = -1;
int arproaming_socketfd_arp = -1;
char arproaming_srcip[25];
unsigned char arproaming_srcmac[25];
struct arproaming_nodes {
	unsigned int timeout;
	char ip[25];
	char mac[25];
	struct arproaming_nodes *next;
} *arproaming_nodes;

static const struct olsrd_plugin_parameters plugin_parameters[] = {
	{ .name = "Interface", .set_plugin_parameter = &arproaming_parameter_set, .data = &arproaming_parameter_interface },
	{ .name = "Timeout", .set_plugin_parameter = &arproaming_parameter_set, .data = &arproaming_parameter_timeout }
};

OLSR_PLUGIN6(plugin_parameters) {
	.descr = PLUGIN_DESCR,
	.author = PLUGIN_AUTHOR,
	.init = arproaming_init,
	.deactivate = false
};

void arproaming_list_add(struct arproaming_nodes **l, unsigned int timeout, const char *ip, const char *mac);
void arproaming_list_remove(struct arproaming_nodes **l, const char *mac);
void arproaming_list_update(struct arproaming_nodes **l, const char *ip, unsigned int timeout);
void arproaming_client_add(struct arproaming_nodes **l);
void arproaming_client_remove(const char *ip);
int arproaming_client_probe(const char *ip);
void arproaming_client_update(struct arproaming_nodes **l);
void arproaming_systemconf(int arproaming_socketfd_system);
int arproaming_plugin_init(void);
static bool arproaming_init(void);
int arproaming_plugin_exit(void);

static bool arproaming_init(void) {
	if (arproaming_plugin_init() < 0) {
		OLSR_ERROR(LOG_PLUGINS, "[ARPROAMING] Could not initialize arproaming plugin!\n");
		return true;
	}

	timer_info = olsr_alloc_timerinfo("arproaming", &arproaming_schedule_event, true);
	olsr_start_timer(MSEC_PER_SEC/3, 0, NULL, timer_info);
	return false;
}

static int arproaming_parameter_set(const char *value, void *data, set_plugin_parameter_addon addon __attribute__ ((unused)))
{
	char *ptr = data;
	snprintf(ptr, 25, "%s", value);

	return 0;
}

void arproaming_list_add(struct arproaming_nodes **l, unsigned int timeout, const char *ip, const char *mac)
{
	struct arproaming_nodes *list, *new;

	list = *l;
	new = malloc(sizeof(*new));

	new->timeout = timeout;
	strncpy(new->ip, ip, 25);
	strncpy(new->mac, mac, 25);
	new->next = NULL;

	if (list != NULL) {
		while (list->next != NULL)
			list = list->next;
		list->next = new;
	}
	else {
		*l = new;
	}
}

void arproaming_list_remove(struct arproaming_nodes **l, const char *mac)
{
	struct arproaming_nodes *list, *tmp;

	tmp = *l;

	for (list = *l; list != NULL; list = list->next) {
		if (strncmp(list->mac, mac, 25) == 0) {
			if (list->next == NULL) {
				tmp->next = NULL;
			}
			else {
				tmp->next = list->next;
			}
			free(list);
			list = *l;
		}
		tmp = list;
	}
}

void arproaming_list_update(struct arproaming_nodes **l, const char *ip, unsigned int timeout)
{
	struct arproaming_nodes *list;

	for (list = *l; list != NULL; list = list->next) {
		if (strncmp(list->ip, ip, 25) == 0) {
			list->timeout = timeout;
			break;
		}
	}
}

void arproaming_client_add(struct arproaming_nodes **l)
{
	int status, rtattrlen, len;
	char buf[10240], lladdr[6], ip[25], mac[25];

	struct {
		struct nlmsghdr n;
		struct ndmsg r;
	} req;
	struct rtattr *rta, *rtatp;
	struct nlmsghdr *nlmsghdr;
	struct ndmsg *ndmsg;
	struct ifaddrmsg *ifa;
	struct in_addr *in;
	union olsr_ip_addr host_net;

	memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ndmsg));
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ROOT;
	req.n.nlmsg_type = RTM_NEWNEIGH | RTM_GETNEIGH;
	req.r.ndm_family = AF_INET;

	rta = (struct rtattr *)(((char *)&req) + NLMSG_ALIGN(req.n.nlmsg_len));
	rta->rta_len = RTA_LENGTH(4);

	send(arproaming_socketfd_netlink, &req, req.n.nlmsg_len, 0);
	status = recv(arproaming_socketfd_netlink, buf, sizeof(buf), 0);

	for(nlmsghdr = (struct nlmsghdr *)buf; (unsigned int) status > sizeof(*nlmsghdr);) {
		len = nlmsghdr->nlmsg_len;
		ndmsg = (struct ndmsg *)NLMSG_DATA(nlmsghdr);
		rtatp = (struct rtattr *)IFA_RTA(ndmsg);
		rtattrlen = IFA_PAYLOAD(nlmsghdr);
		rtatp = RTA_NEXT(rtatp, rtattrlen);
		ifa = (struct ifaddrmsg *)NLMSG_DATA(nlmsghdr);


		if (rtatp->rta_type == IFA_ADDRESS && ndmsg->ndm_state & NUD_REACHABLE) {
			in = (struct in_addr *)RTA_DATA(rtatp);
			inet_ntop(AF_INET, in, ip, 25);
			host_net.v4.s_addr = inet_addr(ip);

			if (ip_prefix_list_find(&olsr_cnf->hna_entries, &host_net, 32, 4) == NULL && if_nametoindex(arproaming_parameter_interface) == ifa->ifa_index) {
				memcpy(lladdr, RTA_DATA(rtatp), 6);
				sprintf(mac, "%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x", lladdr[0], lladdr[1], lladdr[2], lladdr[3], lladdr[4], lladdr[5]);

				ip_prefix_list_add(&olsr_cnf->hna_entries, &host_net, 32);
				arproaming_list_add(l, time(NULL) + atoi(arproaming_parameter_timeout), ip, mac);

				OLSR_DEBUG(LOG_PLUGINS, "[ARPROAMING] Adding host %s\n", ip);
			}
		}

		status -= NLMSG_ALIGN(len);
		nlmsghdr = (struct nlmsghdr*)((char*)nlmsghdr + NLMSG_ALIGN(len));
	}
}

void arproaming_client_remove(const char *ip)
{
	int socketfd;
	struct arpreq req;
	struct sockaddr_in *sin;

	socketfd = socket(AF_INET, SOCK_DGRAM, 0);

	memset(&req, 0, sizeof(struct arpreq));
	sin = (struct sockaddr_in *) &req.arp_pa;
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = inet_addr(ip);
	strcpy(req.arp_dev, arproaming_parameter_interface);

	ioctl(socketfd, SIOCDARP, (caddr_t)&req);
	close(socketfd);
}

int arproaming_client_probe(const char *ip)
{
	int ret = 0;
	int	timeout = 1;
	struct arpMsg {
		struct ethhdr ethhdr;
		u_short htype;
		u_short ptype;
		u_char	hlen;
		u_char	plen;
		u_short operation;
		u_char	sHaddr[6];
		u_char	sInaddr[4];
		u_char	tHaddr[6];
		u_char	tInaddr[4];
	};
	struct sockaddr addr;
	struct arpMsg arp;
	fd_set fdset;
	struct timeval	tm;
	time_t prevTime;

	memset(&arp, 0, sizeof(arp));
	memcpy(arp.ethhdr.h_dest, "\xFF\xFF\xFF\xFF\xFF\xFF", 6);
	memcpy(arp.ethhdr.h_source, arproaming_srcmac, 6);
	arp.ethhdr.h_proto = htons(ETH_P_ARP);
	arp.htype = htons(ARPHRD_ETHER);
	arp.ptype = htons(ETH_P_IP);
	arp.hlen = 6;
	arp.plen = 4;
	arp.operation = htons(ARPOP_REQUEST);
	*((u_int *) arp.sInaddr) = inet_addr(arproaming_srcip);
	memcpy(arp.sHaddr, arproaming_srcmac, 6);
	*((u_int *) arp.tInaddr) = inet_addr(ip);

	memset(&addr, 0, sizeof(addr));
	strcpy(addr.sa_data, arproaming_parameter_interface);
	sendto(arproaming_socketfd_arp, &arp, sizeof(arp), 0, &addr, sizeof(addr));

	tm.tv_usec = 0;
	time(&prevTime);
	while (timeout > 0) {
		FD_ZERO(&fdset);
		FD_SET(arproaming_socketfd_arp, &fdset);
		tm.tv_sec = timeout;
		select(arproaming_socketfd_arp + 1, &fdset, (fd_set *) NULL, (fd_set *) NULL, &tm);

		if (FD_ISSET(arproaming_socketfd_arp, &fdset)) {
			recv(arproaming_socketfd_arp, &arp, sizeof(arp), 0);
			if (arp.operation == htons(ARPOP_REPLY) && bcmp(arp.tHaddr, arproaming_srcmac, 6) == 0 && *((u_int *) arp.sInaddr) == inet_addr(ip)) {
				ret = 1;
				break;
			}
		}

		timeout -= time(NULL) - prevTime;
		time(&prevTime);
	}

	return ret;
}

void arproaming_client_update(struct arproaming_nodes **l)
{
	struct arproaming_nodes *list;
	union olsr_ip_addr host_net;

	for (list = *l; list != NULL; list = list->next) {
		if (list->timeout > 0 && list->timeout <= time(NULL)) {
			if (arproaming_client_probe(list->ip) == 0) {
				OLSR_DEBUG(LOG_PLUGINS, "[ARPROAMING] Removing host %s\n", list->ip);
				host_net.v4.s_addr = inet_addr(list->ip);
				ip_prefix_list_remove(&olsr_cnf->hna_entries, &host_net, 32, 4);
				arproaming_client_remove(list->ip);
				arproaming_list_remove(l, list->mac);
				break;
			}
			else {
				OLSR_DEBUG(LOG_PLUGINS, "[ARPROAMING] Renewing host %s\n", list->ip);
				list->timeout = time(NULL) + atoi(arproaming_parameter_timeout);
			}
		}
	}
}

void arproaming_systemconf(int arproaming_socketfd_system)
{
	int i, optval = 1;
	char buf[1024];
	struct ifreq ifa, *IFR;
	struct sockaddr_in *in;
	struct ifconf ifc;

	strcpy(ifa.ifr_name, arproaming_parameter_interface);
	ioctl(arproaming_socketfd_system, SIOCGIFADDR, &ifa);
	in = (struct sockaddr_in*)&ifa.ifr_addr;
	strcpy(arproaming_srcip, inet_ntoa(in->sin_addr));

	ifc.ifc_len = sizeof(buf);
	ifc.ifc_buf = buf;
	ioctl(arproaming_socketfd_system, SIOCGIFCONF, &ifc);

	IFR = ifc.ifc_req;
	for (i = ifc.ifc_len / sizeof(struct ifreq); i-- >= 0; IFR++) {
		strcpy(ifa.ifr_name, IFR->ifr_name);
		if (ioctl(arproaming_socketfd_system, SIOCGIFFLAGS, &ifa) == 0) {
			if (! (ifa.ifr_flags & IFF_LOOPBACK)) {
				if (ioctl(arproaming_socketfd_system, SIOCGIFHWADDR, &ifa) == 0) {
					break;
				}
			}
		}
	}
	bcopy(ifa.ifr_hwaddr.sa_data, arproaming_srcmac, 6);
	close(arproaming_socketfd_system);

	setsockopt(arproaming_socketfd_arp, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval));
}

static void arproaming_schedule_event(void *foo __attribute__ ((unused)))
{
	arproaming_client_add(&arproaming_nodes);
	arproaming_client_update(&arproaming_nodes);
}

int arproaming_plugin_init(void)
{
	int arproaming_socketfd_system = -1;
	arproaming_nodes = NULL;

	arproaming_list_add(&arproaming_nodes, 0, "0", "0");

	arproaming_socketfd_netlink = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
	arproaming_socketfd_arp = socket(PF_PACKET, SOCK_PACKET, htons(ETH_P_ARP));
	arproaming_socketfd_system = socket(AF_INET, SOCK_DGRAM, 0);

	if (arproaming_socketfd_netlink < 0 || arproaming_socketfd_arp < 0 || arproaming_socketfd_system < 0) {
		return -1;
	}
	else {
		arproaming_systemconf(arproaming_socketfd_system);
		return 0;
	}
}

int arproaming_plugin_exit(void)
{
	if (arproaming_socketfd_netlink >= 0) {
		OLSR_DEBUG(LOG_PLUGINS, "[ARPROAMING] Closing netlink socket.\n");
		close(arproaming_socketfd_netlink);
	}

	if (arproaming_socketfd_arp >= 0) {
		OLSR_DEBUG(LOG_PLUGINS, "[ARPROAMING] Closing arp socket.\n");
		close(arproaming_socketfd_arp);
	}

	OLSR_DEBUG(LOG_PLUGINS, "[ARPROAMING] Exiting.\n");

	return 0;
}