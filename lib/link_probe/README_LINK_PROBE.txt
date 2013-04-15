LINK-SPEED PROBING PLUGIN FOR OLSRD
by Erik Tromp (eriktromp@users.sourceforge.net, erik_tromp@hotmail.com)
Version 0.3

1. Introduction
---------------

The link-speed probing plugin attempts to detect the speed of connection (link) to each
neighbor node. This is done to facilitate link cost routing, in which path calculation is
based not only on a link quality metric (e.g. ETX) but also on a link capability metric.

The detected link speed is rounded upward to one of the following values:
  - 1 Mbit/sec
  - 2 Mbit/sec
  - 5 Mbit/sec
  - 10 Mbit/sec
  - 20 Mbit/sec
  - 50 Mbit/sec
  - 100 Mbit/sec
  - 200 Mbit/sec
  - 500 Mbit/sec
  - 1000 Mbit/sec

It it not the goal of the link-speed probing plugin to determine the link speed very
accurately; only a bare indication is enough as a basis for link cost routing. Therefore,
any measured value is rounded up toward the first higher value in the above mentioned list.

The link-speed probing plugin uses a probing mechanism to determine the link speed with each
neighbor node. The probing is done by measuring the trip time for:
* small unicast packets (e.g 24 bytes),
* large unicast packets (e.g. 1472 bytes),

By comparing the trip times of these packets, an indication of the link speed can be
derived.


2. How to build and install
---------------------------

Follow the instructions in the base directory README file under section II. - BUILDING AND
RUNNING OLSRD. To be sure to install the link-speed probing plugin, cd to the base directory
and issue the following command at the shell prompt:

  make build_all

followed by:

  make install_all

Edit the file /etc/olsrd.conf to load the link-speed plugin. For
example:

  LoadPlugin "olsrd_link_probe.so.0.3"
  {
    # No PlParam entries required for basic operation
  }


3. How to run
-------------

After building and installing OLSRD with the link-speed probing plugin, run the olsrd daemon
by entering at the shell prompt:

  olsrd

Look at the output; it should list the link-speed probing plugin, e.g.:

  ---------- LOADING LIBRARY olsrd_link_probe.so.0.3 ----------
  OLSRD LINK PROBE plugin 0.3 (Sep 30 2010 11:54:07)
    (C) Erik Tromp
    Erik Tromp (eriktromp@users.sourceforge.net)
    Download latest version at http://sourceforge.net/projects/olsr-lc
  Checking plugin interface version:  5 - OK
  Trying to fetch plugin init function: OK
  Trying to fetch parameter table and it's size...
  Sending parameters...
  Running plugin_init function...
  OLSRD LINK PROBE plugin: probing 3 network interfaces
  ---------- LIBRARY olsrd_link_probe.so.0.3 LOADED ----------


4. How to check if it works
---------------------------

To check that the link-speed probing plugin is working, run the olsr daemon with
debug level 1:

  olsrd -d 1

In the debug output, the estimated speed of each link is reported in Mbits/sec:

IP address       hyst   LQ     lost   total  NLQ      ETX  Speed  Cost
10.0.6.3         0.000  0.200  0      1      0.200  25.00      5  300000.00



5. Common problems, FAQ
-----------------------


Question:
How is the link speed estimated?

Answer:
As described above, the link speed is estimated using a packet pair
technique. The probing is done by measuring the trip time on each neighbor for
two packets, namely:
* small unicast packets (e.g 24 bytes),
* large unicast packets (e.g. 1472 bytes)

By comparing the trip times of these packets, an indication of the link speed can be
derived.

Here is the procedure
1.) A small unicast probe packet is sent to the neighbor
2.) The neighbor receives the probe packet and sends an equally small reply packet back to the
    sender
3.) A large unicast probe packet is sent to the neighbor
4.) The neighbor receives the probe packet and sends a small reply packet back to the
    sender. The size of the reply packet is the same as that in step 2.)

