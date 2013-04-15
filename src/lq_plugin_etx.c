
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

#include "lq_packet.h" /* LQ_ETX_HELLO_MESSAGE, LQ_ETX_TC_MESSAGE */
#include "link_set.h" /* link_entry */
#include "tc_set.h" /* tc_edge_entry */
#include "lq_plugin_etx.h"

static olsr_linkcost calc_hello_tc_cost_etx(const void *);
static olsr_linkcost calc_link_cost_etx(const struct link_entry *, const struct network_interface *);

static const char *print_cost_header_etx(struct lqtextbuffer *);
static const char *print_link_lq_etx(const struct link_entry *, char, struct lqtextbuffer *);
static const char *print_cost_etx(olsr_linkcost, struct lqtextbuffer *);

static void packet_loss_worker_etx(struct link_entry *, bool, const struct network_interface*);
static void memorize_foreign_hello_lq_etx(void*, void *);

static int serialize_lq_etx(unsigned char *, const void *);
static void deserialize_lq_etx(const uint8_t **, void *);

static void copy_link_lq_into_lq_etx(void *, struct link_entry *);
static void copy_link_lq_into_tc_edge_etx(struct tc_edge_entry *, struct link_entry *);

static void init_linkquality_in_link_entry_etx(void *, const struct network_interface *);


/* ETX link quality plugin (float version) settings */
struct lq_handler lq_etx_handler = {
  &calc_hello_tc_cost_etx, /* calc_hello_cost() */
  &calc_hello_tc_cost_etx, /* calc_tc_cost() */
  &calc_link_cost_etx, /* calc_link_cost() */

  &print_cost_header_etx, /* print_cost_header() */
  &print_link_lq_etx, /* print_link_lq() */
  &print_cost_etx, /* print_cost() */

  NULL, /* initialize() */

  &packet_loss_worker_etx, /* packet_loss_worker() */
  &memorize_foreign_hello_lq_etx, /* memorize_foreign_hello_lq() */

  &serialize_lq_etx, /* serialize_hello_lq() */
  &serialize_lq_etx, /* serialize_tc_lq() */
  &deserialize_lq_etx, /* deserialize_hello_lq() */
  &deserialize_lq_etx, /* deserialize_tc_lq() */

  &copy_link_lq_into_lq_etx, /* copy_link_lq_into_hello_lq() */
  &copy_link_lq_into_lq_etx, /* copy_link_lq_into_tc_lq() */
  &copy_link_lq_into_tc_edge_etx, /* copy_link_lq_into_tc_edge() */

  &init_linkquality_in_link_entry_etx, /* init_linkquality_in_link_entry() */

  LQ_ETX_HELLO_MESSAGE, /* lq_hello_message_type for ETT */
  LQ_ETX_TC_MESSAGE, /* lq_tc_message_type for ETT */

  4, /* sizeof_lqdata_in_lqhello_packet */
  4, /* sizeof_lqdata_in_lqtc_packet */

  sizeof(struct lq_etx), /* sizeof_linkquality_in_lq_hello_neighbor */
  sizeof(struct lq_etx), /* sizeof_linkquality_in_lq_tc_neighbor */
  sizeof(struct link_lq_etx) /* sizeof_linkquality_in_link_entry */
};

static olsr_linkcost
calc_cost_etx(float lq, float nlq)
{
  olsr_linkcost result;

  if (lq < MINIMAL_USEFUL_LQ || nlq < MINIMAL_USEFUL_LQ) {
    return LINK_COST_BROKEN;
  }

  /* Calculate the bi-directional link cost */
  result = (olsr_linkcost) (1.0 / (lq * nlq) * LQ_PLUGIN_LC_MULTIPLIER);

  if (result > LINK_COST_BROKEN) {
    return LINK_COST_BROKEN;
  }
  if (result == 0) {
    return 1;
  }
  return result;
}

static olsr_linkcost
calc_hello_tc_cost_etx(const void *hello_tc_lq)
{
  const struct lq_etx *lq = hello_tc_lq;
  return calc_cost_etx(lq->lq, lq->nlq);
}

