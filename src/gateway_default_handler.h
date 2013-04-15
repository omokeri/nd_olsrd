/*
 * gateway_default_handler.h
 *
 *  Created on: Jan 29, 2010
 *      Author: rogge
 */

#ifndef _GATEWAY_DEFAULT_HANDLER_H
#define _GATEWAY_DEFAULT_HANDLER_H

#ifndef WIN32
#include "gateway.h"

#define GW_DEFAULT_TIMER_INTERVAL 10*1000
#define GW_DEFAULT_STABLE_COUNT   6

void olsr_gw_default_init(void);
void olsr_gw_default_lookup_gateway(bool, bool);

#endif /* !WIN32 */
#endif /* _GATEWAY_DEFAULT_HANDLER_H */
