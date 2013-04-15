/*
 * OLSR Link Probe plugin.
 * Copyright (c) 2008 Erik Tromp.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY 
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE 
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED 
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* -------------------------------------------------------------------------
 * File       : Probe.c
 * Description: Probing functions and interpretation of probe results
 *
 * ------------------------------------------------------------------------- */

#define _MULTI_THREADED

/* System includes */
#include <stdio.h> /* vsnprintf() */
#include <stdlib.h> /* abs() */
#include <stdarg.h> /* va_list, va_start, va_end */
#include <string.h> /* strerror() */
#include <errno.h> /* errno */
#include <pthread.h> /* pthread_t, pthread_create() */
#include <signal.h> /* sigset_t, sigfillset(), sigdelset(), SIGINT */
#include <sys/time.h> /* struct timezone */
#include <arpa/inet.h> /* inet_ntoa() */
#include <unistd.h> /* sleep(3) */

/* OLSRD includes */
#include "olsr.h" /* olsr_printf() */
#include "link_set.h" /* get_link_set() */
//#include "lq_route.h" /* MIN_LINK_QUALITY */
#include "ipcalc.h" /* ipequal */
#include "scheduler.h" /* olsr_mutex */
#include "lq_plugin_ett.h" /* struct link_lq_ett */

/* Plugin includes */
#include "NetworkInterfaces.h"
#include "Probe.h"
#include "Link.h" /* struct TProbedLink */
#include "Packet.h" /* SMALL_PROBE_BIT_SIZE */

static pthread_t RxThread;
static int RxThreadRunning = 0;

#define BUFFER_SIZE 2048
static unsigned char ProbePacketBuffer[BUFFER_SIZE];

void ProbeLink(struct link_entry* link_entry, struct TProbedInterface* intf);

/* -------------------------------------------------------------------------
 * Function   : LinkProbePError
 * Description: Prints an error message at OLSR debug level 1.
 *              First the plug-in name is printed. Then (if format is not NULL
 *              and *format is not empty) the arguments are printed, followed
 *              by a colon and a blank. Then the message and a new-line.
 * Input      : format, arguments
 * Output     : none
 * Return     : none
 * Data Used  : errno
 * ------------------------------------------------------------------------- */
void LinkProbePError(const char* format, ...)
{
#define MAX_STR_DESC 255
  char* strErr = strerror(errno);
  char strDesc[MAX_STR_DESC];

  /* Rely on short-circuit boolean evaluation */
  if (format == NULL || *format == '\0')
  {
    olsr_printf(1, "%s: %s\n", PLUGIN_NAME, strErr);
  }
  else
  {
    va_list arglist;

    olsr_printf(1, "%s: ", PLUGIN_NAME);

    va_start(arglist, format);
    vsnprintf(strDesc, MAX_STR_DESC, format, arglist);
    va_end(arglist);

    /* Ensure null-termination also with Windows C libraries */
    strDesc[MAX_STR_DESC - 1] = '\0';

    olsr_printf(1, "%s: %s\n", strDesc, strErr);
  }
} /* LinkProbePError */

/* -------------------------------------------------------------------------
 * Function   : tvsub
 * Description: Subtract 2 timeval structs: out = out - in
 * Input      : out, in
 * Output     : out
 * Return     : none
 * Data Used  : none
 * Notes      : Out is assumed to be >= in
 * ------------------------------------------------------------------------- */
static void tvsub(struct timeval* out, struct timeval* in)
{
  if ((out->tv_usec -= in->tv_usec) < 0)
  {
    out->tv_sec--;
    out->tv_usec += 1000000;
  }
  out->tv_sec -= in->tv_sec;
} /* tvsub */

/* -------------------------------------------------------------------------
 * Function   : StickyRoundUpwardsMediumSpeed
 * Description: Rounds a medium speed to one of the values valid for the
 *              network interface type
 * Input      : currentRoundedSpeed (Mbits/sec)
 *              estimatedSpeed (Mbits/sec)
 * Output     : none
 * Return     : The rounded medium speed (Mbits/sec)
 * Data Used  : none
 * ------------------------------------------------------------------------- */
static int StickyRoundUpwardsMediumSpeed(
  int currentRoundedSpeed,
  float estimatedSpeed)
{
/* Note:
 * We do not actually detect the 802.11 data rate, but the (UDP)
 * throughput, which may be a lot lower. The list of values below is not a
 * list of 802.11 data rates, but merely a list of sensible values (in Mbits/sec)
 * that we stick to when determining the UDP throughput. */
#define N_MEDIUM_SPEEDS 10
  static int MediumSpeeds[N_MEDIUM_SPEEDS] = 
    {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000};

  int i;
  for (i = 0; i < N_MEDIUM_SPEEDS; i++)
  {
    /* Keep a band of 20%, so that an estimated speed of e.g. 11.5 Mbits/sec
     * would lead to a rounded speed of 10 Mbits/sec, not 20 Mbits/sec */
    if ((float)estimatedSpeed <= 1.2 * MediumSpeeds[i])
    {
      /* Step upwards directly */
      if (MediumSpeeds[i] >= currentRoundedSpeed)
      {
        return MediumSpeeds[i];
      }

      /* Step downwards only if 5% under speed */
      if ((float)estimatedSpeed <= 0.95 * MediumSpeeds[i])
      {
        return MediumSpeeds[i];
      }

      return currentRoundedSpeed;
    }
  }

  return -1;
} /* StickyRoundUpwardsMediumSpeed */

