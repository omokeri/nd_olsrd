
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

#include "lq_packet.h" /* LQ_ETT_HELLO_MESSAGE, LQ_ETT_TC_MESSAGE */
#include "link_set.h" /* link_entry */
#include "tc_set.h" /* tc_edge_entry */
#include "lq_plugin_ett.h"

static olsr_linkcost calc_hello_cost_ett(const void *);
static olsr_linkcost calc_tc_cost_ett(const void *);
static olsr_linkcost calc_link_cost_ett(const struct link_entry *, const struct network_interface *);

static const char *print_cost_header_ett(struct lqtextbuffer *);
static const char *print_link_lq_ett(const struct link_entry *, char, struct lqtextbuffer *);
static const char *print_cost_ett(olsr_linkcost, struct lqtextbuffer *);

static void packet_loss_worker_ett(struct link_entry *, bool, const struct network_interface*);
static void memorize_foreign_hello_lq_ett(void*, void *);

static int serialize_hello_lq_ett(unsigned char *, const void *);
static int serialize_tc_lq_ett(unsigned char *, const void *);
static void deserialize_hello_lq_ett(const uint8_t **, void *);
static void deserialize_tc_lq_ett(const uint8_t ** curr, void *);

static void copy_link_lq_into_hello_lq_ett(void *, struct link_entry *);
static void copy_link_lq_into_tc_lq_ett(void *, struct link_entry *);
static void copy_link_lq_into_tc_edge_ett(struct tc_edge_entry *, struct link_entry *);

static void init_linkquality_in_link_entry_ett(void *, const struct network_interface *);


#define ETT_SIZEOF_LQDATA_IN_LQHELLO_PACKET 8
#define ETT_SIZEOF_LQDATA_IN_LQTC_PACKET 4

/* ETT link quality plugin (float version) settings */
struct lq_handler lq_ett_handler = {
  &calc_hello_cost_ett, /* calc_hello_cost() */
  &calc_tc_cost_ett, /* calc_tc_cost() */
  &calc_link_cost_ett, /* calc_link_cost() */

  &print_cost_header_ett, /* print_cost_header() */
  &print_link_lq_ett, /* print_link_lq() */
  &print_cost_ett, /* print_cost() */

  NULL, /* initialize() */

  &packet_loss_worker_ett, /* packet_loss_worker() */
  &memorize_foreign_hello_lq_ett, /* memorize_foreign_hello_lq() */

  &serialize_hello_lq_ett, /* serialize_hello_lq() */
  &serialize_tc_lq_ett, /* serialize_tc_lq() */
  &deserialize_hello_lq_ett, /* deserialize_hello_lq() */
  &deserialize_tc_lq_ett, /* deserialize_tc_lq() */

  &copy_link_lq_into_hello_lq_ett, /* copy_link_lq_into_hello_lq() */
  &copy_link_lq_into_tc_lq_ett, /* copy_link_lq_into_tc_lq() */
  &copy_link_lq_into_tc_edge_ett, /* copy_link_lq_into_tc_edge() */

  &init_linkquality_in_link_entry_ett, /* init_linkquality_in_link_entry() */

  LQ_ETT_HELLO_MESSAGE, /* lq_hello_message_type for ETT */
  LQ_ETT_TC_MESSAGE, /* lq_tc_message_type for ETT */

  ETT_SIZEOF_LQDATA_IN_LQHELLO_PACKET, /* sizeof_lqdata_in_lqhello_packet */
  ETT_SIZEOF_LQDATA_IN_LQTC_PACKET, /* sizeof_lqdata_in_lqtc_packet */

  sizeof(struct hello_lq_ett), /* sizeof_linkquality_in_lq_hello_neighbor */
  sizeof(struct tc_lq_ett), /* sizeof_linkquality_in_lq_tc_neighbor */
  sizeof(struct link_lq_ett) /* sizeof_linkquality_in_link_entry */
};

static olsr_linkcost
calc_hello_cost_ett(const void *hello_lq)
{
  const struct hello_lq_ett *lq = hello_lq;
  return (olsr_linkcost)lq->cost;
}

static olsr_linkcost
calc_tc_cost_ett(const void *tc_lq)
{
  const struct tc_lq_ett *lq = tc_lq;
  return (olsr_linkcost)lq->cost;
}

static olsr_linkcost
calc_link_cost_ett(const struct link_entry *link, const struct network_interface* in_if)
{
  const struct link_lq_ett *lq_data = (const struct link_lq_ett*) link->linkquality;
  olsr_linkcost result;

  if (lq_data->lq < MINIMAL_USEFUL_LQ || lq_data->nlq < MINIMAL_USEFUL_LQ) {
    return LINK_COST_BROKEN;
  }

  if (in_if == NULL) {
    return LINK_COST_BROKEN;
  }

  if (lq_data->link_speed < MIN_MEDIUM_SPEED) {
    return LINK_COST_BROKEN;
  }

  {
    float lq_weight =
      in_if->int_lc_medium_time_weight / lq_data->link_speed +
      in_if->int_lc_medium_type_weight * in_if->int_lc_medium_type;

    float lq = 1.0 / (lq_data->lq * lq_data->nlq);

    result = 
      lq_weight * lq +
      in_if->int_lc_offset;
  }
    
  if (result > LINK_COST_BROKEN) {
    return LINK_COST_BROKEN;
  }
  if (result == 0) {
    return 1;
  }
  return result;
}

