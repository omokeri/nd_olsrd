/*
 * The olsr.org Optimized Link-State Routing daemon (olsrd)
 *
 * (c) by the OLSR project
 *
 * See our Git repository to find out who worked on this file
 * and thus is a copyright holder on it.
 *
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

#include "olsrd_netjson_helpers.h"

#include "olsr.h"
#include <unistd.h>

struct node_entry * netjson_constructMidSelf(struct mid_entry *mid) {
  struct node_entry * node;
  struct olsr_if *ifs;

  memset(mid, 0, sizeof(*mid));
  mid->main_addr = olsr_cnf->main_addr;

  node = olsr_malloc(sizeof(struct node_entry), "netjson NetworkGraph node - MID - self");
  node->avl.key = &mid->main_addr;
  node->isAlias = false;
  node->mid = mid;
  node->link = NULL;
  node->neighbor = NULL;

  for (ifs = olsr_cnf->interfaces; ifs != NULL ; ifs = ifs->next) {
    struct node_entry * node_self_alias;
    union olsr_ip_addr *addr = NULL;

    if (!ifs->configured) {
      continue;
    }

    if (ifs->host_emul) {
      addr = &ifs->hemu_ip;
    } else {
      struct interface_olsr *iface = ifs->interf;
      if (!iface) {
        continue;
      }

      if (olsr_cnf->ip_version == AF_INET) {
        addr = (union olsr_ip_addr *) &iface->ip_addr.v4;
      } else {
        addr = (union olsr_ip_addr *) &iface->int6_addr.sin6_addr;
      }
    }

    node_self_alias = olsr_malloc(sizeof(struct node_entry), "netjson NetworkGraph node - MID - self alias");
    node_self_alias->avl.key = addr;
    node_self_alias->isAlias = true;
    node_self_alias->mid = mid;
    node_self_alias->link = NULL;
    node_self_alias->neighbor = NULL;

    {
      bool is_self_main = (olsr_cnf->ip_version == AF_INET) //
          ? ip4equal(&mid->main_addr.v4, &addr->v4) //
              : ip6equal(&mid->main_addr.v6, &addr->v6);
      if (!is_self_main) {
        struct mid_address *alias = olsr_malloc(sizeof(struct mid_address), "netjson NetworkGraph node - MID - self alias");
        struct mid_address *aliases_saved;

        alias->alias = *addr;
        alias->main_entry = mid;
        alias->next_alias = NULL;
        alias->vtime = 0;

        aliases_saved = mid->aliases;
        mid->aliases = alias;
        alias->next_alias = aliases_saved;
      }
    }
  }

  return node;
}

void netjson_cleanup_mid_self(struct node_entry *node_entry) {
  struct mid_address *alias = node_entry->mid->aliases;
  while (alias) {
    struct mid_address *alias_to_free = alias;
    alias = alias->next_alias;
    free(alias_to_free);
  }
  node_entry->mid->aliases = NULL;
}

void netjson_midIntoNodesTree(struct avl_tree *nodes, struct mid_entry *mid) {
  struct mid_address * alias = mid->aliases;
  struct node_entry * node = olsr_malloc(sizeof(struct node_entry), "netjson NetworkGraph node - MID - main");
  node->avl.key = &mid->main_addr;
  node->isAlias = false;
  node->mid = mid;
  node->link = NULL;
  node->neighbor = NULL;
  if (avl_insert(nodes, &node->avl, AVL_DUP_NO) == -1) {
    /* duplicate */
    free(node);
  }

  if (alias) {
    while (alias) {
      node = olsr_malloc(sizeof(struct node_entry), "netjson NetworkGraph node - MID - alias");
      node->avl.key = &alias->alias;
      node->isAlias = true;
      node->mid = mid;
      node->link = NULL;
      node->neighbor = NULL;
      if (avl_insert(nodes, &node->avl, AVL_DUP_NO) == -1) {
        /* duplicate */
        free(node);
      }

      alias = alias->next_alias;
    }
  }
}

void netjson_linkIntoNodesTree(struct avl_tree *nodes, struct link_entry *link, union olsr_ip_addr *addr) {
  struct avl_node *avlnode;
  struct node_entry *node;

  avlnode = avl_find(nodes, addr);
  if (!avlnode) {
    /* the IP address is not yet known */
    node = olsr_malloc(sizeof(struct node_entry), "netjson NetworkGraph node - link");

    node->avl.key = addr;
    node->isAlias = false;
    node->mid = NULL;
    node->link = link;
    node->neighbor = NULL;
    if (avl_insert(nodes, &node->avl, AVL_DUP_NO) == -1) {
      /* duplicate */
      free(node);
    }
  }
}

void netjson_neighborIntoNodesTree(struct avl_tree *nodes, struct neighbor_entry *neighbor) {
  struct avl_node *avlnode;
  struct node_entry *node;

  union olsr_ip_addr *addr = &neighbor->neighbor_main_addr;
  avlnode = avl_find(nodes, addr);
  if (!avlnode) {
    /* the IP address is not yet known */
    node = olsr_malloc(sizeof(struct node_entry), "netjson NetworkGraph node - neighbor");

    node->avl.key = addr;
    node->isAlias = false;
    node->mid = NULL;
    node->link = NULL;
    node->neighbor = neighbor;
    if (avl_insert(nodes, &node->avl, AVL_DUP_NO) == -1) {
      /* duplicate */
      free(node);
    }
  }
}
