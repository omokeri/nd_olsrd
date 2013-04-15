/*
 * olsr_niit.h
 *
 *  Created on: 02.02.2010
 *      Author: henning
 */

#ifndef OLSR_NIIT_H_
#define OLSR_NIIT_H_

#include "olsr_types.h"  /* bool */

/* Forward declarations */
struct rt_entry;

#define DEF_NIIT4TO6_IFNAME         "niit4to6"
#define DEF_NIIT6TO4_IFNAME         "niit6to4"

#ifdef LINUX_NETLINK_ROUTING
void olsr_init_niit(void);
void olsr_setup_niit_routes(void);
void olsr_cleanup_niit_routes(void);

void olsr_niit_handle_route(const struct rt_entry *, bool set);
#endif /* LINUX_NETLINK_ROUTING */

#endif /* OLSR_NIIT_H_ */
