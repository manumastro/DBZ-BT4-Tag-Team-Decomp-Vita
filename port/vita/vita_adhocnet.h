/* PS Vita, online milestone A1: gs/net.c's socket calls sent to port/vita/vita_adhoc.c (VitaNet_*), which carries
   its datagram socket over Ad Hoc in an Ad Hoc session (BT3_NET_ADHOC) and is the C library's own otherwise.
   build.py puts this before gs/net.c's first line (-include) in a BT4_VITA_NETLINK=1 build; net.c is unchanged.
   Object-like names: the C library's own declarations of these calls become the VitaNet_* ones, same types. */
#ifndef VITA_ADHOCNET_H
#define VITA_ADHOCNET_H
#define socket VitaNet_socket
#define connect VitaNet_connect
#define bind VitaNet_bind
#define sendto VitaNet_sendto
#define recvfrom VitaNet_recvfrom
#define select VitaNet_select
#define getsockname VitaNet_getsockname
#define close VitaNet_close
#endif
