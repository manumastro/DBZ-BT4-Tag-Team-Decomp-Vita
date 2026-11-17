/*
 * BT4decomp's own screens in the disc's language on the Vita: its port/src/gs/online_language.c, built unchanged.
 * PortOnline_Text gives a line of its screens (the Tag Team screens, the Tag Team fight's words, the main menu's Tag
 * Team description, the results) in Spanish on the Spanish B14 Rev2 disc and as written (English) on the English one,
 * by Port_GameProfileEnglish (port/vita/vita_bt4.c: which of the two releases ux0:data/bt4 holds, vita_mem.c). That is
 * the PC's own choice: its language picker (src/menu/port_language.c, port/src/gs/game_profile.c) chooses the disc
 * (Spanish or English) and leaves the menus' translation catalog at "en" (menu_translation.c: the disc's own text, no
 * pack over it); only the PC's Brazilian Portuguese pack has a catalog of its own, which the Vita does not have.
 *
 * bt3.txt taglang: 1 (default) as on the PC; 0 every line as written (English), as before.
 */
#define PortOnline_Text PortOnline_TextUp
#include "online_language.c"
#undef PortOnline_Text

int gVitaTagLang = 1;

const char *PortOnline_Text(const char *text) {
    return gVitaTagLang ? PortOnline_TextUp(text) : text;
}