static const char *
print_cost_header_ett(struct lqtextbuffer *buffer)
{
  sprintf(buffer->buf, "ETT");
  return buffer->buf;
}

static const char *
print_link_lq_ett(const struct link_entry* link, char separator, struct lqtextbuffer *buffer)
{
  const struct link_lq_ett *link_lq = (const struct link_lq_ett *)link->linkquality;

  snprintf(
    buffer->buf,
    sizeof(buffer->buf),
    "LQ = %.1f%%%cNLQ = %.1f%%%cspeed = %.3f Mbits/sec",
    link_lq->lq *100, /* Output as percentage */
    separator,
    link_lq->nlq * 100, /* Output as percentage */
    separator,
    link_lq->link_speed);

  /* Ensure null-termination also with Windows C libraries */
  buffer->buf[sizeof(buffer->buf) - 1] = '\0';

  return buffer->buf;
}

static const char *
print_cost_ett(olsr_linkcost cost, struct lqtextbuffer *buffer)
{
  snprintf(buffer->buf, sizeof(buffer->buf), "%.3f", (float)cost);

  /* Ensure null-termination also with Windows C libraries */
  buffer->buf[sizeof(buffer->buf) - 1] = '\0';

  return buffer->buf;
}

static void
packet_loss_worker_ett(struct link_entry *link, bool lost, const struct network_interface* in_if)
{
  struct link_lq_ett *tlq = (struct link_lq_ett *)link->linkquality;
  float alpha = olsr_cnf->lq_aging;

  /* Use an exponential moving average */
  tlq->lq *= (1 - alpha);
  if (! lost) {
    tlq->lq += (alpha * link->loss_link_multiplier / 65536);
  }
  link->link_cost = calc_link_cost_ett(link, in_if);
  olsr_relevant_linkcost_change();
}

static void
memorize_foreign_hello_lq_ett(void *ptr_local_link_lq, void *ptr_foreign_hello_lq)
{
  struct link_lq_ett *local_link_lq = ptr_local_link_lq;
  struct hello_lq_ett *foreign = ptr_foreign_hello_lq;

  if (foreign != NULL) {
    local_link_lq->nlq = foreign->lq;
  }
  else {
    local_link_lq->nlq = 0.0;
  }
}

static int
serialize_hello_lq_ett(unsigned char *buff, const void *hello_lq)
{
  const struct hello_lq_ett *lq = hello_lq;

  pkt_put_u8 (&buff, (unsigned char)(lq->lq * 255));

  /* Three padding bytes: fill with defined value of 0 */
  pkt_put_u8 (&buff, 0);
  pkt_put_u8 (&buff, 0);
  pkt_put_u8 (&buff, 0);

  pkt_put_u32(&buff, (uint32_t)lq->cost);

  return ETT_SIZEOF_LQDATA_IN_LQHELLO_PACKET;
}

static int
serialize_tc_lq_ett(unsigned char *buff, const void *tc_lq)
{
  const struct tc_lq_ett *lq = tc_lq;

  pkt_put_u32(&buff, (uint32_t)lq->cost);

  return ETT_SIZEOF_LQDATA_IN_LQTC_PACKET;
}

static void
deserialize_hello_lq_ett(const uint8_t ** curr, void *hello_lq)
{
  struct hello_lq_ett *lq = hello_lq;
  uint8_t lq_value;
  uint32_t cost_value;

  pkt_get_u8(curr, &lq_value);

  /* Three padding bytes */
  pkt_ignore_u8 (curr);
  pkt_ignore_u8 (curr);
  pkt_ignore_u8 (curr);

  pkt_get_u32(curr, &cost_value);

  lq->lq = (float)lq_value / 255.0;
  lq->cost = (float)cost_value;
}

static void
deserialize_tc_lq_ett(const uint8_t ** curr, void *tc_lq)
{
  struct tc_lq_ett *lq = tc_lq;
  uint32_t cost_value;

  pkt_get_u32(curr, &cost_value);

  lq->cost = (float)cost_value;
}

static void
copy_link_lq_into_hello_lq_ett(void *ptr_target_hello_lq, struct link_entry *source_link)
{
  struct hello_lq_ett *target_hello_lq = ptr_target_hello_lq;
  struct link_lq_ett *source_link_lq = (struct link_lq_ett *)source_link->linkquality;

  target_hello_lq->lq = source_link_lq->lq;
  target_hello_lq->cost = source_link->link_cost;
}

static void
copy_link_lq_into_tc_lq_ett(void *ptr_target_tc_lq, struct link_entry *source_link)
{
  struct tc_lq_ett *target_tc_lq = ptr_target_tc_lq;

  target_tc_lq->cost = source_link->link_cost;
}

static void
copy_link_lq_into_tc_edge_ett(struct tc_edge_entry *target_edge, struct link_entry *source_link)
{
  target_edge->link_cost = source_link->link_cost;
}

static void
init_linkquality_in_link_entry_ett(void *ptr_link_lq, const struct network_interface *in_if)
{
  struct link_lq_ett *link_lq = ptr_link_lq;

  link_lq->lq = 0.0;
  link_lq->nlq = 0.0;

  /* The initial link speed is copied from the network interface */
  link_lq->link_speed = in_if->int_lc_medium_speed;

  link_lq->link_cost_data = NULL;
}

/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