In other words, the sender alternates small and large probe packets, while the receiver
always replies with short reply packets.

Step 1.) and 2.) measure the round-trip time of small packets. On average, the smart packet
single trip time is half that of the measured round-trip time.

Step 3.) and 4.) measure the total trip time of one large packet and one small packet. The
single trip time of the large packet is calculated by subtracting the single trip time
of the small packet from the total (small + large) trip time.

The bandwidth is then estimated from the single trip time of the large packet. For example,
say the single trip time of a packet of 1472 bytes is 1.472 milliseconds. Then the estimated
bit rate is 1472 * 8 / 0.001472 = 8 Mbits/sec .


Question:
Is the link speed estimated for each individual neighbor link or for each available network
interface?

Answer:
The link speed estimated for each individual neighbor link. Suppose that there are 3 network
interfaces ("wlan0", "wlan1" and "wlan2"). On "wlan0" there is one neighbor, on "wlan1" there
are three neighbors, and on "wlan2" there are two neighbors. Then there are 1 + 3 + 2 neighbor
links being probed.


Question:
How much bandwidth is being used by the probing?

Answer:
All active probing methods involve injecting traffic onto the channels that are
being probed. If there is only one neighbor detected on a network interface, then
the default settings of the plugin lead to 1 probe packet and 1 reply packet being sent
every second. The small (24 UDP bytes) probe packets and large probe packets (1472 UDP bytes)
are sent alternatingly. Including overhead, the small packets are 66 Ethernet bytes and the
large probe packets are 1514 Ethernet bytes. So in 2 seconds, 66 + 66 + 1514 + 66 = 1712
Ethernet bytes are sent, averaging to 1712 / 2 = 856 Ethernet bytes / second. This number
of bytes excludes the overhead specific to the 802.11 technology.

Counting in bits, the probing of one neighbor costs 6848 bits/sec .

If there are more neighbors, more probe packets are sent, e.g. having 4 neighbors on one
network interface will lead to 4 * 6848 = 54784 bits / second.

On an 11 Mbits/sec network interface with an effective throughput of 5 Mbits/sec, probing
4 neighbors will cost approx. 1 percent of the available bandwidth.

In practice, I do not expect the numbers to scale. If, for example, there are 160 neighbors,
the probing bandwidth would be 40 % , but I think that the 802.11 ad-hoc mode will simply not
even work when there are 160 neighbors.

The compile-time constants PROBING_INTERVAL_BASE (default 0.05 seconds) and N_TIME_SLOTS
(default 20) - see file Probe.h - determine the frequency at which probe packets are being
sent. 20 * 0.05 = 1, leading to one probe packet and one reply packet being sent per second.


Question:
How are the trip times evaluated?

Answer:
Since trip times may vary wildly in 802.11 networks, the trip times are first filtered
then averaged. The filtering consists simply of excluding the 4 most extreme values; the
averaging is done over the remaining values. By default, the last 20 trip times are kept,
so the averaging is done over the 16 least extreme values.

The constant PROBE_HISTORY_SIZE (default 20) - see file Link.h - determines the number
of kept trip times.


Question:
I don't like active probing. Passive probing is the way....

Answer:
Since this is hardly a question, it is difficult to answer. There is a nearly religious
discussion on the internet about which ways there are to determine link quality. Even when
narrowing the discussion down and accepting link speed (bits/sec) as the measure of link
quality, there are various streams, each claiming that their god is the only one true one.

I think that there are two principle ways of measuring the speed of a link:

1.) Passively, by monitoring the 'rate' of the incoming packets.

2.) Actively, by sending specially formed probe packets and monitoring the speed of the
    probe packets.

Advantages of 1.):
+ Passively monitoring does not cost any extra air time