/* -------------------------------------------------------------------------
 * Function   : ProbeTypeStr
 * Description: Convert a probe type value to its corresponding string
 * Input      : probeType
 * Output     : none
 * Return     : The output string
 * Data Used  : none
 * ------------------------------------------------------------------------- */
static const char* ProbeTypeStr(int probeType)
{
  static const char* types[PtNTypes] =
  {
    "UnicastSmall",
    "UnicastLarge",
  };
  return types[probeType];
} /* ProbeTypeStr */

/* -------------------------------------------------------------------------
 * Function   : UpdateEstimations
 * Description: Update the estimations for the medium type and speed for a
 *              link
 * Input      : link_entry - the link object to update
 *              intf - the network interface on which the link exists
 * Output     : none
 * Return     : none
 * Data Used  : none
 * ------------------------------------------------------------------------- */
static void UpdateEstimations(struct link_entry* link_entry, struct TProbedInterface* intf)
{
  struct link_lq_ett* lq_data = (struct link_lq_ett*) link_entry->linkquality;
  struct TProbedLink* plink = (struct TProbedLink*) lq_data->link_cost_data;
  int probeType,j;
  float avgTripTimes[PtNTypes] = {0.0, 0.0}; /* microsec */
  int nAvg;
  float minTripTimes[PtNTypes] = {1000000.0, 1000000.0}; /* microsec */
  float min2TripTimes[PtNTypes] = {1000000.0, 1000000.0}; /* microsec */
  float maxTripTimes[PtNTypes] = {0.0, 0.0}; /* microsec */
  float max2TripTimes[PtNTypes] = {0.0, 0.0}; /* microsec */
  int minIndex = -1; /* position at which the smallest trip time value is found */
  int min2Index = -1; /* position at which the one-but smallest trip time value is found */
  int maxIndex = -1; /* position at which the largest trip time value is found */
  int max2Index = -1; /* position at which the one-but largest trip time value is found */
  float* tripTimesUsed; /* pointer to either avgTripTimes[] or minTripTimes[] */
  float pktPairDiffTime; /* Difference in trip time between large and small packets, in microseconds */
  float estimatedUnicastSpeed;
#ifndef NODEBUG
  struct ipaddr_str buf;
#endif

  /* Continue only if enough measurements have been recorded */
  if (plink->logSize < PROBE_HISTORY_SIZE)
  {
    return;
  } /* if */

  olsr_printf(
    7,
    "%s: probe log for %s on \"%s\":\n",
    PLUGIN_NAME_SHORT,
    olsr_ip_to_string(&buf, &link_entry->neighbor_iface_addr),
    link_entry->if_name);

  for (probeType = 0; probeType < PtNTypes; probeType++)
  {
    /* Quick-access pointer */
    int* currTripTimes = plink->tripTimes[probeType];

    olsr_printf(
      8,
      "%s: single trip times (ms) for %-16s:",
      PLUGIN_NAME_SHORT,
      ProbeTypeStr(probeType));

    for (j = 0; j < PROBE_HISTORY_SIZE; j++)
    {
      olsr_printf(8, " %4d", currTripTimes[j]);
    }
    olsr_printf(8, "\n");

    /* Find extremes: smallest, one-but smallest, one-but largest and largest value */
    for (j = 0; j < PROBE_HISTORY_SIZE; j++)
    {
      if (currTripTimes[j] < minTripTimes[probeType])
      {
        minTripTimes[probeType] = currTripTimes[j];
        minIndex = j;
      }
      if (currTripTimes[j] > maxTripTimes[probeType])
      {
        maxTripTimes[probeType] = currTripTimes[j];
        maxIndex = j;
      }
    }
    for (j = 0; j < PROBE_HISTORY_SIZE; j++)
    {
      if (j != minIndex && currTripTimes[j] < min2TripTimes[probeType])
      {
        min2TripTimes[probeType] = currTripTimes[j];
        min2Index = j;
      }
      if (j != maxIndex && currTripTimes[j] > max2TripTimes[probeType])
      {
        max2TripTimes[probeType] = currTripTimes[j];
        max2Index = j;
      }
    }

    /* Calculate average */
    nAvg = 0;
    for (j = 0; j < PROBE_HISTORY_SIZE; j++)
    {
      avgTripTimes[probeType] += currTripTimes[j];
      nAvg++;
    }

    /* Don't take into account the extremes as found above */
    avgTripTimes[probeType] -=
      (min2TripTimes[probeType] +
       minTripTimes[probeType] +
       max2TripTimes[probeType] +
       maxTripTimes[probeType]);
    nAvg -= 4;

    avgTripTimes[probeType] /= nAvg;

    olsr_printf(
      7,
      "%s: <<%5.1f, <%5.1f, [%5.1f], >%5.1f, >>%5.1f, #TX: %d, #RX: %d (%d%%)\n",
      PLUGIN_NAME_SHORT,
      minTripTimes[probeType],
      min2TripTimes[probeType],
      avgTripTimes[probeType],
      max2TripTimes[probeType],
      maxTripTimes[probeType],
      plink->nSentProbes[probeType],
      plink->nReceivedReplies[probeType],
      plink->nReceivedReplies[probeType] * 100 / plink->nSentProbes[probeType]);
  } /* for */

  /* Using average in stead of minimum seems to lead to more stable estimations */
  /*tripTimesUsed = minTripTimes;*/
  tripTimesUsed = avgTripTimes;

  /* Estimate (unicast) medium speed. Clip at MAX_ESTIMATED_MEDIUM_SPEED (1 GBit/sec) */
  pktPairDiffTime = tripTimesUsed[PtUnicastLarge] - tripTimesUsed[PtUnicastSmall];
  if (pktPairDiffTime < 0.0)
  {
    /* Don't change existing estimation */
    estimatedUnicastSpeed = plink->estimatedLinkSpeed;
  }
  else if (pktPairDiffTime < intf->packetSizeDiffBits / MAX_ESTIMATED_MEDIUM_SPEED)
  {
    estimatedUnicastSpeed = MAX_ESTIMATED_MEDIUM_SPEED;
  }
  else /* pktPairDiffTime >= intf->packetSizeDiffBits / MAX_ESTIMATED_MEDIUM_SPEED */
  {
    estimatedUnicastSpeed = intf->packetSizeDiffBits / pktPairDiffTime;
  }

  olsr_printf(
    6,
    "%s: probe results for %s on \"%s\": raw unicast speed = %0.2fMbit/s --> rounded speed = %dMbit/s\n",
    PLUGIN_NAME_SHORT,
    olsr_ip_to_string(&buf, &link_entry->neighbor_iface_addr),
    link_entry->if_name,
    estimatedUnicastSpeed,
    StickyRoundUpwardsMediumSpeed(
      plink->roundedLinkSpeed,
      estimatedUnicastSpeed));

  /* Copy the values as estimated by using the averages */
  plink->estimatedLinkSpeed = estimatedUnicastSpeed;

  plink->roundedLinkSpeed =
    StickyRoundUpwardsMediumSpeed(
      plink->roundedLinkSpeed,
      estimatedUnicastSpeed);

  /* Assumption: olsr_mutex has been grabbed for safe access to OLSR data */
  lq_data->link_speed = plink->roundedLinkSpeed;

} /* UpdateEstimations */

