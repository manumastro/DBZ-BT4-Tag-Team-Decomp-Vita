/* PS Vita: the Ad Hoc rows of the online hub (src/menu/port_tbo.c, PORT_VITA) and the session's start
   (vita_netglue.c), answered by port/vita/vita_adhoc.c. Plain C. */
#ifndef VITA_ADHOC_H
#define VITA_ADHOC_H

/* a room a host advertises on the group, as a joiner's list shows it */
typedef struct VitaAdhocRoom {
    char name[32]; /* the host's online name */
    char mac[18];  /* its MAC, "AA:BB:CC:DD:EE:FF" */
    char addr[16]; /* the address net.c is given for it: 10.m3.m4.m5 */
    int port;      /* its lobby's port (net.c's, carried on the group) */
    int age;       /* ms since its last beacon */
} VitaAdhocRoom;

int Vita_AdhocMenuOn(void);                                  /* bt3.txt adhoc_menu */
int Vita_AdhocMenuBegin(int host, const char *name, int port); /* the group's dialog, then beacons (host) or the list */
int Vita_AdhocMenuState(void);                               /* 0 idle, 1 the dialog, 2 in the group, -1 no group */
int Vita_AdhocRooms(VitaAdhocRoom *out, int max);            /* the rooms heard lately */
void Vita_AdhocArm(const char *how);                         /* net.c's next socket on the group: "1" host, a MAC joiner */
void Vita_AdhocMenuEnd(int keep);                            /* beacons stopped; keep 0: the group left too */
void Vita_AdhocSessionLoad(void);                            /* at a session's start (session.txt read) */
#endif
