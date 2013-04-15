
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

#include "lq_packet.h" /* LQ_ETXETH_HELLO_MESSAGE, LQ_ETXETH_TC_MESSAGE */
#include "link_set.h" /* link_entry */
#include "tc_set.h" /* tc_edge_entry */
#include "mid_set.h" /* mid_lookup_main_addr() */
#include "olsr_protocol.h" /* pkt_olsr_packet_v4 */
#include "log.h" /* olsr_syslog() */
#include "fpm.h" /* fpm */
#include "parser.h" /* olsr_packetparser_add_function() */
#include "lq_plugin_etx_ffeth.h"

static olsr_linkcost calc_cost_etx_ffeth(float lq, float nlq);
static olsr_linkcost calc_hello_tc_cost_etx_ffeth(const void *);
static olsr_linkcost calc_link_cost_etx_ffeth(const struct link_entry *, const struct network_interface *);

static const char *print_cost_header_etx_ffeth(struct lqtextbuffer *);
static const char *print_link_lq_etx_ffeth(const struct link_entry *, char, struct lqtextbuffer *);
static const char *print_cost_etx_ffeth(olsr_linkcost, struct lqtextbuffer *);

static void initialize_etx_ffeth(void);

static void packet_loss_worker_etx_ffeth(struct link_entry *, bool, const struct network_interface*);
static void memorize_foreign_hello_lq_etx_ffeth(void*, void *);

static int serialize_lq_etx_ffeth(unsigned char *, const void *);
static void deserialize_lq_etx_ffeth(const uint8_t ** curr, void *);

static void copy_link_lq_into_lq_etx_ffeth(void *, struct link_entry *);
static void copy_link_lq_into_tc_edge_etx_ffeth(struct tc_edge_entry *, struct link_entry *);

static void init_linkquality_in_link_entry_etx_ffeth(void *, const struct network_interface *);

/* ETX-ETH link quality plugin (freifunk fpm version) settings */
struct lq_handler lq_etx_ffeth_handler = {
  &calc_hello_tc_cost_etx_ffeth, /* calc_hello_cost() */
  &calc_hello_tc_cost_etx_ffeth, /* calc_tc_cost() */
  &calc_link_cost_etx_ffeth, /* calc_link_cost() */

  &print_cost_header_etx_ffeth, /* print_cost_header() */
  &print_link_lq_etx_ffeth, /* print_link_lq() */
  &print_cost_etx_ffeth, /* print_cost() */

  &initialize_etx_ffeth, /* initialize() */

  &packet_loss_worker_etx_ffeth, /* packet_loss_worker() */
  &memorize_foreign_hello_lq_etx_ffeth, /* memorize_foreign_hello_lq() */

  &serialize_lq_etx_ffeth, /* serialize_hello_lq() */
  &serialize_lq_etx_ffeth, /* serialize_tc_lq() */
  &deserialize_lq_etx_ffeth, /* deserialize_hello_lq() */
  &deserialize_lq_etx_ffeth, /* deserialize_tc_lq() */

  &copy_link_lq_into_lq_etx_ffeth, /* copy_link_lq_into_hello_lq() */
  &copy_link_lq_into_lq_etx_ffeth, /* copy_link_lq_into_tc_lq() */
  &copy_link_lq_into_tc_edge_etx_ffeth, /* copy_link_lq_into_tc_edge() */

  &init_linkquality_in_link_entry_etx_ffeth, /* init_linkquality_in_link_entry() */

  LQ_ETXETH_HELLO_MESSAGE, /* lq_hello_message_type, NOT compatible with normal ETX! */
  LQ_ETXETH_TC_MESSAGE, /* lq_tc_message_type, NOT compatible with normal ETX! */

  4, /* sizeof_lqdata_in_lqhello_packet */
  4, /* sizeof_lqdata_in_lqtc_packet */

  sizeof(struct lq_etx_ff), /* sizeof_linkquality_in_lq_hello_neighbor */
  sizeof(struct lq_etx_ff), /* sizeof_linkquality_in_lq_tc_neighbor */
  sizeof(struct link_lq_etx_ffeth) /* sizeof_linkquality_in_link_entry */
};

