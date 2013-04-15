
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
#include <stddef.h> /* NULL */
#include <assert.h> /* assert() */ 
#include <string.h> /* strcasecmp() */
#include <stdlib.h> /* free() */

/* OLSRD include */
#include "olsr_types.h" /* uint8_t, olsr_linkcost */
#include "common/avl.h" /* avl_tree, avl_find(), avl_init(), avl_insert() */
#include "defs.h" /* olsr_cnf, OLSR_PRINTF, strscpy() */
#include "olsr.h" /* olsr_exit(), olsr_calloc() */
#include "olsr_cfg.h" /* DEF_LQ_ALGORITHM */
#include "link_set.h" /* link_entry */
#include "interfaces.h" /* network_interface */
#include "lq_packet.h" /* lq_hello_neighbor, lq_tc_neighbor */
#include "tc_set.h" /* tc_edge_entry */
#include "lq_plugin_etx.h" /* lq_etx_handler, LQ_ALGORITHM_ETX_NAME */
#include "lq_plugin_etx_fpm.h" /* lq_etx_fpm_handler, LQ_ALGORITHM_ETX_FPM_NAME */
#include "lq_plugin_etx_ff.h" /* lq_etx_ff_handler, LQ_ALGORITHM_ETX_FF_NAME */
#include "lq_plugin_etx_ffeth.h" /* lq_etx_ffeth_handler, LQ_ALGORITHM_ETX_FFETH_NAME */
#include "lq_plugin_ett.h" /* lq_ett_handler, LQ_ALGORITHM_ETT_NAME */
#include "lq_plugin_rfc.h" /* lq_rfc_compliant_handler, LQ_ALGORITHM_RFC_NAME */
#include "lq_plugin.h" /* register_lq_handler(), lqtextbuffer */

struct avl_tree lq_handler_tree;
struct lq_handler *active_lq_handler = NULL;

/**
 * case-insensitive string comparator for avl-trees
 * @param str1
 * @param str2
 * @return
 */
static int
avl_strcasecmp(const void *str1, const void *str2)
{
  return strcasecmp(str1, str2);
}

/**
 * Activate a LQ handler
 * @param name
 */
static void
activate_lq_handler(const char *name)
{
  struct lq_handler_node *node;

  node = (struct lq_handler_node *)avl_find(&lq_handler_tree, name);
  if (node == NULL) {
    OLSR_PRINTF(1, "Error, unknown lq_handler '%s'\n", name);
    olsr_exit("", 1);
  }

  OLSR_PRINTF(1, "Using '%s' algorithm for lq calculation.\n", name);
  active_lq_handler = node->handler;
  if (active_lq_handler->initialize != NULL) {
    active_lq_handler->initialize();
  }
}

/**
 * Initialize LQ handler
 */
void
init_lq_handler_tree(void)
{
  avl_init(&lq_handler_tree, &avl_strcasecmp);
  if (olsr_cnf->lq_level > 0) {
    register_lq_handler(&lq_etx_handler, LQ_ALGORITHM_ETX_NAME);
    register_lq_handler(&lq_etx_fpm_handler, LQ_ALGORITHM_ETX_FPM_NAME);
    register_lq_handler(&lq_etx_ff_handler, LQ_ALGORITHM_ETX_FF_NAME);
    register_lq_handler(&lq_etx_ffeth_handler, LQ_ALGORITHM_ETX_FFETH_NAME);
    register_lq_handler(&lq_ett_handler, LQ_ALGORITHM_ETT_NAME);
  }
  register_lq_handler(&lq_rfc_compliant_handler, LQ_ALGORITHM_RFC_NAME);

  if (olsr_cnf->lq_level == 0) {
    activate_lq_handler(LQ_ALGORITHM_RFC_NAME);
  }
  else if (olsr_cnf->lq_algorithm == NULL) {
    activate_lq_handler(DEF_LQ_ALGORITHM);
  }
  else {
    activate_lq_handler(olsr_cnf->lq_algorithm);
  }
}

/**
 * register_lq_handler
 *
 * Used by routing metric plugins to activate their link quality handler
 *
 * @param pointer to lq_handler structure
 * @param name of the link quality handler for debug output
 */