/* -------------------------------------------------------------------------
 * Function   : ProbePacketReceived
 * Description: Handle a received probe packet
 * Input      : intf - the network interface on which the packet was received
 *              from - the IP address of the source node
 *              probe - the probe packet
 * Output     : none
 * Return     : none
 * Data Used  : none
 * ------------------------------------------------------------------------- */
static void ProbePacketReceived(
  struct TProbedInterface* intf, 
  struct sockaddr_in* from,
  struct TProbePacket* probe)
{
  struct link_entry* link_entry;
  struct TProbedLink* plink;

  switch (probe->phase)
  {
    case(PT_PROBE_REPLY):
    {
      /* Received a reply to one of our probe request packets */

      struct timeval tv;
      struct timezone tz; /* dummy */
      struct timeval* tp;
      int roundTripTime;
      int currIndex;

      plink = NULL;

      olsr_printf(
        9,
        "%s: Received PT_PROBE_REPLY packet for %d byte probe on \"%s\" from %s\n",
        PLUGIN_NAME_SHORT,
        probe->sentSize,
        intf->olsrIntf->interf->int_name,
        inet_ntoa(from->sin_addr));

      /* Lookup the probed link object belonging to this probe reply packet. */
      /* Assumption: olsr_mutex has been grabbed for safe access to OLSR data */
      OLSR_FOR_ALL_LINK_ENTRIES(link_entry)
      {
        if (probe->daddr == link_entry->neighbor_iface_addr.v4.s_addr)
        {
          struct link_lq_ett* lq_data = (struct link_lq_ett*) link_entry->linkquality;
          plink = (struct TProbedLink*) lq_data->link_cost_data;
          break; /* for */
        }
      }
      OLSR_FOR_ALL_LINK_ENTRIES_END(link_entry);

      /* No link for received reply packet? */
      if (plink == NULL)
      {
        olsr_printf(
          1,
          "%s on \"%s\": Unexpected reply from %s. Ignoring.\n",
          PLUGIN_NAME,
          intf->olsrIntf->interf->int_name,
          inet_ntoa(from->sin_addr));

        return;
      }

      /* Check if we have received the expected reply */
      if (plink->probeId != probe->id || plink->probeType != probe->type)
      {
        /* Apparently a probe or its reply was lost, or the reply was still on its
         * way while we sent the next probe packet. Try again. */

        olsr_printf(
          1,
          "%s on \"%s\": Wrong id/type in reply: expecting (%d/%s) but got (%d/%s) from %s. Ignoring.\n",
          PLUGIN_NAME,
          intf->olsrIntf->interf->int_name,
          plink->probeId,
          ProbeTypeStr(plink->probeType),
          probe->id,
          ProbeTypeStr(probe->type),
          inet_ntoa(from->sin_addr));

        /* Go back to type 'PtUnicastSmall' */
        plink->probeType = PtUnicastSmall;

        return;
      }

      /* Calculate round trip time */
      gettimeofday(&tv, &tz);
      tp = (struct timeval*)&probe->sentAt;
      tvsub(&tv, tp);
      roundTripTime = tv.tv_sec * 1000000 + (tv.tv_usec);

      /* Ignore round trip times larger than probing interval, and
       * out-of-limit round trip times */
      if (roundTripTime > PROBING_INTERVAL_BASE * 1000000 ||
          roundTripTime < RTT_MIN ||
          roundTripTime > RTT_MAX)
      {
        olsr_printf(
          1,
          "%s on \"%s\": roundTripTime with %s out of range: %d, min = %d, max = %d. Ignoring.\n",
          PLUGIN_NAME,
          intf->olsrIntf->interf->int_name,
          inet_ntoa(from->sin_addr),
          roundTripTime,
          RTT_MIN,
          RTT_MAX < PROBING_INTERVAL_BASE * 1000000 ? RTT_MAX : (int)(PROBING_INTERVAL_BASE * 1000000));

        /* Go back to type 'PtUnicastSmall' */
        plink->probeType = PtUnicastSmall;

        return;
      }

      olsr_printf(
        9,
        "%s: Round Trip Time = %d\n",
        PLUGIN_NAME_SHORT,
        roundTripTime);

      plink->nReceivedReplies[plink->probeType]++;

      currIndex = plink->currIndex;

      /* Log single trip time value */
      if (plink->probeType == PtUnicastSmall)
      {
        /* For the small probe packets, the reply is the same size, so the single
         * trip time is half the round trip time (assuming a symmetric link...) */
        plink->tripTimes[plink->probeType][currIndex] = roundTripTime / 2;

        olsr_printf(
          8,
          "%s: Short probe packet single trip time = %d\n",
          PLUGIN_NAME_SHORT,
          plink->tripTimes[plink->probeType][currIndex]);
      }
      else
      {
        /* For the large probe packets, subtract the time needed for the
         * reply packet, which is the same size as a small probe packet */
        plink->tripTimes[plink->probeType][currIndex] =
          roundTripTime - plink->tripTimes[plink->probeType - 1][currIndex];

        olsr_printf(
          8,
          "%s: Long probe packet single trip time = %d\n",
          PLUGIN_NAME_SHORT,
          plink->tripTimes[plink->probeType][currIndex]);
      }

      /* Ignore packet pairs with the large packet having a smaller trip time than the
       * small packet */
      if (
           plink->probeType == PtUnicastLarge &&
           plink->tripTimes[PtUnicastLarge][currIndex] < plink->tripTimes[PtUnicastSmall][currIndex]
         )
      {
        olsr_printf(
          8,
          "%s on \"%s\": tripTime with %s of large packet (%d) is smaller than that of small packet (%d). Ignoring.\n",
          PLUGIN_NAME_SHORT,
          intf->olsrIntf->interf->int_name,
          inet_ntoa(from->sin_addr),
          plink->tripTimes[PtUnicastLarge][currIndex],
          plink->tripTimes[PtUnicastSmall][currIndex]);

        /* Go back to type 'PtUnicastSmall' */
        plink->probeType = PtUnicastSmall;

        return;
      }

      /* Advance to next probe type */
      if (plink->probeType >= PtLast)
      {
        /* Roll over to first probe type */
        plink->probeType = PtFirst;

        /* Advance to next sample */
        plink->currIndex = (currIndex + 1) % PROBE_HISTORY_SIZE;
        if (plink->logSize < PROBE_HISTORY_SIZE)
        {
          plink->logSize++;
        }

        /* Update the estimations about the link speed */
        UpdateEstimations(link_entry, intf);
      }
      else
      {
        plink->probeType++;
      }

    } /* case(PT_PROBE_REPLY): */
    break;

    case(PT_PROBE_REQUEST):
    {
      /* Received a probe request from one of our neighbors */

      /* Retrieve IP address of receiving network interface */
      /* Cast down to correct sockaddr subtype */
      struct sockaddr_in* sin = (struct sockaddr_in*)&intf->olsrIntf->interf->int_addr;
      int nBytesWritten;

      plink = NULL;

      /* Is it me you're looking for? (Violins rush in...) */
      if (probe->daddr != sin->sin_addr.s_addr)
      {
        /* No */
        break; /* case */
      }

      olsr_printf(
        9,
        "%s: Received PT_PROBE_REQUEST packet of %d bytes on \"%s\" from %s\n",
        PLUGIN_NAME_SHORT,
        probe->sentSize,
        intf->olsrIntf->interf->int_name,
        inet_ntoa(from->sin_addr));

      /* Lookup the probed link object belonging to this probe request packet */
      /* Assumption: olsr_mutex has been grabbed for safe access to OLSR data. */
      OLSR_FOR_ALL_LINK_ENTRIES(link_entry)
      {
        if (probe->daddr == link_entry->local_iface_addr.v4.s_addr)
        {
          struct link_lq_ett* lq_data = (struct link_lq_ett*) link_entry->linkquality;
          plink = (struct TProbedLink*) lq_data->link_cost_data;
          break; /* for */
       	}
      }
      OLSR_FOR_ALL_LINK_ENTRIES_END(link_entry);

      /* Change the probe request into a probe reply */
      probe->phase = PT_PROBE_REPLY;

      /* No link for received reply packet? */
      if (plink != NULL)
      {
        /* Fill my idea of the speed */
        probe->myEstimatedLinkSpeed = plink->roundedLinkSpeed;
      }
      else
      {
        probe->myEstimatedLinkSpeed = (u_int32_t)UNKNOWN_MEDIUM_SPEED;
      }

      /* For debugging: artificial delay in the reply to long probe packets */
/*
      if (probe->type == PtUnicastLarge)
      {
        struct timespec replyAfter  = { 0L, 1000000L };
        nanosleep(&replyAfter, NULL);
      }
*/

      olsr_printf(
        9,
        "%s: Sending PT_PROBE_REPLY packet of %d bytes to %s\n",
        PLUGIN_NAME_SHORT,
        PROBE_MINLEN,
        inet_ntoa(from->sin_addr));

      /* Echo back the data (not the padding) of the probe packet */
      nBytesWritten = sendto(
        intf->skfd,
        probe,
        PROBE_MINLEN,
        MSG_DONTROUTE,
        (struct sockaddr*)from,
        sizeof(struct sockaddr_in));                   
      if (nBytesWritten != PROBE_MINLEN)
      {
        olsr_printf(
          1,
          "%s: sendto() error sending probe reply packet pkt to %s on \"%s\": %s\n",
          PLUGIN_NAME,
          inet_ntoa(from->sin_addr),
          intf->olsrIntf->interf->int_name,
          strerror(errno));
      } /* if (nBytesWritten...) */
    } /* case(PT_PROBE_REQUEST) */
    break;

    default:
    {
      olsr_printf(
        1,
        "%s: illegal probe packet received from %s on \"%s\". Ignoring.\n",
        PLUGIN_NAME,
        inet_ntoa(from->sin_addr),
        intf->olsrIntf->interf->int_name);
    }
    break;
  } /* switch (probe->phase) */
} /* ProbePacketReceived */