static void
handle_lqchange_etx_ffeth(void) {
  struct link_entry *link;

  bool triggered = false;

  OLSR_FOR_ALL_LINK_ENTRIES(link) {

    struct ipaddr_str buf;
    bool relevant = false;
    struct link_lq_etx_ffeth *lq = (struct link_lq_etx_ffeth *)link->linkquality;

#if 0
  fprintf(stderr, "%s: old = %u/%u   new = %u/%u\n", olsr_ip_to_string(&buf, &link->neighbor_iface_addr),
      lq->smoothed_lq.valueLq, lq->smoothed_lq.valueNlq,
      lq->lq.valueLq, lq->lq.valueNlq);
#endif

    if (lq->smoothed_lq.valueLq < lq->lq.valueLq) {
      if (lq->lq.valueLq >= 254 || lq->lq.valueLq - lq->smoothed_lq.valueLq > lq->smoothed_lq.valueLq/10) {
        relevant = true;
      }
    }
    else if (lq->smoothed_lq.valueLq > lq->lq.valueLq) {
      if (lq->smoothed_lq.valueLq - lq->lq.valueLq > lq->smoothed_lq.valueLq/10) {
        relevant = true;
      }
    }
    if (lq->smoothed_lq.valueNlq < lq->lq.valueNlq) {
      if (lq->lq.valueNlq >= 254 || lq->lq.valueNlq - lq->smoothed_lq.valueNlq > lq->smoothed_lq.valueNlq/10) {
        relevant = true;
      }
    }
    else if (lq->smoothed_lq.valueNlq > lq->lq.valueNlq) {
      if (lq->smoothed_lq.valueNlq - lq->lq.valueNlq > lq->smoothed_lq.valueNlq/10) {
        relevant = true;
      }
    }

    if (relevant) {
      memcpy(&lq->smoothed_lq, &lq->lq, sizeof(struct lq_etx_ff));
      link->link_cost = calc_cost_etx_ffeth(lq->smoothed_lq.valueLq, lq->smoothed_lq.valueNlq);
      triggered = true;
    }
  } OLSR_FOR_ALL_LINK_ENTRIES_END(link)

  if (!triggered) {
    return;
  }

  OLSR_FOR_ALL_LINK_ENTRIES(link) {
    struct link_lq_etx_ffeth *lq = (struct link_lq_etx_ffeth *)link->linkquality;

    if (lq->smoothed_lq.valueLq >= 254 && lq->smoothed_lq.valueNlq >= 254) {
      continue;
    }

    if (lq->smoothed_lq.valueLq == lq->lq.valueLq && lq->smoothed_lq.valueNlq == lq->lq.valueNlq) {
      continue;
    }

    memcpy(&lq->smoothed_lq, &lq->lq, sizeof(struct lq_etx_ff));
    link->link_cost = calc_cost_etx_ffeth(lq->smoothed_lq.valueLq, lq->smoothed_lq.valueNlq);
  } OLSR_FOR_ALL_LINK_ENTRIES_END(link)

  olsr_relevant_linkcost_change();
}

static void
parser_etx_ffeth(struct pkt_olsr_packet_v4 *olsr, struct network_interface *in_if, union olsr_ip_addr *from_addr)
{
  const union olsr_ip_addr *main_addr;
  struct link_entry *lnk;
  struct link_lq_etx_ffeth *lq;
  uint32_t seq_diff;

  /* Find main address */
  main_addr = mid_lookup_main_addr(from_addr);

  /* Loopup link entry */
  lnk = lookup_link_entry(from_addr, main_addr, in_if);
  if (lnk == NULL) {
    return;
  }

  lq = (struct link_lq_etx_ffeth *)lnk->linkquality;

  /* ignore double packet */
  if (lq->last_seq_nr == olsr->seqno) {
    struct ipaddr_str buf;
    olsr_syslog(
      OLSR_LOG_INFO,
      "detected duplicate packet with seqnr %d from %s on %s (%d Bytes)",
      olsr->seqno,
      olsr_ip_to_string(&buf, from_addr),
      in_if->int_name,
      ntohs(olsr->packlen));
    return;
  }

  if (lq->last_seq_nr > olsr->seqno) {
    seq_diff = (uint32_t) olsr->seqno + 65536 - lq->last_seq_nr;
  } else {
    seq_diff = olsr->seqno - lq->last_seq_nr;
  }

  /* Jump in sequence numbers ? */
  if (seq_diff > 256) {
    seq_diff = 1;
  }

  lq->received[lq->activeIdx]++;
  lq->total[lq->activeIdx] += seq_diff;

  lq->last_seq_nr = olsr->seqno;
  lq->missed_hellos = 0;
}

