/*
 * PS Vita, online Tag Team (milestone T1, first step): the room's authority, as the PC's room owner runs it.
 *
 * BT4decomp's online Tag Team is owned by one player's game: the owner runs the room's state machine
 * (shared/tag_kernel.c, the Kernel_* calls: commands checked, seats, phase, epoch, options, the fight's frames of all
 * pads) and every other player sends it commands and pads and gets back states and frames (port/src/gs/tag_team_p2p.cpp
 * on the PC, over WebRTC; the server's copy of the same kernel in wasm for the server rooms). The Vita's rooms between
 * Vitas (Ad Hoc first) keep that: the same kernel, built here unchanged as the PC builds it natively
 * (port/src/gs/tag_team_authority.c), and a Vita transport under it (PDP with a reliable control channel: the next
 * step, notes/online/vita-multiplayer-plan.md section 7). Only in a build with BT4_VITA_NETLINK=1; nothing calls it
 * yet.
 */
#define TT_KERNEL_NATIVE 1
#include "../../shared/tag_kernel.c"