void
register_lq_handler(struct lq_handler *handler, const char *name)
{
  struct lq_handler_node *node;
  size_t name_size = sizeof(*node) + strlen(name) + 1;

  node = olsr_calloc(name_size, "olsr lq handler");

  strscpy(node->name, name, name_size);
  node->node.key = node->name;
  node->handler = handler;

  avl_insert(&lq_handler_tree, &node->node, false);
}

/**
 * olsr_update_packet_loss_worker
 *
 * this function is called every times a hello packet for a certain link entry
 * is lost (timeout) or received. This way the lq-plugin can update the link entries link
 * quality data.
 *
 * @param link_entry
 * @param true if hello packet was lost
 * @param in_if network interface
 */
void
olsr_update_packet_loss_worker(struct link_entry *link, bool lost, const struct network_interface* in_if)
{
  assert((const char *)link + sizeof(*link) >= (const char *)link->linkquality);

  if (active_lq_handler->packet_loss_worker != NULL) {
    active_lq_handler->packet_loss_worker(link, lost, in_if);
  }
}

/**
 * olsr_memorize_foreign_hello_lq
 *
 * this function is called to copy the link quality information from a received
 * hello packet into a link_entry.
 *
 * @param link_entry
 * @param lq_hello_neighbor, if NULL the neighbor link quality information
 * of the link entry has to be reset to "zero"
 */
void
olsr_memorize_foreign_hello_lq(
  struct link_entry *link,
  struct lq_hello_neighbor *foreign)
{
  assert((const char *)link + sizeof(*link) >= (const char *)link->linkquality);

  if (active_lq_handler->memorize_foreign_hello_lq != NULL) {
    if (foreign != NULL) {
      assert((const char *)foreign + sizeof(*foreign) >= (const char *)foreign->linkquality);
      active_lq_handler->memorize_foreign_hello_lq(link->linkquality, foreign->linkquality);

    } else {
      active_lq_handler->memorize_foreign_hello_lq(link->linkquality, NULL);
    }
  }
}

/**
 * olsr_calloc_lq_hello_neighbor
 *
 * this function allocates memory for an lq_hello_neighbor inclusive
 * linkquality data.
 *
 * @param id string for memory debugging
 *
 * @return pointer to lq_hello_neighbor
 */
struct lq_hello_neighbor *
olsr_calloc_lq_hello_neighbor(const char *id)
{
  struct lq_hello_neighbor *h;

  h = olsr_calloc(sizeof(struct lq_hello_neighbor) + active_lq_handler->sizeof_linkquality_in_lq_hello_neighbor, id);

  assert((const char *)h + sizeof(*h) >= (const char *)h->linkquality);
  return h;
}

/**
 * olsr_serialize_hello_lq_data
 *
 * this function converts the lq information of a lq_hello_neighbor into binary packet
 * format
 *
 * @param pointer to binary buffer to write into
 * @param pointer to lq_hello_neighbor
 * @return number of bytes that have been written
 */
int
olsr_serialize_hello_lq_data(unsigned char *buff, struct lq_hello_neighbor *neigh)
{
  assert((const char *)neigh + sizeof(*neigh) >= (const char *)neigh->linkquality);

  if (active_lq_handler->serialize_hello_lq != NULL) {
    return active_lq_handler->serialize_hello_lq(buff, neigh->linkquality);
  }
  return 0;
}

/**
 * olsr_deserialize_hello_lq_data
 *
 * this function reads the link quality data of a HELLO message into a lq_hello_neighbor
 * and uses that data to calculate the cost field of the lq_hello_neighbor.
 *
 * @param pointer to the current buffer pointer
 * @param lq_hello_neighbor
 */
void
olsr_deserialize_hello_lq_data(const uint8_t ** curr, struct lq_hello_neighbor *neigh)
{
  assert((const char *)neigh + sizeof(*neigh) >= (const char *)neigh->linkquality);

  if (active_lq_handler->deserialize_hello_lq != NULL) {
    active_lq_handler->deserialize_hello_lq(curr, neigh->linkquality);
  }

  neigh->cost = active_lq_handler->calc_hello_cost(neigh->linkquality);
}

