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
#include <pthread.h>
#include <string.h>
#define PortOnline_Text PortOnline_TextUp
#include "online_language.c"
#undef PortOnline_Text

int gVitaTagLang = 1;

/* PortOnline_TextUp looks a line up in three tables (about 950 entries, strcmp each) and asks the environment twice, on
   every call; the screens call it for every line of every picture, on the Spanish disc the whole way down for a line
   that is not in them. The answers are kept: by the line's hash, the line itself checked. bt3.txt tbopre (the same
   switch as the maker's, vita_tbo.c): 0 asked every time, as v1.4. */
#define MEMO_SLOTS 512
#define MEMO_KEY 160
extern int gVitaTboPre;
static struct {
    unsigned hash;
    const char *answer; /* a line of the tables, or NULL: the line as it is */
    char key[MEMO_KEY];
    unsigned char used;
} sMemo[MEMO_SLOTS];
static pthread_mutex_t sMemoLock = PTHREAD_MUTEX_INITIALIZER;

const char *PortOnline_Text(const char *text) {
    unsigned hash = 2166136261u, slot;
    size_t n = 0;
    const char *answer;
    if (!gVitaTagLang) {
        return text;
    }
    if (!gVitaTboPre || text == NULL || sRaw) { /* (sRaw: the raw text asked for, online_language.c) */
        return PortOnline_TextUp(text);
    }
    while (text[n] != '\0' && n < MEMO_KEY) {
        hash = (hash ^ (unsigned char)text[n++]) * 16777619u;
    }
    if (n >= MEMO_KEY) { /* (too long to keep) */
        return PortOnline_TextUp(text);
    }
    slot = hash % MEMO_SLOTS;
    pthread_mutex_lock(&sMemoLock);
    if (sMemo[slot].used && sMemo[slot].hash == hash && strcmp(sMemo[slot].key, text) == 0) {
        answer = sMemo[slot].answer != NULL ? sMemo[slot].answer : text;
        pthread_mutex_unlock(&sMemoLock);
        return answer;
    }
    pthread_mutex_unlock(&sMemoLock);
    answer = PortOnline_TextUp(text);
    pthread_mutex_lock(&sMemoLock);
    sMemo[slot].used = 1;
    sMemo[slot].hash = hash;
    sMemo[slot].answer = answer == text ? NULL : answer;
    memcpy(sMemo[slot].key, text, n + 1);
    pthread_mutex_unlock(&sMemoLock);
    return answer;
}