/* -------------------------------------------------------------------------
 * Function   : WaitForPacket
 * Description: Wait (blocking) for probe packets, then call the handler for each
 *              received packet
 * Input      : none
 * Output     : none
 * Return     : none
 * Data Used  : ProbedInterfaces, HighestSkfd, InputSet
 * ------------------------------------------------------------------------- */
static void WaitForPacket(void)
{
  int nFdBitsSet;
  fd_set rxFdSet;

  if (HighestSkfd < 0)
  {
    /* No interfaces to listen to. Sleep a short while then return. */
    sleep(1);
    return;
  }

  /* Make a local copy of the set of file descriptors that select() can
   * modify to indicate which descriptors actually changed status */
  rxFdSet = InputSet;

  /* Wait (blocking) for packets received on any of the sockets.
   * NOTE: don't use a timeout (last parameter). It causes a high system CPU load! */
  nFdBitsSet = select(HighestSkfd + 1, &rxFdSet, NULL, NULL, NULL/*&timeout*/);
  if (nFdBitsSet < 0)
  {
    if (errno != EINTR)
    {
      LinkProbePError("select() error");
    }
    return;
  }

  if (nFdBitsSet == 0)
  {
    /* No packets waiting. This is unexpected; normally we would excpect select(...)
     * to return only if at least one packet was received (so nFdBitsSet > 0), or
     * if this thread received a signal (so nFdBitsSet < 0). */
    return;
  }

  while (nFdBitsSet > 0)
  {
    struct TProbedInterface* intf;

    /* Guarantee safe access to OLSR data */
    // TODO
    //pthread_mutex_lock(&olsr_mutex);

    /* Check if a packet was received on the capturing socket (if any)
     * of each network interface */
    for (intf = ProbedInterfaces; intf != NULL; intf = intf->next)
    {
      int skfd = intf->skfd;
      if (FD_ISSET(skfd, &rxFdSet))
      {
        unsigned char buffer[BUFFER_SIZE];
        struct sockaddr_in from;
        socklen_t fromLen = sizeof(from);
        int nBytes;

        /* A probe packet was received */

        nFdBitsSet--;

        nBytes = recvfrom(skfd, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&from, &fromLen);
        if (nBytes < 0)
        {
          LinkProbePError("recvfrom() error on \"%s\"", intf->olsrIntf->interf->int_name);
          continue; /* for */
        }
        if ((size_t)nBytes < PROBE_MINLEN)
        {
          olsr_printf(
            1,
            "%s: probe packet too short (%d bytes) from %s\n",
            PLUGIN_NAME,
            nBytes,
            inet_ntoa(from.sin_addr));
          continue; /* for */
        }

        /* nBytes >= PROBE_MINLEN */

        ProbePacketReceived(intf, &from, (struct TProbePacket*) buffer);
      } /* if */
    } /* for */

    /* Release safe access to OLSR data */
    // TODO
    // pthread_mutex_unlock(&olsr_mutex);

  } /* while */
} /* WaitForPacket */