/**
 * olsr_copy_link_lq_into_hello_lq
 *
 * this function copies the link quality information from a link_entry to a
 * lq_hello_neighbor.
 *
 * @param target_hello_lq lq_hello_neighbor
 * @param source_link link_entry
 */
void
olsr_copy_link_lq_into_hello_lq(struct lq_hello_neighbor *target_hello_lq, struct link_entry *source_link)
{
  assert((const char *)target_hello_lq + sizeof(*target_hello_lq) >= (const char *)target_hello_lq->linkquality);

  if (active_lq_handler->copy_link_lq_into_hello_lq != NULL) {
    active_lq_handler->copy_link_lq_into_hello_lq(target_hello_lq->linkquality, source_link);
  }
}

/**
 * olsr_calloc_lq_tc_neighbor
 *
 * this function allocates memory for an lq_tc_neighbor including linkquality data.
 *
 * @param id string for memory debugging
 *
 * @return pointer to lq_tc_neighbor
 */
struct lq_tc_neighbor *
olsr_calloc_lq_tc_neighbor(const char *id)
{
  return olsr_calloc(sizeof(struct lq_tc_neighbor) + active_lq_handler->sizeof_linkquality_in_lq_tc_neighbor, id);
}

/**
 * olsr_serialize_tc_lq_data
 *
 * this function converts the link quality data of a lq_tc_neighbor
 * into binary packet format
 *
 * @param binary buffer to write into
 * @param lq_tc_neighbor
 * @return number of bytes that have been written
 */
int
olsr_serialize_tc_lq_data(unsigned char *buff, struct lq_tc_neighbor *neigh)
{
  assert((const char *)neigh + sizeof(*neigh) >= (const char *)neigh->linkquality);

  if (active_lq_handler->serialize_tc_lq != NULL) {
    return active_lq_handler->serialize_tc_lq(buff, neigh->linkquality);
  }
  return 0;
}

/**
 * olsr_deserialize_tc_lq_data
 *
 * this function reads the link quality data of a TC message and uses that to
 * calculate the corresponding link cost value.
 *
 * @param curr pointer to the current buffer pointer
 * @return the calculated link cost value
 */
olsr_linkcost
olsr_deserialize_tc_lq_data(const uint8_t ** curr)
{
  struct lq_tc_neighbor *neigh = olsr_calloc_lq_tc_neighbor("TC deserialization");
  olsr_linkcost result;

  assert((const char *)neigh + sizeof(*neigh) >= (const char *)neigh->linkquality);

  if (active_lq_handler->deserialize_tc_lq != NULL) {
    active_lq_handler->deserialize_tc_lq(curr, neigh->linkquality);
  }

  result = active_lq_handler->calc_tc_cost(neigh->linkquality);

  free(neigh);
  return result;
}

/**
 * olsr_copy_link_lq_into_tc_lq
 *
 * this function copies the link quality information from a link_entry to a
 * lq_tc_neighbor.
 *
 * @param target_tc_lq lq_tc_neighbor
 * @param source_link link_entry
 */
void
olsr_copy_link_lq_into_tc_lq(struct lq_tc_neighbor *target_tc_lq, struct link_entry *source_link)
{
  assert((const char *)target_tc_lq + sizeof(*target_tc_lq) >= (const char *)target_tc_lq->linkquality);

  if (active_lq_handler->copy_link_lq_into_tc_lq != NULL) {
    active_lq_handler->copy_link_lq_into_tc_lq(target_tc_lq->linkquality, source_link);
  }
}

/**
 * olsr_calloc_link_entry
 *
 * this function allocates memory for an link_entry inclusive
 * linkquality data.
 *
 * @param id string for memory debugging
 *
 * @return pointer to link_entry
 */
struct link_entry *
olsr_calloc_link_entry(const char *id)
{
  return olsr_calloc(sizeof(struct link_entry) + active_lq_handler->sizeof_linkquality_in_link_entry, id);
}

/* Initialize the link quality data of a link set entry */
void olsr_init_linkquality_in_link_entry(struct link_entry *link, const struct network_interface *netIf)
{
  assert((const char *)link + sizeof(*link) >= (const char *)link->linkquality);

  if (active_lq_handler->init_linkquality_in_link_entry != NULL) {
    active_lq_handler->init_linkquality_in_link_entry(link->linkquality, netIf);
  }
}