static void
timer_etx_ffeth(void __attribute__ ((unused)) * context)
{
  struct link_entry *link;

  OLSR_FOR_ALL_LINK_ENTRIES(link) {
    struct link_lq_etx_ffeth *tlq = (struct link_lq_etx_ffeth *)link->linkquality;
    fpm ratio;
    int i, received, total;

    received = 0;
    total = 0;

    /* Enlarge window if still in quickstart phase */
    if (tlq->windowSize < LQ_FFETH_WINDOW) {
      tlq->windowSize++;
    }
    for (i = 0; i < tlq->windowSize; i++) {
      received += tlq->received[i];
      total += tlq->total[i];
    }

    /* Calculate link quality */
    if (total == 0) {
      tlq->lq.valueLq = 0;

    } else {

      /* Start with link-loss-factor */
      ratio = fpmidiv(itofpm(link->loss_link_multiplier), LINK_LOSS_MULTIPLIER);

      /* Keep missed hello periods in mind (round down(??) hello interval to seconds)
       *
       * What if 'link->inter->hello_etime' < ''LQ_FFETH_WINDOW' (32) * 1000 (hardcoded?)  ??
       * Then always 0 is subtracted from 'received'. This seems to be not good.
       *
       * Just a scenario. Suppose that:
       * - received = 100
       * - tlq->missed_hellos = 1
       * - link->inter->hello_etime = 1000
       * - LQ_FFETH_WINDOW = 32
       *
       * Then the new value for received is:
       * 100 - (
       *   100
       *     * 1 ==> 100
       *     * 1000 ==> 100000
       *     / 1000 ==> 100
       *     / 32 ==> 31 )
       * = 100 - 31 = 69
       */
      if (tlq->missed_hellos > 1) {
        received = received - received * tlq->missed_hellos * link->inter->hello_etime/1000 / LQ_FFETH_WINDOW;
      }

      /* Calculate received/total factor */

      /* Continuing the above scenario, assuming:
       * - link->loss_link_multiplier == LINK_LOSS_MULTIPLIER
       * - total = 101 (we missed 1 hello out of 101)
       *
       * ratio = 1 * 69 = 69
       * ratio = 69 / 101 = 0,68316831...
       * ratio = 0,68316831... * 255 = 174,2...
       * tlq->lq.valueLq = 174 
       * 
       * This value seems quite unlogical.
       *
       * Please add a comment. */

      ratio = fpmmuli(ratio, received);
      ratio = fpmidiv(ratio, total);
      ratio = fpmmuli(ratio, 255);

      tlq->lq.valueLq = (uint8_t) (fpmtoi(ratio));
    }

    /* ethernet booster */
    if (link->inter->mode == IF_MODE_ETHER) {
      if (tlq->lq.valueLq > (uint8_t)(0.95 * 255)) {
        tlq->perfect_eth = true;
      }
      else if (tlq->lq.valueLq > (uint8_t)(0.90 * 255)) {
        tlq->perfect_eth = false;
      }

      if (tlq->perfect_eth) {
        tlq->lq.valueLq = 255;
      }
    }
    else if (link->inter->mode != IF_MODE_ETHER && tlq->lq.valueLq > 0) {
      tlq->lq.valueLq--;
    }

    /* Shift buffer */
    tlq->activeIdx = (tlq->activeIdx + 1) % LQ_FFETH_WINDOW;
    tlq->total[tlq->activeIdx] = 0;
    tlq->received[tlq->activeIdx] = 0;
  } OLSR_FOR_ALL_LINK_ENTRIES_END(link);

  handle_lqchange_etx_ffeth();
}

static olsr_linkcost
calc_cost_etx_ffeth(float lq, float nlq)
{
  olsr_linkcost result;
  bool ether;
  int lq_int, nlq_int;

  if (lq < (unsigned int)(255 * MINIMAL_USEFUL_LQ) || nlq < (unsigned int)(255 * MINIMAL_USEFUL_LQ)) {
    return LINK_COST_BROKEN;
  }

  /* TODO: why is a link with quality 1.0 always an ethernet link? Are there no 802.11 links
   * without loss? Please add a comment. */
  ether = lq == 255 && nlq == 255;

  lq_int = (int)lq;
  if (lq_int > 0 && lq_int < 255) {
    lq_int++;
  }

  nlq_int = (int)nlq;
  if (nlq_int > 0 && nlq_int < 255) {
    nlq_int++;
  }
  result = fpmidiv(itofpm(255 * 255), lq_int * nlq_int);
  if (ether) {
    /* ethernet boost */
    result /= 10;
  }

  if (result > LINK_COST_BROKEN) {
    return LINK_COST_BROKEN;
  }
  if (result == 0) {
    return 1;
  }
  return result;
}

static olsr_linkcost
calc_hello_tc_cost_etx_ffeth(const void *hello_tc_lq)
{
  const struct lq_etx_ff *lq = hello_tc_lq;
  return calc_cost_etx_ffeth(lq->valueLq, lq->valueNlq);
}

static olsr_linkcost
calc_link_cost_etx_ffeth(const struct link_entry *link, const struct network_interface* in_if __attribute__ ((unused)))
{
  const struct link_lq_etx_ffeth *lq = (const struct link_lq_etx_ffeth *)link->linkquality;
  return calc_cost_etx_ffeth(lq->lq.valueLq, lq->lq.valueNlq);
}

static const char *
print_cost_header_etx_ffeth(struct lqtextbuffer *buffer)
{
  sprintf(buffer->buf, "ETX-FF-ETH");
  return buffer->buf;
}