/* -------------------------------------------------------------------------
 * Function   : ProbeLink
 * Description: Compose a probe request packet and send it to the
 *              neighbor on a link
 * Input      : link_entry - the link over which to send the probe
 *              intf - the network interface over which to send the probe
 * Output     : none
 * Return     : none
 * Data Used  : ProbePacketBuffer
 * ------------------------------------------------------------------------- */
void ProbeLink(struct link_entry* link_entry, struct TProbedInterface* intf)
{
  struct TProbePacket* probe = (struct TProbePacket*) ProbePacketBuffer;
  struct link_lq_ett* lq_data = (struct link_lq_ett*) link_entry->linkquality;
  struct TProbedLink* plink = (struct TProbedLink*) lq_data->link_cost_data;
  int nBytesWritten;
  ssize_t len;
  struct sockaddr_in whereto;
  struct timezone tz; /* dummy */

  /* If some idiot changed the interface's MTU... */
  if (intf->mtu != intf->olsrIntf->interf->int_mtu)
  {
    enum TProbeType pt;

    intf->mtu = intf->olsrIntf->interf->int_mtu;
    intf->packetSizeDiffBits = (intf->mtu - PROBE_MINLEN) * 8;

    /* ...start all over again */
    plink->probeType = PtUnicastSmall;
    plink->probeId= 0;
    plink->currIndex = 0;
    plink->logSize = 0;
    for (pt = PtFirst; pt < PtNTypes; pt++)
    {
      plink->nSentProbes[pt] = 0;
      plink->nReceivedReplies[pt] = 0;
    }

    return;
  }

  /* Compose a request probe packet */

  probe->daddr = link_entry->neighbor_iface_addr.v4.s_addr;

  /* Compose IP destination address */
  memset(&whereto, 0, sizeof(whereto));
  whereto.sin_family = AF_INET;
  whereto.sin_port = htons(LINK_PROBE_PORT);

  /* Set IP destination address */
  whereto.sin_addr.s_addr = probe->daddr;

  /* Record current time */
  if (gettimeofday(&probe->sentAt, &tz) < 0)
  {
    LinkProbePError("gettimeofday() error");
    return;
  }

  probe->phase = PT_PROBE_REQUEST;


/* Normally the large probe packet is sent directly after the small probe
 * packet, as is normally done in PktPair measurements. Lab tests showed that,
 * somehow, this way of probing does not work: it seems that both probe packets
 * are transmitted in the same single time slot. This results in similar round
 * trip time measurements for both probe packets, preventing a usefull estimation
 * of the medium speed.
 * If instead the small probe packet is sent separately from the large probe packet,
 * the round trip times are much more distinct, and can be used to estimate the
 * medium speed.
 */

  /* Calculate probe packet size: depends on probe type */
  len = PROBE_MINLEN;
  if (plink->probeType == PtUnicastLarge)
  {
    len = intf->mtu;
  }

  plink->probeId++;

  /* Put additional data into the probe packet */
  probe->type = plink->probeType;
  probe->id = plink->probeId;
  probe->sentSize = len;
  probe->myEstimatedLinkSpeed = plink->roundedLinkSpeed;

  olsr_printf(
    9,
    "%s: Sending PT_PROBE_REQUEST packet of %d bytes to %s on \"%s\"\n",
    PLUGIN_NAME_SHORT,
    len,
    inet_ntoa(whereto.sin_addr),
    intf->olsrIntf->interf->int_name);
    
  /* Send probe packet */
  nBytesWritten = sendto(
    intf->skfd,
    ProbePacketBuffer,
    len,
    MSG_DONTROUTE,
    (struct sockaddr*) &whereto,
    sizeof(whereto));                   
  if (nBytesWritten != len)
  {
    olsr_printf(
      1,
      "%s: sendto() error sending %d-byte probe request packet pkt to %s on \"%s\": %s\n",
      PLUGIN_NAME,
      len,
      inet_ntoa(whereto.sin_addr),
      intf->olsrIntf->interf->int_name,
      strerror(errno));
  }
  else
  {
    /* Keep track of how many probe packets were successfully sent */
    plink->nSentProbes[plink->probeType]++;
  }
} /* ProbeNeighbor */

