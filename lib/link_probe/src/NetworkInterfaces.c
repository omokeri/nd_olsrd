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
 * File       : NetworkInterfaces.c
 * Description: Functions to open and close network interfaces
 *
 * ------------------------------------------------------------------------- */

/* Plugin includes */
#include "NetworkInterfaces.h"
#include "Probe.h"
#include "Packet.h" /* PROBE_MINLEN */

/* System includes */
#include <assert.h> /* assert() */
#include <fcntl.h> /* fcntl() */
#include <unistd.h> /* close() */
#include <string.h> /* strerror() */
#include <errno.h> /* errno */
#include <net/if.h> /* socket(), if_nametoindex() */
#include <netinet/in.h> /* struct sockaddr_in */
#include <netinet/ip.h> /* IPTOS_TOS(), IPTOS_PREC() */
#include <stdlib.h> /* free() */

/* OLSRD includes */
#include "olsr.h" /* olsr_printf() */
#include "defs.h" /* olsr_cnf */
#include "log.h" /* olsr_syslog() */

struct TProbedInterface* ProbedInterfaces = NULL;

/* Highest-numbered open socket file descriptor. To be used as first
 * parameter in calls to select(...). */
int HighestSkfd = -1;

/* Set of socket file descriptors */
fd_set InputSet;

int NextInterfaceTimeSlot = 0;

/* -------------------------------------------------------------------------
 * Function   : AddDescriptorToInputSet
 * Description: Add a socket descriptor to the global set of socket file descriptors
 * Input      : skfd - socket file descriptor
 * Output     : none
 * Return     : none
 * Data Used  : HighestSkfd, InputSet
 * Notes      : Keeps track of the highest-numbered descriptor
 * ------------------------------------------------------------------------- */
static void AddDescriptorToInputSet(int skfd)
{
  /* Keep the highest-numbered descriptor */
  if (skfd > HighestSkfd)
  {
    HighestSkfd = skfd;
  }

  /* Add descriptor to input set */
  FD_SET(skfd, &InputSet);
} /* AddDescriptorToInputSet */

/* -------------------------------------------------------------------------
 * Function   : CreateSocket
 * Description: Create a socket for sending and receiving probe packets
 * Input      : ifname - network interface (e.g. "eth0")
 * Output     : none
 * Return     : the socket descriptor ( >= 0), or -1 if an error occurred
 * Data Used  : none
 * Notes      : The socket is an UDP (datagram) over IP socket, bound to the
 *              specified network interface
 * ------------------------------------------------------------------------- */
static int CreateSocket(const char* ifName)
{
  int on = 1;
  struct sockaddr_in sin;
  int precedence;
  int tos_bits;

  /* Open UDP-IP socket */
  int skfd = socket(PF_INET, SOCK_DGRAM, 0);
  if (skfd < 0)
  {
    LinkProbePError("socket(PF_INET) error");
    return -1;
  }

  /* Enable sending to broadcast addresses */
  if (setsockopt(skfd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) < 0)
  {
    LinkProbePError("socket(SO_BROADCAST) error");
    close(skfd);
    return -1;
  }
	
  /* Bind to the specific network interfaces indicated by ifIndex. */
  /* When using Kernel 2.6 this must happer prior to the port binding! */
  if (setsockopt(skfd, SOL_SOCKET, SO_BINDTODEVICE, ifName, strlen(ifName) + 1) < 0)
  {
    LinkProbePError("socket(SO_BINDTODEVICE) error");
    close(skfd);
    return -1;
  }

  /* Bind to port */
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port = htons(LINK_PROBE_PORT);
  sin.sin_addr.s_addr = htonl(INADDR_ANY);
      
  if (bind(skfd, (struct sockaddr*)&sin, sizeof(sin)) < 0) 
  {
    LinkProbePError("bind() error");
    close(skfd);
    return -1;
  }

  /* Set socket to blocking operation */
  if (fcntl(skfd, F_SETFL, fcntl(skfd, F_GETFL, 0) & ~O_NONBLOCK) < 0)
  {
    LinkProbePError("fcntl() error");
    close(skfd);
    return -1;
  }

  /* Set IP priority and Type of Service (TOS) */
  precedence = IPTOS_PREC(olsr_cnf->tos);
  tos_bits = IPTOS_TOS(olsr_cnf->tos);
  if (setsockopt(skfd, SOL_SOCKET, SO_PRIORITY, (char*)&precedence, sizeof(precedence)) < 0)
  {
    LinkProbePError("setsockopt(SO_PRIORITY) error");
    /* Continue anyway */
  }
  if (setsockopt(skfd, SOL_IP, IP_TOS, (char*)&tos_bits, sizeof(tos_bits)) < 0)    
  {
    LinkProbePError("setsockopt(IP_TOS) error");
    /* Continue anyway */
  }

  AddDescriptorToInputSet(skfd);

  return skfd;
} /* CreateSocket */

