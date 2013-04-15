
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2008 Henning Rogge <rogge@fgan.de>
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

#ifndef _LQ_PLUGIN_ETX_FFETH_H
#define _LQ_PLUGIN_ETX_FFETH_H

#include "olsr_types.h" /* uint8_t, uint16_t */
#include "lq_plugin.h" /* struct lq_handler */
#include "lq_plugin_etx_ff.h" /* lq_etx_ff */

#define LQ_ALGORITHM_ETX_FFETH_NAME "etx_ffeth"

#define LQ_FFETH_WINDOW 32
#define LQ_FFETH_QUICKSTART_INIT 4

/* The ETX-FFETH link quality extension to link set entries */
struct link_lq_etx_ffeth {
  struct lq_etx_ff smoothed_lq;
  struct lq_etx_ff lq;
  uint8_t windowSize;
  uint8_t activeIdx;
  uint16_t last_seq_nr;
  uint16_t missed_hellos;
  bool perfect_eth;
  uint16_t received[LQ_FFETH_WINDOW];
  uint16_t total[LQ_FFETH_WINDOW];
};

extern struct lq_handler lq_etx_ffeth_handler;

#endif /* _LQ_PLUGIN_ETX_FFETH_H */

/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