/* -------------------------------------------------------------------------
 * Function   : ProbeAllLinks
 * Description: For each available link, check if it is time to send
 *              a probe request packet and, if so, send one
 * Input      : useless - not used
 * Output     : none
 * Return     : none
 * Data Used  : ProbePacketBuffer, ProbedInterfaces
 * ------------------------------------------------------------------------- */
void ProbeAllLinks(void* useless __attribute__((unused)))
{
  int i;
  struct TProbedInterface* intf;

  /* Fill probe packet with uncompressable (random) data */
  for (i = PROBE_MINLEN; i < BUFFER_SIZE; i++)
  {
    ProbePacketBuffer[i] = rand() & 0xFF;
  }

  /* Loop through all the network interfaces */
  for (intf = ProbedInterfaces; intf != NULL; intf = intf->next)
  {
    struct link_entry* link_entry;

    /* For each link, check if it is time to send out a probe packet. */
    /* Assumption: olsr_mutex has been grabbed for safe access to OLSR data */
    OLSR_FOR_ALL_LINK_ENTRIES(link_entry)
    {
      struct link_lq_ett* lq_data;
      struct TProbedLink* plink;

      /* Consider only links from the current interface */
      if (! ipequal(&intf->olsrIntf->interf->ip_addr, &link_entry->local_iface_addr))
      {
        continue; /* for */
      }

      lq_data = (struct link_lq_ett*) link_entry->linkquality;
      plink = (struct TProbedLink*) lq_data->link_cost_data;

      /* Countdown couter reached 0 for this link? */
      plink->nextProbeAt--;
      if (plink->nextProbeAt > 0)
      {
        /* No: continue with next link */
        continue; /* for */
      }

      /* Yes: probe this link now */

      /* Re-initialize countdown couter. */
      plink->nextProbeAt = N_TIME_SLOTS;

      ProbeLink(link_entry, intf);
    }
    OLSR_FOR_ALL_LINK_ENTRIES_END(link_entry);
  } /* for */
} /* ProbeAllLinks */

