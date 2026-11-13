/* Where the Vita build keeps its files and which program of the disc it reads: Budokai Tenkaichi 3 by default
   (ux0:data/bt3, SLUS_216.78), set by port/vita/build.py for another game built from the same code (BT4decomp:
   ux0:data/bt4, SLUS_219.78, PORT_VITA_BT4). */
#ifndef VITA_PATHS_H
#define VITA_PATHS_H
#ifndef PORT_VITA_HOME
#define PORT_VITA_HOME "ux0:data/bt3"
#endif
#ifndef PORT_VITA_ELF
#define PORT_VITA_ELF "SLUS_216.78"
#endif
#endif