static olsr_linkcost
calc_link_cost_etx(const struct link_entry *link, const struct network_interface* in_if __attribute__ ((unused)))
{
  const struct link_lq_etx *lq = (const struct link_lq_etx *)link->linkquality;
  return calc_cost_etx(lq->lq, lq->nlq);
}

static const char *
print_cost_header_etx(struct lqtextbuffer *buffer)
{
  sprintf(buffer->buf, "ETX");
  return buffer->buf;
}

static const char *
print_link_lq_etx(const struct link_entry* link, char separator, struct lqtextbuffer *buffer)
{
  const struct link_lq_etx *link_lq = (const struct link_lq_etx *)link->linkquality;

  snprintf(buffer->buf, sizeof(buffer->buf), "LQ = %.3f%cNLQ = %.3f", link_lq->lq, separator, link_lq->nlq);

  /* Ensure null-termination also with Windows C libraries */
  buffer->buf[sizeof(buffer->buf) - 1] = '\0';

  return buffer->buf;
}

static const char *
print_cost_etx(olsr_linkcost cost, struct lqtextbuffer *buffer)
{
  snprintf(buffer->buf, sizeof(buffer->buf), "%.3f", ((float)cost) / LQ_PLUGIN_LC_MULTIPLIER);

  /* Ensure null-termination also with Windows C libraries */
  buffer->buf[sizeof(buffer->buf) - 1] = '\0';

  return buffer->buf;
}

static void
packet_loss_worker_etx(struct link_entry *link, bool lost, const struct network_interface* in_if)
{
  struct link_lq_etx *tlq = (struct link_lq_etx *)link->linkquality;
  float alpha = olsr_cnf->lq_aging;

  if (tlq->quickstart < LQ_QUICKSTART_STEPS) {
    alpha = LQ_QUICKSTART_AGING; /* fast enough to get the LQ value within 6 HELLOs up to 0.9 */
    tlq->quickstart++;
  }

  /* Exponential moving average */
  tlq->lq *= (1 - alpha);
  if (! lost) {
    tlq->lq += (alpha * link->loss_link_multiplier / 65536);
  }
  link->link_cost = calc_link_cost_etx(link, in_if);
  olsr_relevant_linkcost_change();
}

static void
memorize_foreign_hello_lq_etx(void *ptr_local_link_lq, void *ptr_foreign_hello_lq)
{
  struct link_lq_etx *local_link_lq = ptr_local_link_lq;
  struct lq_etx *foreign = ptr_foreign_hello_lq;

  if (foreign != NULL) {
    local_link_lq->nlq = foreign->lq;
  } else {
    local_link_lq->nlq = 0;
  }
}

static int
serialize_lq_etx(unsigned char *buff, const void *hello_tc_lq)
{
  const struct lq_etx *lq = hello_tc_lq;

  buff[0] = (unsigned char)(lq->lq * 255);
  buff[1] = (unsigned char)(lq->nlq * 255);
  buff[2] = 0;
  buff[3] = 0;

  return 4;
}

static void
deserialize_lq_etx(const uint8_t ** curr, void *hello_tc_lq)
{
  struct lq_etx *lq = hello_tc_lq;
  uint8_t lq_value, nlq_value;

  pkt_get_u8(curr, &lq_value);
  pkt_get_u8(curr, &nlq_value);
  pkt_ignore_u16(curr);

  lq->lq = (float)lq_value / 255.0;
  lq->nlq = (float)nlq_value / 255.0;
}

static void
copy_link_lq_into_lq_etx(void *ptr_target_lq, struct link_entry *source_link)
{
  struct lq_etx *target_lq = ptr_target_lq;
  struct link_lq_etx *source_link_lq = (struct link_lq_etx *)source_link->linkquality;

  target_lq->lq = source_link_lq->lq;
  target_lq->nlq = source_link_lq->nlq;
}

static void
copy_link_lq_into_tc_edge_etx(struct tc_edge_entry *target_edge, struct link_entry *source_link)
{
  target_edge->link_cost = source_link->link_cost;
}

static void
init_linkquality_in_link_entry_etx(void *ptr_link_lq, const struct network_interface *in_if __attribute__ ((unused)))
{
  struct link_lq_etx *link_lq = ptr_link_lq;

  link_lq->lq = 0;
  link_lq->nlq = 0;
  link_lq->quickstart = 0;
}

/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