/* -------------------------------------------------------------------------
 * Function   : SignalHandler
 * Description: Signal handler function
 * Input      : signo - signal being handled
 * Output     : none
 * Return     : none
 * Data Used  : RxThreadRunning
 * ------------------------------------------------------------------------- */
static void SignalHandler(int signo __attribute__((unused)))
{
  RxThreadRunning = 0;
} /* SignalHandler */

/* -------------------------------------------------------------------------
 * Function   : RunLinkProbe
 * Description: Receiver thread entry function
 * Input      : useless - not used
 * Output     : none
 * Return     : none
 * Data Used  : RxThreadRunning
 * Notes      : Another thread can gracefully stop this thread by sending
 *              a SIGUSR1 signal.
 * ------------------------------------------------------------------------- */
static void* RunLinkProbe(void* useless __attribute__((unused)))
{
  /* Mask all signals except SIGUSR1 */
  sigset_t blockedSigs;

  sigfillset(&blockedSigs);
  sigdelset(&blockedSigs, SIGUSR1);
  if (pthread_sigmask(SIG_BLOCK, &blockedSigs, NULL) < 0)
  {
    LinkProbePError("pthread_sigmask() error");
  }

  /* Set up the signal handler for the process: use SIGUSR1 to terminate
   * the link probe receive thread. Only if a signal handler is specified, does
   * a blocking system call return with errno set to EINTR; if a signal hander
   * is not specified, any system call in which the thread may be waiting will
   * not return. Note that the probe packet receive thread is usually blocked in
   * the select() function (see WaitForPacket()). */
  if (signal(SIGUSR1, SignalHandler) == SIG_ERR)
  {
    LinkProbePError("signal() error");
  }

  /* Call the thread function until flagged to exit */
  while (RxThreadRunning != 0)
  {
    WaitForPacket();
  }

  return NULL;
} /* RunLinkProbe */

/* -------------------------------------------------------------------------
 * Function   : InterfaceChange
 * Description: Callback function passed to OLSRD for it to call whenever a
 *              network interface has been added, removed or updated.
 * Input      : interf - the network interface to deal with
 *              action - indicates if the specified network interface was
 *                added, removed or updated.
 * Output     : none
 * Return     : always 0
 * Data Used  : none
 * ------------------------------------------------------------------------- */
