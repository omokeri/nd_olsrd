
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

#ifndef _LQ_PLUGIN_H
#define _LQ_PLUGIN_H

#include "olsr_types.h" /* uint8_t, olsr_linkcost */
#include "common/avl.h" /* AVLNODE2STRUCT */

#define LINK_COST_BROKEN (1<<22)
#define ROUTE_COST_BROKEN (0xffffffff)
#define ZERO_ROUTE_COST 0

#define MINIMAL_USEFUL_LQ 0.1
#define LQ_PLUGIN_RELEVANT_COSTCHANGE 16

#define LQ_QUICKSTART_STEPS 12
#define LQ_QUICKSTART_AGING 0.25

#define LQ_TEXT_BUF_SIZE 64
struct lqtextbuffer {
  char buf[LQ_TEXT_BUF_SIZE];
};

/* Forward declarations */
struct link_entry;
struct tc_edge_entry;
struct lq_hello_neighbor;
struct network_interface;
struct link_entry;
struct tc_edge_entry;

struct lq_handler {

  /* Each LQ hander *must* always implement the following functions (--> pure virtual) */

  olsr_linkcost (*calc_hello_cost) (const void *hello_lq);
  olsr_linkcost (*calc_tc_cost) (const void *tc_lq);
  olsr_linkcost (*calc_link_cost) (const struct link_entry *link, const struct network_interface* in_if);

  const char *(*print_cost_header) (struct lqtextbuffer *buffer);
  const char *(*print_link_lq) (const struct link_entry *link, char separator, struct lqtextbuffer *buffer);
  const char *(*print_cost) (olsr_linkcost cost, struct lqtextbuffer *buffer);

  /* Each LQ hander *may* implement the following functions, or fill these pointers with NULL if
   * not applicable (--> virtual) */

  void (*initialize) (void);

  void (*packet_loss_worker) (struct link_entry *link, bool lost, const struct network_interface* in_if);
  void (*memorize_foreign_hello_lq) (void* ptr_local_link_lq, void *ptr_foreign_hello_lq);

  int (*serialize_hello_lq) (unsigned char *buff, const void *hello_lq);
  int (*serialize_tc_lq) (unsigned char *buff, const void *tc_lq);
  void (*deserialize_hello_lq) (const uint8_t ** curr, void *hello_lq);
  void (*deserialize_tc_lq) (const uint8_t ** curr, void *tc_lq);

  void (*copy_link_lq_into_hello_lq) (void *ptr_target_hello_lq, struct link_entry *source_link);
  void (*copy_link_lq_into_tc_lq) (void *ptr_target_tc_lq, struct link_entry *source_link);
  void (*copy_link_lq_into_tc_edge) (struct tc_edge_entry *target_entry, struct link_entry *source_link);

  void (*init_linkquality_in_link_entry) (void *ptr_link_lq, const struct network_interface *);

  /* Each LQ hander *must* always supply values for the following fields */

  uint8_t lq_hello_message_type;
  uint8_t lq_tc_message_type;

  size_t sizeof_lqdata_in_lqhello_packet;
  size_t sizeof_lqdata_in_lqtc_packet;

  size_t sizeof_linkquality_in_lq_hello_neighbor;
  size_t sizeof_linkquality_in_lq_tc_neighbor;
  size_t sizeof_linkquality_in_link_entry;
};

struct lq_handler_node {
  struct avl_node node;
  struct lq_handler *handler;
  char name[0];
};

AVLNODE2STRUCT(lq_handler_tree2lq_handler_node, struct lq_handler_node, node);

void init_lq_handler_tree(void);

void register_lq_handler(struct lq_handler *, const char *);

void olsr_update_packet_loss_worker(struct link_entry *, bool, const struct network_interface* in_if);
void olsr_memorize_foreign_hello_lq(struct link_entry *, struct lq_hello_neighbor *);

/* Functions operating on the link quality data of struct lq_hello_neighbor */
struct lq_hello_neighbor *olsr_calloc_lq_hello_neighbor(const char *);
int olsr_serialize_hello_lq_data(unsigned char *, struct lq_hello_neighbor *);
void olsr_deserialize_hello_lq_data(const uint8_t **, struct lq_hello_neighbor *);
void olsr_copy_link_lq_into_hello_lq(struct lq_hello_neighbor *, struct link_entry *);

/* Functions operating on the link quality data of struct lq_tc_neighbor */
struct lq_tc_neighbor *olsr_calloc_lq_tc_neighbor(const char *);
int olsr_serialize_tc_lq_data(unsigned char *, struct lq_tc_neighbor *);
olsr_linkcost olsr_deserialize_tc_lq_data(const uint8_t ** curr);
void olsr_copy_link_lq_into_tc_lq(struct lq_tc_neighbor *, struct link_entry *);

/* Functions operating on the link quality data of struct link_entry */
struct link_entry *olsr_calloc_link_entry(const char *);
void olsr_init_linkquality_in_link_entry(struct link_entry *, const struct network_interface *);
olsr_linkcost olsr_calc_link_cost(const struct link_entry *, const struct network_interface *);
const char *get_link_entry_text(struct link_entry *, char, struct lqtextbuffer *);

/* Functions operating on the link quality data of struct tc_edge_entry */
void olsr_copylq_link_entry_into_tc_edge_entry(struct tc_edge_entry *, struct link_entry *);

const char *get_linkcost_headertext(struct lqtextbuffer *);
const char *get_linkcost_text(olsr_linkcost, bool, struct lqtextbuffer *);

uint8_t olsr_get_hello_message_type(void);
uint8_t olsr_get_tc_message_type(void);

size_t olsr_sizeof_lqdata_in_lqhello_packet(void);
size_t olsr_sizeof_lqdata_in_lqtc_packet(void);

void olsr_relevant_linkcost_change(void);

/* Externals. */
extern struct lq_handler *active_lq_handler;

#endif /* _LQ_PLUGIN_H */

/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