/**
 * olsr_calc_link_cost
 *
 * this function calculates the linkcost of a link_entry
 *
 * @param link_entry
 * @param network_interface
 * @return linkcost
 */
olsr_linkcost
olsr_calc_link_cost(const struct link_entry *link, const struct network_interface* in_if)
{
  assert((const char *)link + sizeof(*link) >= (const char *)link->linkquality);
  return active_lq_handler->calc_link_cost(link, in_if);
}

/**
 * get_link_entry_text
 *
 * this function returns the text representation of a link_entry cost value.
 * It's not thread safe and should not be called twice with the same println
 * value in the same context (a single printf command for example).
 *
 * @param pointer to link_entry
 * @param char separator between LQ and NLQ
 * @param buffer for output
 * @return pointer to a buffer with the text representation
 *
 * Note: the output text may be at most sizeof(buffer->buf) (LQ_TEXT_BUF_SIZE) characters,
 * including the terminating NULL! 
 */
const char *
get_link_entry_text(struct link_entry *entry, char separator, struct lqtextbuffer *buffer)
{
  return active_lq_handler->print_link_lq(entry, separator, buffer);
}

/**
 * olsr_copylq_link_entry_into_tc_edge_entry
 *
 * this function copies the link quality information from a link_entry to a
 * tc_edge_entry.
 *
 * @param tc_edge_entry
 * @param link_entry
 */
void
olsr_copylq_link_entry_into_tc_edge_entry(struct tc_edge_entry *target_entry, struct link_entry *source_link)
{
  if (active_lq_handler->copy_link_lq_into_tc_edge != NULL) {
    active_lq_handler->copy_link_lq_into_tc_edge(target_entry, source_link);
  }
}

/**
 * get_linkcost_headertext
 *
 * This function copies the name of the used link cost metric (e.g. "ETX", "ETT" or "MTM")
 * into a buffer.
 *
 * @param pointer to buffer
 * @return pointer to buffer filled with text
 *
 * Note: the output text may be at most sizeof(buffer->buf) (LQ_TEXT_BUF_SIZE) characters,
 * including the terminating NULL! 
 */
const char *
get_linkcost_headertext(struct lqtextbuffer *buffer)
{
  return active_lq_handler->print_cost_header(buffer);
}

/**
 * get_linkcost_text
 *
 * This function transforms an olsr_linkcost value into it's text representation and copies
 * the result into a buffer.
 *
 * @param linkcost value
 * @param true to transform the cost of a route, false for a link
 * @param pointer to buffer
 * @return pointer to buffer filled with text
 *
 * Note: the output text may be at most sizeof(buffer->buf) (LQ_TEXT_BUF_SIZE) characters,
 * including the terminating NULL! 
 */
const char *
get_linkcost_text(olsr_linkcost cost, bool route, struct lqtextbuffer *buffer)
{
  static const char *infinite = "INFINITE";

  if (route) {
    if (cost >= ROUTE_COST_BROKEN) {
      return infinite;
    }
  } else {
    if (cost >= LINK_COST_BROKEN) {
      return infinite;
    }
  }
  return active_lq_handler->print_cost(cost, buffer);
}

uint8_t olsr_get_hello_message_type(void) {
  return active_lq_handler->lq_hello_message_type;
}

uint8_t olsr_get_tc_message_type(void) {
  return active_lq_handler->lq_tc_message_type;
}

size_t olsr_sizeof_lqdata_in_lqhello_packet(void) {
  return active_lq_handler->sizeof_lqdata_in_lqhello_packet;
}

size_t olsr_sizeof_lqdata_in_lqtc_packet(void) {
  return active_lq_handler->sizeof_lqdata_in_lqtc_packet;
}

/**
 * This function should be called whenever the current link cost
 * value changed in a relevant way.
 *
 * @param link pointer to current link
 * @param newcost new cost of this link
 */
void olsr_relevant_linkcost_change(void) {
  changes_neighborhood = true;
  changes_topology = true;

  /* TODO - we should check whether we actually announce this neighbour */
  signal_link_changes(true);
}

/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
