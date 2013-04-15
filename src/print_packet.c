
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004, Andreas Tonnesen(andreto@olsr.org)
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

#include "defs.h"
#include "olsr.h" /* olsr_link_to_string() */
#include "lq_packet.h" /* pkt_lq_hello_info_header */
#include "olsr_protocol.h" /* OLSR_HEADERSIZE */
#include "print_packet.h"

static void
print_messagedump(FILE * handle, uint8_t * msg, int16_t size)
{
  int i, x = 0;

  fprintf(handle, "     Data dump:\n     ");
  for (i = 0; i < size; i++) {
    if (x == 4) {
      x = 0;
      fprintf(handle, "\n     ");
    }
    x++;
    if (olsr_cnf->ip_version == AF_INET)
      fprintf(handle, " %-3i ", (u_char) msg[i]);
    else
      fprintf(handle, " %-2x ", (u_char) msg[i]);
  }
  fprintf(handle, "\n");
}

static void
print_hellomsg(FILE * handle, uint8_t msgtype, uint8_t * data, int16_t totsize)
{
  union olsr_ip_addr *haddr;
  int hellosize = totsize - ((olsr_cnf->ip_version == AF_INET) ? OLSR_MSGHDRSZ_IPV4 : OLSR_MSGHDRSZ_IPV6);
  struct pkt_lq_hello_header *h = (struct pkt_lq_hello_header *)data;
  struct pkt_lq_hello_info_header *hinf;

  fprintf(handle, "    +Htime: %u ms\n", me_to_reltime(data[2]));
  fprintf(handle, "    +Willingness: %d\n", data[3]);

  for (
    hinf = (struct pkt_lq_hello_info_header *)(h + 1); /* Start at the byte just after h */
    (char *)hinf < ((char *)data + hellosize);
    hinf = (struct pkt_lq_hello_info_header *)((char *)hinf + ntohs(hinf->size)))
  {
    fprintf(
      handle,
      "    ++ Link: %s, Status: %s, Size: %d\n",
      olsr_link_to_string(EXTRACT_LINK_TYPE(hinf->link_code)),
      olsr_status_to_string(EXTRACT_NEIGHBOR_TYPE(hinf->link_code)),
      ntohs(hinf->size));

    /* TODO: bug: haddr should be increased more if LQ packet */
    for (
      haddr = (union olsr_ip_addr *)(hinf + 1); /* Start at the byte just after hinf */
      (char *)haddr < (char *)hinf + ntohs(hinf->size);
      haddr += olsr_cnf->ip_version == AF_INET ? sizeof(haddr->v4) : sizeof(haddr->v6))
    {
      struct ipaddr_str buf;

      fprintf(handle, "    ++ %s\n", olsr_ip_to_string(&buf, haddr));

      /* TODO: the quality data in the HELLO message may differ per link quality plug-in */
      if (msgtype != HELLO_MESSAGE) /* ahummmm */
      {
        uint8_t *quality = (uint8_t *) haddr + olsr_cnf->ipsize;
        fprintf(handle, "    ++ LQ = %d, RLQ = %d\n", quality[0], quality[1]);
        haddr += 4; /* Ouch, very very ugly */
      }
    }
  }
}

static void
print_olsr_tcmsg(FILE * handle, uint8_t msgtype, uint8_t * data, int16_t totsize)
{
  int remsize = totsize - ((olsr_cnf->ip_version == AF_INET) ? OLSR_MSGHDRSZ_IPV4 : OLSR_MSGHDRSZ_IPV6);

  fprintf(handle, "    +ANSN: %d\n", htons(((struct pkt_lq_tc_header *)data)->ansn));

  data += 4;
  remsize -= 4;

  while (remsize > 0)
  {
    struct ipaddr_str buf;
    fprintf(
      handle,
      "    +Neighbor: %s\n",
      olsr_ip_to_string(&buf, (union olsr_ip_addr *)ARM_NOWARN_ALIGN(data)));

    data += olsr_cnf->ipsize;

    /* TODO: the quality data in the TC message may differ per link quality plug-in */
    if (msgtype != TC_MESSAGE) /* ahummmm */
    {
      fprintf(handle, "    +LQ: %d, ", *data);
      data += 1;
      fprintf(handle, "RLQ: %d\n", *data);
      data += 3;
    }
    remsize -= (olsr_cnf->ipsize + 4);
  }

}

static void
print_hnamsg(FILE * handle, uint8_t * data, int16_t totsize)
{
  int remsize = totsize - ((olsr_cnf->ip_version == AF_INET) ? OLSR_MSGHDRSZ_IPV4 : OLSR_MSGHDRSZ_IPV6);

  while (remsize) {
    struct ipaddr_str buf;
    fprintf(handle, "    +Network: %s\n", olsr_ip_to_string(&buf, (union olsr_ip_addr *)ARM_NOWARN_ALIGN(data)));
    data += olsr_cnf->ipsize;
    fprintf(handle, "    +Netmask: %s\n", olsr_ip_to_string(&buf, (union olsr_ip_addr *)ARM_NOWARN_ALIGN(data)));
    data += olsr_cnf->ipsize;

    remsize -= (olsr_cnf->ipsize * 2);
  }

}