/* -------------------------------------------------------------------------
 * Function   : CreateInterface
 * Description: Create a new TProbedInterface object and adds it to the
 *              global ProbedInterfaces list
 * Input      : olsrIntf - OLSR interface object of the network interface
 * Output     : none
 * Return     : the created TProbedInterface object, or NULL if an error
 *              occurred
 * Data Used  : ProbedInterfaces
 * ------------------------------------------------------------------------- */
static struct TProbedInterface* CreateInterface(struct olsr_if* olsrIntf)
{
  struct TProbedInterface* newIf;
  int skfd;

  /* Check pre-conditions: network interface must be OLSR-enabled */
  assert(olsrIntf != NULL && olsrIntf->interf != NULL);

  /* Allocate memory for object */
  newIf = olsr_calloc(sizeof(struct TProbedInterface), "Link Probe plugin - CreateInterface");
  if (newIf == NULL)
  {
    return NULL;
  }

  /* Create socket for sending and receiving probe packets */
  skfd = CreateSocket(olsrIntf->interf->int_name);
  if (skfd < 0)
  {
    free(newIf);
    return NULL;
  }
  
  /* Clear trip time logging data and initialize object */
  newIf->skfd = skfd;
  newIf->olsrIntf = olsrIntf;

  newIf->mtu = newIf->olsrIntf->interf->int_mtu;
  newIf->packetSizeDiffBits = (newIf->mtu - PROBE_MINLEN) * 8;

  /* Add new object to global list */
  newIf->next = ProbedInterfaces;
  ProbedInterfaces = newIf;

  newIf->nextLinkTimeSlot = NextInterfaceTimeSlot;
  NextInterfaceTimeSlot = 
    (NextInterfaceTimeSlot + TIME_SLOT_INCREMENT_INTERFACE)
    % N_TIME_SLOTS;

  return newIf;
} /* CreateInterface */

/* -------------------------------------------------------------------------
 * Function   : CreateInterfaces
 * Description: Create Link Probe interface objects for all OLSR-enabled network
 *              interfaces
 * Input      : skipThisIntf - network interface to skip, if encountered
 * Output     : none
 * Return     : fail (-1) or success (0)
 * Data Used  : none
 * ------------------------------------------------------------------------- */
int CreateInterfaces(struct network_interface* skipThisIntf)
{
  struct olsr_if* iface;
  int result = -1;
  int nOpened = 0;

  for (iface = olsr_cnf->interfaces; iface != NULL; iface = iface->next)
  {
    /* Rely on short-circuit boolean evaluation */
    if (iface->interf != NULL && 
        iface->interf != skipThisIntf && 
        ! IsNonProbedInterface(iface->interf->int_name))
    {
      if (CreateInterface(iface) != NULL)
      {
        result = 0;
        nOpened++;
      }
    }
  } /* for */

  if (nOpened == 0)
  {
    olsr_printf(1, "%s: could not initialize any network interface\n", PLUGIN_NAME);
  }
  else
  {
    olsr_printf(
      1,
      "%s: probing %d network interface%s\n",
      PLUGIN_NAME,
      nOpened,
      nOpened == 1 ? "" : "s");
  }

  return result;
} /* CreateInterfaces */

