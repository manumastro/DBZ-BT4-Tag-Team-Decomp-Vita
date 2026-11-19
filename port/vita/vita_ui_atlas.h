/* Written by port/vita/tools/vita_ui_atlas.py: the Tag Team screens' kit made at its pixel size on the
   960 x 544 screen (tbo/ui/tag_ui_vita.vt, bt3.txt taguivita), its pieces' boxes in pixels. Do not edit;
   run the script with --header. */
#ifndef VITA_UI_ATLAS_H
#define VITA_UI_ATLAS_H
#define TBO_VITA_UI 0x241 /* its picture number (TEX0's CBP with TBO_PSM, include/menu/tbo_art.h: free) */
#define VUA_W 512
#define VUA_H 248
/* X(picture, x, y, w, h) */
#define VUA_BOXES(X) \
    X(TBO_KIT_BAR, 2, 152, 356, 28) \
    X(TBO_KIT_SEL, 2, 184, 335, 28) \
    X(TBO_KIT_BAR_COMPACT, 112, 216, 45, 28) \
    X(TBO_KIT_SEL_COMPACT, 161, 216, 42, 28) \
    X(TBO_KIT_GOLD, 2, 216, 106, 28) \
    X(TBO_KIT_STEEL, 341, 184, 114, 28) \
    X(TBO_KIT_WINDOW, 268, 2, 124, 60) \
    X(TBO_KIT_DIALOG, 2, 94, 340, 54) \
    X(TBO_BTN_CROSS, 313, 216, 24, 24) \
    X(TBO_BTN_SQUARE, 341, 216, 24, 24) \
    X(TBO_BTN_TRIANGLE, 369, 216, 24, 24) \
    X(TBO_BTN_CIRCLE, 397, 216, 24, 24) \
    X(TBO_BTN_LEFTRIGHT, 269, 216, 40, 24) \
    X(TBO_BTN_START, 207, 216, 58, 24) \
    X(TBO_TAG_LOGO, 2, 2, 262, 88) \
    X(TBO_MAIN_TAG_EMBLEM, 346, 94, 47, 48) \

#endif
