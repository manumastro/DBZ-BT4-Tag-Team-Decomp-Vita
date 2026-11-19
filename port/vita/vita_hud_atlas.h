/* Written by port/vita/tools/vita_hud_atlas.py: the Tag Team HUD's atlas (tbo/ui/tag_hud_vita.vt), its
   elements' boxes in pixels. Do not edit; run the script with --header. */
#ifndef VITA_HUD_ATLAS_H
#define VITA_HUD_ATLAS_H
#define TBO_VITA_HUD 0x240 /* its picture number (TEX0's CBP with TBO_PSM, include/menu/tbo_art.h: free) */
#define VHA_W 128
#define VHA_H 112
#define VHA_RADAR 0, 0, 48, 48
#define VHA_RING_L 48, 0, 64, 64
#define VHA_RING_M 0, 64, 44, 44
#define VHA_RING_S 44, 64, 30, 30
#define VHA_CONTACT 112, 0, 9, 9
#define VHA_DOT 112, 10, 8, 8
#define VHA_STICK 74, 64, 16, 16
#define VHA_KEY 90, 64, 18, 15
#define VHA_ARROW 108, 64, 16, 16
#define VHA_BAR 0, 108, 64, 4
#endif