Disadvantages of 2.):
- Links that carry hardly any traffic are difficult to measure ("you may never discover a
  better alternative route due to lack of measurements on it") - this may be circumvented by
  switching to active probing on hardly used links.
- Tapping all traffic into user space for analysis by a user-space process causes a high
  CPU load
- Alternatively, if tapping is not used, there is a dependency on non-standard interfaces to
  retrieve metrics such as Layer-1 or -2 metrics (radio signal strength, number of
  retransmissions at the MAC layer, etc.). Besides, it is difficult to determine the link speed
  based on only these metrics.

Advantages of 2.):
+ Permanent monitoring of quality is possible, even in low-traffic circumstances.
+ Low CPU load because only the probe packets are processed (socket + select calls in kernel time)
+ No dependency on non-standard interfaces

Disadvantages of 2.):
- Requires extra air time


Question:
I found a way to see the quality of the link with each neighbor. Simply type:

ifconfig wlan0 list sta

at the command prompt. The response is a table with one line per neighbor:

ADDR               AID CHAN RATE RSSI IDLE  TXSEQ  RXSEQ CAPS FLAG
00:15:6d:53:1f:ee    0   64   6M  6.0    0   3271  42432 I    A
00:02:6f:41:19:2b    0   64   6M 13.5    0  53104  39744 I    A
00:80:48:50:9a:48    0   64   6M  8.0    0     82     96 I    A
00:02:6f:41:19:2b    0   64  36M 15.0   30   7664  22896 I    A
00:02:6f:41:19:2b    0   64  36M 15.5   30   8135  36928 I    A
00:02:6f:41:19:2b    0   64  48M 15.0    0   8640  39712 I    A
00:15:6d:53:20:0b    0   64   6M  2.5    0      0  16208 I    A

The column 'RATE' indicates the rate of communication with each neighbor.

Would this be a good way to estimate the link speed?

Answer:
It would be nice to have a plugin that uses Layer-1 or Layer-2 metrics
to estimate the link speed. If you write one I will be eager to add it to the
code.

Bear in mind, though, that:

1.) The indicated 'RATE' is the rate at which the last(?) packet has been received
    by the 802.11 MAC layer. This is probably not the same as the effective link
    speed, which is lower due to packet overhead, collisions, back-off time, etc.
    Also, the sending node may not have a rate adaptation algorithm, forcing it
    to send all its packets at a fixed high rate. Receiving these high-rate
    packets does not imply that the throughput is also high, since a lot of packets
    may need to be retransmitted.

2.) Not all Linux variants, and not all Wifi drivers, offer an interface to retrieve
    such Layer-1 and Layer-2 information. There is hardly any standardization of
    software interfaces to this kind of data. If you write a plugin that uses
    Layer-1 and Layer-2 information, please specify exactly on which drivers and
    on which Linux variants it was tested.

3.) Not all network interfaces that OLSR operates on are wireless. Examples of
    other kinds of interfaces are wired (e.g. 100Mbps/802.3u or 1000Mbps/802.3z
    EtherNet) or PPP connections. Asking a report of radio rates on a wired Ethernet
    or PPP connection is not applicable.

For more inspiration on cross-layer interfacing, see http://xian.sourceforge.net/


Question:
It is not the primary task of a routing daemon to determine link speeds. This should be
left to other processes.

Answer:
That is why the link probing is implemented in a separate plugin. Compare it to the
httpinfo plugin: it is also not the primary task of a routing daemon to offer a user-friendly
web interface, therefore it is available in a separate plugin.


Question:
My WLAN network is 802.11g , which is 54 Mbits/sec . Why does the link probing
plugin estimate a link speed of only 10 Mbits/sec?

Answer:
The specified speed varies from the actual throughput due to various factors, such as:
- per-packet overhead: packet headers are sent at a (lower) basic bit rate.
- protocol overhead (RTS/CTS)
- air time collisions resulting in back-off
- packet retransmissions
- auto rate fallback (ARF) mechanisms that choose a lower transmission rate in less 
  favorable conditions
- etc., etc.

