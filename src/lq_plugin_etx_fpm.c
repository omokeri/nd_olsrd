
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
#include "lq_plugin_etx_fpm.h"

static olsr_linkcost calc_hello_tc_cost_etx(const void *);
static olsr_linkcost calc_link_cost_etx_fpm(const struct link_entry *, const struct network_interface *);

static const char *print_cost_header_etx_fpm(struct lqtextbuffer *);
static const char *print_link_lq_etx_fpm(const struct link_entry *, char, struct lqtextbuffer *);
static const char *print_cost_etx_fpm(olsr_linkcost, struct lqtextbuffer *);

static void initialize_etx_fpm(void);

static void packet_loss_worker_etx_fpm(struct link_entry *, bool, const struct network_interface*);
static void memorize_foreign_hello_lq_etx_fpm(void*, void *);

static int serialize_lq_etx_fpm(unsigned char *, const void *);
static void deserialize_lq_etx_fpm(const uint8_t ** , void *);

static void copy_link_lq_into_lq_etx_fpm(void *, struct link_entry *);
static void copy_link_lq_into_tc_edge_etx_fpm(struct tc_edge_entry *, struct link_entry *);

static void init_linkquality_in_link_entry_etx_fpm(void *, const struct network_interface *);


/* ETX link quality plugin (fpm version) settings */
struct lq_handler lq_etx_fpm_handler = {
  &calc_hello_tc_cost_etx, /* calc_hello_cost() */
  &calc_hello_tc_cost_etx, /* calc_tc_cost() */
  &calc_link_cost_etx_fpm, /* calc_link_cost() */

  &print_cost_header_etx_fpm, /* print_cost_header() */
  &print_link_lq_etx_fpm, /* print_link_lq() */
  &print_cost_etx_fpm, /* print_cost() */

  &initialize_etx_fpm, /* initialize() */

  &packet_loss_worker_etx_fpm, /* packet_loss_worker() */
  &memorize_foreign_hello_lq_etx_fpm, /* memorize_foreign_hello_lq() */

  &serialize_lq_etx_fpm, /* serialize_hello_lq() */
  &serialize_lq_etx_fpm, /* serialize_tc_lq() */
  &deserialize_lq_etx_fpm, /* deserialize_hello_lq() */
  &deserialize_lq_etx_fpm, /* deserialize_tc_lq() */

  &copy_link_lq_into_lq_etx_fpm, /* copy_link_lq_into_hello_lq() */
  &copy_link_lq_into_lq_etx_fpm, /* copy_link_lq_into_tc_lq() */
  &copy_link_lq_into_tc_edge_etx_fpm, /* copy_link_lq_into_tc_edge() */

  &init_linkquality_in_link_entry_etx_fpm, /* init_linkquality_in_link_entry() */

  LQ_ETX_HELLO_MESSAGE, /* lq_hello_message_type for ETT */
  LQ_ETX_TC_MESSAGE, /* lq_tc_message_type for ETT */

  4, /* sizeof_lqdata_in_lqhello_packet */
  4, /* sizeof_lqdata_in_lqtc_packet */

  sizeof(struct lq_etx_fpm), /* sizeof_linkquality_in_lq_hello_neighbor */
  sizeof(struct lq_etx_fpm), /* sizeof_linkquality_in_lq_tc_neighbor */
  sizeof(struct link_lq_etx_fpm) /* sizeof_linkquality_in_link_entry */
};


uint32_t aging_factor_new, aging_factor_old;
uint32_t aging_quickstart_new, aging_quickstart_old;

static void
initialize_etx_fpm(void)
{
  aging_factor_new = (uint32_t) (olsr_cnf->lq_aging * LQ_FPM_INTERNAL_MULTIPLIER);
  aging_factor_old = LQ_FPM_INTERNAL_MULTIPLIER - aging_factor_new;

  aging_quickstart_new = (uint32_t) (LQ_QUICKSTART_AGING * LQ_FPM_INTERNAL_MULTIPLIER);
  aging_quickstart_old = LQ_FPM_INTERNAL_MULTIPLIER - aging_quickstart_new;
}

static olsr_linkcost
calc_cost_etx_fpm(uint8_t lq, uint8_t nlq)
{
  olsr_linkcost result;

  if (lq < (unsigned int)(255 * MINIMAL_USEFUL_LQ) || nlq < (unsigned int)(255 * MINIMAL_USEFUL_LQ)) {
    return LINK_COST_BROKEN;
  }

  /* Calculate the bi-directional link cost */
  result = LQ_FPM_LINKCOST_MULTIPLIER * 255 / (int)lq * 255 / (int)nlq;

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
  const struct lq_etx_fpm *lq = hello_tc_lq;
  return calc_cost_etx_fpm(lq->lq, lq->nlq);
}

