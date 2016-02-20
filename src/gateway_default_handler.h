/*
 * gateway_default_handler.h
 *
 *  Created on: Jan 29, 2010
 *      Author: rogge
 */

#ifndef GATEWAY_DEFAULT_HANDLER_H_
#define GATEWAY_DEFAULT_HANDLER_H_

#ifdef __linux__

#include "defs.h"
#include "gateway.h"

#include <stdbool.h>

static INLINE bool isGwSelectable(struct gateway_entry * gw, bool ipv6) {
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

extern struct olsr_gw_handler gw_def_handler;

#endif /* __linux__ */
#endif /* GATEWAY_DEFAULT_HANDLER_H_ */