void InterfaceChange(
  int if_index __attribute__((unused)),
  struct network_interface* interf,
  enum olsr_ifchg_flag action)
{
  if (interf == NULL) {
    return;
  }

  switch (action)
  {
  case (IFCHG_IF_ADD):
    AddInterface(interf);
    olsr_printf(1, "%s: interface %s added\n", PLUGIN_NAME, interf->int_name);
    break;

  case (IFCHG_IF_REMOVE):
    CloseLinkProbePlugin();
    InitLinkProbePlugin(interf);
    olsr_printf(1, "%s: interface %s removed\n", PLUGIN_NAME, interf->int_name);
    break;

  case (IFCHG_IF_UPDATE):
    /* Nothing to do here */
    olsr_printf(1, "%s: interface %s updated\n", PLUGIN_NAME, interf->int_name);
    break;
      
  default:
    break;
  }
} /* InterfaceChange */

/* -------------------------------------------------------------------------
 * Function   : LinkChange
 * Description: Callback function passed to OLSRD for it to call whenever a
 *              link to a neighbor node has been added or removed.
 * Input      : link - the link to deal with
 *              action - indicates if the specified network interface was
 *                added or removed.
 * Output     : none
 * Return     : always 0
 * Data Used  : none
 * ------------------------------------------------------------------------- */
int LinkChange(struct link_entry* link_entry, int action)
{
#ifndef NODEBUG
  struct ipaddr_str buf;
#endif

  if (IsNonProbedInterface(link_entry->if_name))
  {
    return 0;
  }

  switch (action)
  {
  case (LNKCHG_LNK_ADD):
    CreateProbedLink(link_entry);
    olsr_printf(
      1,
      "%s: link to %s added\n",
      PLUGIN_NAME,
      olsr_ip_to_string(&buf, &link_entry->neighbor_iface_addr));
    break;

  case (LNKCHG_LNK_REMOVE):
    RemoveProbedLink(link_entry);
    olsr_printf(
      1,
      "%s: link to %s removed\n",
      PLUGIN_NAME,
      olsr_ip_to_string(&buf, &link_entry->neighbor_iface_addr));
    break;

  default:
    break;
  } /* switch */

  return 0;
} /* InterfaceChange */

/* -------------------------------------------------------------------------
 * Function   : InitLinkProbePlugin
 * Description: Initialize the Link Probe plugin
 * Input      : skipThisIntf - specifies which network interface should not
 *              be probed. Pass NULL if not applicable.
 * Output     : none
 * Return     : fail (0) or success (1)
 * Data Used  : ProbePacketBuffer, RxThreadRunning, RxThread
 * ------------------------------------------------------------------------- */
int InitLinkProbePlugin(struct network_interface* skipThisIntf)
{
  /* Initially, fill probe packet buffer with dummy data */
  memset(ProbePacketBuffer, 'P', BUFFER_SIZE);

  if (CreateInterfaces(skipThisIntf) < 0)
  {
    olsr_printf(1, "%s: Could not initialize any network interface!\n", PLUGIN_NAME);
    /* Continue anyway; maybe an interface will be added later */
  }

  /* Start running the probe packet receiver thread */
  RxThreadRunning = 1;
  pthread_create(&RxThread, NULL, RunLinkProbe, NULL);
/*
  int policy;
  struct sched_param param;
  if (pthread_getschedparam(RxThread, &policy, &param) < 0)
  {
    fprintf(stderr, PLUGIN_NAME ": pthread_getschedparam failed!\n");
    return 0;
  }
*/

  /* set the priority; others are unchanged */
/*
  policy = SCHED_FIFO;
  param.sched_priority = sched_get_priority_max(policy);
*/

  /* set the new scheduling param */
/*
  if (pthread_setschedparam (RxThread, policy, &param) < 0)
  {
    fprintf(stderr, PLUGIN_NAME ": pthread_setschedparam failed!\n");
    return 0;
  }
*/

  return 1;
} /* InitLinkProbePlugin */

/* -------------------------------------------------------------------------
 * Function   : CloseLinkProbePlugin
 * Description: Close the Link Probe plugin and clean up
 * Input      : none
 * Output     : none
 * Return     : none
 * Data Used  : RxThread
 * ------------------------------------------------------------------------- */
void CloseLinkProbePlugin(void)
{
  /* Signal receive-thread to exit */
  if (pthread_kill(RxThread, SIGUSR1) < 0)
  /* Strangely enough, all running threads receive the SIGUSR1 signal. But only the
   * link probe thread is affected by this signal, having specified a handler for this
   * signal in its thread entry function RunLinkProbe(...). */
  {
    LinkProbePError("pthread_kill() error");
    /* Try to continue anyway */
  }

  /* Wait for RxThread to acknowledge */
  if (pthread_join(RxThread, NULL) < 0)
  {
    LinkProbePError("pthread_join() error");
    /* Try to continue anyway */
  }

  /* Time to clean up */
  CloseInterfaces();
} /* CloseLinkProbePlugin */