static olsr_linkcost
calc_link_cost_etx_fpm(const struct link_entry *link, const struct network_interface* in_if __attribute__ ((unused)))
{
  const struct link_lq_etx_fpm *lq = (const struct link_lq_etx_fpm *)link->linkquality;
  return calc_cost_etx_fpm(lq->lq, lq->nlq);
}

static const char *
print_cost_header_etx_fpm(struct lqtextbuffer *buffer)
{
  sprintf(buffer->buf, "ETX-FPM");
  return buffer->buf;
}

static const char *
print_link_lq_etx_fpm(const struct link_entry* link, char separator, struct lqtextbuffer *buffer)
{
  const struct link_lq_etx_fpm *link_lq = (const struct link_lq_etx_fpm *)link->linkquality;

  snprintf(
    buffer->buf,
    sizeof(buffer->buf),
    "LQ = %.3f%cNLQ = %.3f",
    (float)(link_lq->lq) / 255.0,
    separator,
    (float)(link_lq->nlq) / 255.0);

  /* Ensure null-termination also with Windows C libraries */
  buffer->buf[sizeof(buffer->buf) - 1] = '\0';

  return buffer->buf;
}

static const char *
print_cost_etx_fpm(olsr_linkcost cost, struct lqtextbuffer *buffer)
{
  snprintf(buffer->buf, sizeof(buffer->buf), "%.3f", ((float)cost) / LQ_FPM_LINKCOST_MULTIPLIER);

  /* Ensure null-termination also with Windows C libraries */
  buffer->buf[sizeof(buffer->buf) - 1] = '\0';

  return buffer->buf;
}

static void
packet_loss_worker_etx_fpm(struct link_entry *link, bool lost, const struct network_interface* in_if)
{
  struct link_lq_etx_fpm *tlq = (struct link_lq_etx_fpm *)link->linkquality;
  uint32_t alpha_old = aging_factor_old;
  uint32_t alpha_new = aging_factor_new;

  uint32_t value;

  /* TODO: why is the aging factor a global variable? And not per-link? Please add a comment. */
  if (tlq->quickstart < LQ_QUICKSTART_STEPS) {
    alpha_new = aging_quickstart_new;
    alpha_old = aging_quickstart_old;
    tlq->quickstart++;
  }

  /* Exponential moving average */
  value = (uint32_t) (tlq->lq) * LQ_FPM_INTERNAL_MULTIPLIER / 255;

  value = (value * alpha_old + LQ_FPM_INTERNAL_MULTIPLIER - 1) / LQ_FPM_INTERNAL_MULTIPLIER;

  if (!lost) {
    uint32_t ratio;

    ratio = (alpha_new * link->loss_link_multiplier + LINK_LOSS_MULTIPLIER - 1) / LINK_LOSS_MULTIPLIER;
    value += ratio;
  }
  tlq->lq = (value * 255 + LQ_FPM_INTERNAL_MULTIPLIER - 1) / LQ_FPM_INTERNAL_MULTIPLIER;

  link->link_cost = calc_link_cost_etx_fpm(link, in_if);
  olsr_relevant_linkcost_change();
}

static void
memorize_foreign_hello_lq_etx_fpm(void *ptr_local_link_lq, void *ptr_foreign_hello_lq)
{
  struct link_lq_etx_fpm *local_link_lq = ptr_local_link_lq;
  struct lq_etx_fpm *foreign = ptr_foreign_hello_lq;

  if (foreign != NULL) {
    local_link_lq->nlq = foreign->lq;
  } else {
    local_link_lq->nlq = 0;
  }
}

static int
serialize_lq_etx_fpm(unsigned char *buff, const void *hello_tc_lq)
{
  const struct lq_etx_fpm *lq = hello_tc_lq;

  buff[0] = (unsigned char)lq->lq;
  buff[1] = (unsigned char)lq->nlq;
  buff[2] = 0;
  buff[3] = 0;

  return 4;
}

static void
deserialize_lq_etx_fpm(const uint8_t ** curr, void *hello_tc_lq)
{
  struct lq_etx_fpm *lq = hello_tc_lq;

  pkt_get_u8(curr, &lq->lq);
  pkt_get_u8(curr, &lq->nlq);
  pkt_ignore_u16(curr);
}

static void
copy_link_lq_into_lq_etx_fpm(void *ptr_target_lq, struct link_entry *source_link)
{
  struct lq_etx_fpm *target_lq = ptr_target_lq;
  struct link_lq_etx_fpm *source_link_lq = (struct link_lq_etx_fpm *)source_link->linkquality;

  target_lq->lq = source_link_lq->lq;
  target_lq->nlq = source_link_lq->nlq;
}

static void
copy_link_lq_into_tc_edge_etx_fpm(struct tc_edge_entry *target_edge, struct link_entry *source_link)
{
  target_edge->link_cost = source_link->link_cost;
}

static void
init_linkquality_in_link_entry_etx_fpm(void *ptr_link_lq, const struct network_interface *in_if __attribute__ ((unused)))
{
  struct link_lq_etx_fpm *link_lq = ptr_link_lq;

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