static void
print_midmsg(FILE * handle, uint8_t * data, int16_t totsize)
{
  int remsize = totsize - ((olsr_cnf->ip_version == AF_INET) ? OLSR_MSGHDRSZ_IPV4 : OLSR_MSGHDRSZ_IPV6);

  while (remsize) {
    struct ipaddr_str buf;
    fprintf(handle, "    +Alias: %s\n", olsr_ip_to_string(&buf, (union olsr_ip_addr *)ARM_NOWARN_ALIGN(data)));
    data += olsr_cnf->ipsize;
    remsize -= olsr_cnf->ipsize;
  }
}

/* Single message */
int8_t
print_olsr_serialized_message(FILE * handle, union pkt_olsr_message * msg)
{
  struct ipaddr_str buf;

  fprintf(handle, "   ------------ OLSR MESSAGE ------------\n");
  fprintf(handle, "    Sender main addr: %s\n", olsr_ip_to_string(&buf, (union olsr_ip_addr *)&msg->v4.originator));
  fprintf(handle, "    Type: %s, size: %d, vtime: %u ms\n", olsr_msgtype_to_string(msg->v4.msgtype),
          ntohs(msg->v4.msgsize), me_to_reltime(msg->v4.vtime));
  fprintf(handle, "    TTL: %d, Hopcnt: %d, seqno: %d\n", (olsr_cnf->ip_version == AF_INET) ? msg->v4.ttl : msg->v6.ttl,
          (olsr_cnf->ip_version == AF_INET) ? msg->v4.hopcnt : msg->v6.hopcnt,
          ntohs((olsr_cnf->ip_version == AF_INET) ? msg->v4.seqno : msg->v6.seqno));

  switch (msg->v4.msgtype) {
    /* Print functions for individual messagetypes */
  case (MID_MESSAGE):
    print_midmsg(
      handle,
      (olsr_cnf->ip_version == AF_INET) ? (uint8_t *) & msg->v4.message : (uint8_t *) & msg->v6.message,
      ntohs(msg->v4.msgsize));
    break;
  case (HNA_MESSAGE):
    print_hnamsg(
      handle,
      (olsr_cnf->ip_version == AF_INET) ? (uint8_t *) & msg->v4.message : (uint8_t *) & msg->v6.message,
      ntohs(msg->v4.msgsize));
    break;
  case (TC_MESSAGE):
  case (LQ_ETX_TC_MESSAGE):
  case (LQ_ETT_TC_MESSAGE):
  case (LQ_ETXETH_TC_MESSAGE):
    print_olsr_tcmsg(
      handle,
      msg->v4.msgtype,
      (olsr_cnf->ip_version == AF_INET) ? (uint8_t *) & msg->v4.message : (uint8_t *) & msg->v6.message,
      ntohs(msg->v4.msgsize));
    break;
  case (HELLO_MESSAGE):
  case (LQ_ETX_HELLO_MESSAGE):
  case (LQ_ETT_HELLO_MESSAGE):
  case (LQ_ETXETH_HELLO_MESSAGE):
    print_hellomsg(
      handle,
      msg->v4.msgtype,
      (olsr_cnf->ip_version == AF_INET) ? (uint8_t *) & msg->v4.message : (uint8_t *) & msg->v6.message,
      ntohs(msg->v4.msgsize));
    break;
  default:
    print_messagedump(handle, (uint8_t *) msg, ntohs(msg->v4.msgsize));
  }

  fprintf(handle, "   --------------------------------------\n\n");
  return 1;
}

/* Entire packet */
int8_t
print_olsr_serialized_packet(FILE * handle, union pkt_olsr_packet *pkt, uint16_t size, union olsr_ip_addr *from_addr)
{
  int16_t remainsize = size - OLSR_HEADERSIZE;
  union pkt_olsr_message *msg;
  struct ipaddr_str buf;

  /* Print packet header (no IP4/6 difference) */
  fprintf(handle, "  ============== OLSR PACKET ==============\n   source: %s\n   length: %d bytes\n   seqno: %d\n\n",
          from_addr ? olsr_ip_to_string(&buf, from_addr) : "UNKNOWN", ntohs(pkt->v4.packlen), ntohs(pkt->v4.seqno));

  /* Check size */
  if (size != ntohs(pkt->v4.packlen))
    fprintf(handle, "   SIZE MISSMATCH(%d != %d)!\n", size, ntohs(pkt->v4.packlen));

  msg = (union pkt_olsr_message *)pkt->v4.msg;

  /* Print all messages */
  while ((remainsize > 0) && ntohs(msg->v4.msgsize)) {
    print_olsr_serialized_message(handle, msg);
    remainsize -= ntohs(msg->v4.msgsize);
    msg = (union pkt_olsr_message *)((char *)msg + ntohs(msg->v4.msgsize));
  }

  /* Done */
  fprintf(handle, "  =========================================\n\n");
  return 1;
}

/*
 * Local Variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
