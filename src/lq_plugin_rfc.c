
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

/* System includes */
#include <stdio.h> /* sprintf() */

/* OLSRD includes */
#include "olsr_protocol.h" /* HELLO_MESSAGE, TC_MESSAGE */
#include "lq_plugin.h" /* struct lq_handler */

static olsr_linkcost calc_hello_tc_cost_rfc(const void *);
static olsr_linkcost calc_link_cost_rfc(const struct link_entry *, const struct network_interface *);

static const char *print_cost_header_rfc(struct lqtextbuffer *);
static const char *print_link_lq_rfc(const struct link_entry *, char, struct lqtextbuffer *);
static const char *print_cost_rfc(olsr_linkcost, struct lqtextbuffer *);

/* RFC-3626 compliant link quality plugin settings */
struct lq_handler lq_rfc_compliant_handler = {
  &calc_hello_tc_cost_rfc, /* calc_hello_cost() */
  &calc_hello_tc_cost_rfc, /* calc_tc_cost() */
  &calc_link_cost_rfc, /* calc_link_cost() */

  &print_cost_header_rfc, /* print_cost_header() */
  &print_link_lq_rfc, /* print_link_lq() */
  &print_cost_rfc, /* print_cost() */

  NULL, /* initialize() */

  NULL, /* packet_loss_worker() */
  NULL, /* memorize_foreign_hello_lq() */

  NULL, /* serialize_hello_lq() */
  NULL, /* serialize_tc_lq() */
  NULL, /* deserialize_hello_lq() */
  NULL, /* deserialize_tc_lq() */

  NULL, /* copy_link_lq_into_hello_lq() */
  NULL, /* copy_link_lq_into_tc_lq() */
  NULL, /* copy_link_lq_into_tc_edge() */

  NULL, /* init_linkquality_in_link_entry() */

  HELLO_MESSAGE, /* lq_hello_message_type - RFC-compliant HELLO message_type */
  TC_MESSAGE, /* lq_tc_message_type - RFC-compliant TC message type */

  0, /* sizeof_lqdata_in_lqhello_packet */
  0, /* sizeof_lqdata_in_lqtc_packet */

  0, /* sizeof_linkquality_in_lq_hello_neighbor */
  0, /* sizeof_linkquality_in_lq_tc_neighbor */
  0  /* sizeof_linkquality_in_link_entry */
};

static olsr_linkcost
calc_hello_tc_cost_rfc(const void *hello_tc_lq __attribute__ ((unused)))
{
  /* A link is one hop, so return 1 */
  return 1;
}

static olsr_linkcost
calc_link_cost_rfc(
  const struct link_entry *link __attribute__ ((unused)),
  const struct network_interface* in_if __attribute__ ((unused)))
{
  /* A link is one hop, so return 1 */
  return 1;
}

static const char *
print_cost_header_rfc(struct lqtextbuffer *buffer)
{
  sprintf(buffer->buf, "Hops");
  return buffer->buf;
}

static const char *
print_link_lq_rfc(
  const struct link_entry* link __attribute__ ((unused)),
  char separator __attribute__ ((unused)),
  struct lqtextbuffer *buffer)
{
  sprintf(buffer->buf, "--");
  return buffer->buf;
}

static const char *
print_cost_rfc(olsr_linkcost cost __attribute__ ((unused)), struct lqtextbuffer *buffer)
{
  snprintf(buffer->buf, sizeof(buffer->buf), "%d", cost);

  /* Ensure null-termination also with Windows C libraries */
  buffer->buf[sizeof(buffer->buf) - 1] = '\0';

  return buffer->buf;
}

/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