/* -------------------------------------------------------------------------
 * Function   : AddInterface
 * Description: Add a network interface to the list of probed network
 *              interfaces
 * Input      : newIntf - network interface to add
 * Output     : none
 * Return     : none
 * Data Used  : none
 * ------------------------------------------------------------------------- */
void AddInterface(struct network_interface* newIntf)
{
  struct olsr_if* iface;
  int nOpened = 0;

  assert(newIntf != NULL);

  if (IsNonProbedInterface(newIntf->int_name))
  {
    return;
  }

  for (iface = olsr_cnf->interfaces; iface != NULL; iface = iface->next)
  {
    if (iface->interf == newIntf)
    {
      if (CreateInterface(iface) != NULL)
      {
        nOpened++;
      } /* if */
    } /* if */
  } /* for */

  olsr_printf(
    1,
    "%s: added %d network interface%s for probing\n",
    PLUGIN_NAME,
    nOpened,
    nOpened == 1 ? "" : "s");
} /* AddInterface */

/* -------------------------------------------------------------------------
 * Function   : CloseInterfaces
 * Description: Closes every socket on each network interface used by
 *              the Link Probe plugin
 * Input      : none
 * Output     : none
 * Return     : none
 * Data Used  : none
 * Notes      : Also restores the network state to the situation before
 *              OLSR was started
 * ------------------------------------------------------------------------- */
void CloseInterfaces(void)
{
  int nClosed = 0;
  
  /* Close all opened sockets and free all allocated memory. */
  struct TProbedInterface* nextIf = ProbedInterfaces;
  while (nextIf != NULL)
  {
    struct TProbedInterface* currIf = nextIf;
    nextIf = currIf->next;

    if (currIf->skfd >= 0)
    {
      close(currIf->skfd);
      nClosed++;
    } /* if */

    free(currIf);
  } /* while */

  ProbedInterfaces = NULL;

  olsr_printf(
    1,
    "%s: stopped probing %d network interface%s\n",
    PLUGIN_NAME,
    nClosed,
    nClosed == 1 ? "" : "s");
} /* CloseInterfaces */

#define MAX_NON_PROBED_IFS 10
static char NonProbedIfNames[MAX_NON_PROBED_IFS][IFNAMSIZ];
static int nNonProbedIfs = 0;

/* -------------------------------------------------------------------------
 * Function   : AddNonProbedInterface
 * Description: Add a network interface to the list of not-probed interfaces
 * Input      : ifName - network interface (e.g. "eth0")
 *              data - not used
 *              addon - not used
 * Output     : none
 * Return     : success (0) or fail (1)
 * Data Used  : none
 * ------------------------------------------------------------------------- */
int AddNonProbedInterface(
  const char* ifName,
  void* data __attribute__((unused)),
  set_plugin_parameter_addon addon __attribute__((unused)))
{
  assert(ifName != NULL);

  if (nNonProbedIfs >= MAX_NON_PROBED_IFS)
  {
    olsr_printf(
      1,
      "%s: too many non-probed interfaces specified, maximum is %d\n",
      PLUGIN_NAME,
      MAX_NON_PROBED_IFS);
    return 1;
  }

  strncpy(NonProbedIfNames[nNonProbedIfs], ifName, IFNAMSIZ);
  nNonProbedIfs++;
  return 0;
} /* AddNonProbedInterface */

/* -------------------------------------------------------------------------
 * Function   : IsNonProbedInterface
 * Description: Checks if a network interface is to be probed
 * Input      : ifName - network interface (e.g. "eth0")
 * Output     : none
 * Return     : true (1) or false (0)
 * Data Used  : none
 * ------------------------------------------------------------------------- */
int IsNonProbedInterface(const char* ifName)
{
  int i;

  assert(ifName != NULL);

  for (i = 0; i < nNonProbedIfs; i++)
  {
    if (strncmp(NonProbedIfNames[i], ifName, IFNAMSIZ) == 0) return 1;
  }
  return 0;
} /* IsNonProbedInterface */