static const char *
print_link_lq_etx_ffeth(const struct link_entry *link, char separator, struct lqtextbuffer *buffer)
{
  const struct link_lq_etx_ffeth *link_lq = (const struct link_lq_etx_ffeth *)link->linkquality;
  const struct lq_etx_ff *lq = &link_lq->lq;
  int lq_int, nlq_int;

  lq_int = (int)lq->valueLq;
  if (lq_int > 0 && lq_int < 255) {
    lq_int++;
  }

  nlq_int = (int)lq->valueNlq;
  if (nlq_int > 0 && nlq_int < 255) {
    nlq_int++;
  }

  snprintf(
    buffer->buf,
    sizeof(buffer->buf),
    "LQ = %s%cNLQ = %s",
    fpmtoa(fpmidiv(itofpm(lq_int), 255)),
    separator,
    fpmtoa(fpmidiv(itofpm(nlq_int), 255)));

  /* Ensure null-termination also with Windows C libraries */
  buffer->buf[sizeof(buffer->buf) - 1] = '\0';

  return buffer->buf;
}

static const char *
print_cost_etx_ffeth(olsr_linkcost cost, struct lqtextbuffer *buffer)
{
  snprintf(buffer->buf, sizeof(buffer->buf), "%s", fpmtoa(cost));

  /* Ensure null-termination also with Windows C libraries */
  buffer->buf[sizeof(buffer->buf) - 1] = '\0';

  return buffer->buf;
}

static void
initialize_etx_ffeth(void)
{
  if (olsr_cnf->lq_nat_thresh < 1.0) {
    fprintf(stderr, "Warning, nat_treshold < 1.0 is more likely to produce loops with etx_ffeth\n");
  }
  olsr_packetparser_add_function(&parser_etx_ffeth);
  olsr_start_timer(1000, 0, OLSR_TIMER_PERIODIC, &timer_etx_ffeth, NULL, 0);
}

static void
packet_loss_worker_etx_ffeth(struct link_entry *link, bool lost, const struct network_interface* in_if __attribute__ ((unused)))
{
  struct link_lq_etx_ffeth *link_lq = (struct link_lq_etx_ffeth *)link->linkquality;

  if (lost) {
    link_lq->missed_hellos++;
  }
  return;
}

static void
memorize_foreign_hello_lq_etx_ffeth(void *ptr_local_link_lq, void *ptr_foreign_hello_lq)
{
  struct link_lq_etx_ffeth *local_link_lq = ptr_local_link_lq;
  struct lq_etx_ff *foreign = ptr_foreign_hello_lq;

  if (foreign != NULL) {
    local_link_lq->lq.valueNlq = foreign->valueLq;
  } else {
    local_link_lq->lq.valueNlq = 0;
  }
}

static int
serialize_lq_etx_ffeth(unsigned char *buff, const void *hello_tc_lq)
{
  const struct lq_etx_ff *lq = hello_tc_lq;

  buff[0] = 0;
  buff[1] = 0;
  buff[2] = (unsigned char)lq->valueLq;
  buff[3] = (unsigned char)lq->valueNlq;

  return 4;
}

static void
deserialize_lq_etx_ffeth(const uint8_t ** curr, void *hello_tc_lq)
{
  struct lq_etx_ff *lq = hello_tc_lq;

  pkt_ignore_u16(curr);
  pkt_get_u8(curr, &lq->valueLq);
  pkt_get_u8(curr, &lq->valueNlq);
}

static void
copy_link_lq_into_lq_etx_ffeth(void *ptr_target_lq, struct link_entry *source_link)
{
  struct lq_etx_ff *target_lq = ptr_target_lq;
  struct link_lq_etx_ffeth *source_link_lq = (struct link_lq_etx_ffeth *)source_link->linkquality;

  *target_lq = source_link_lq->smoothed_lq;
}

static void
copy_link_lq_into_tc_edge_etx_ffeth(struct tc_edge_entry *target_edge, struct link_entry *source_link)
{
  target_edge->link_cost = source_link->link_cost;
}

static void
init_linkquality_in_link_entry_etx_ffeth(void *ptr_link_lq, const struct network_interface *in_if __attribute__ ((unused)))
{
  struct link_lq_etx_ffeth *link_lq = ptr_link_lq;
  int i;

  link_lq->smoothed_lq.valueLq = 0;
  link_lq->smoothed_lq.valueNlq = 0;
  link_lq->lq.valueLq = 0;
  link_lq->lq.valueNlq = 0;
  link_lq->windowSize = LQ_FFETH_QUICKSTART_INIT;
  for (i = 0; i < LQ_FFETH_WINDOW; i++) {
    link_lq->total[i] = 3;
  }
}


/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
