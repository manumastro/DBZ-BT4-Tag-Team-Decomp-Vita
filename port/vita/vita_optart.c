/*
 * The menus' art changed for the PS Vita, applied to a screen's pack of sections as soon as it is unpacked.
 *
 * The options screen (src/menu/menu_x_c.c Option_Init), its controller page:
 *   - section 16, textures 1 and 3 (the rows' lettering, plain and highlighted): "Vibration" becomes "Vita Settings";
 *   - section 14, textures 1 and 3 (the second strip of the two-way pickers' captions): the rows the Vita does not
 *     show (1P, 2P, Simple) and the free ones read L2, R2, L3, Rear, Front and R3 (Touch Buttons), or, as the page
 *     changes, Touch, Clashes, 2nd Pad, Mods, Square and Stick (Vita_OptionCaptions, Option_VitaSetPlates);
 *   - section 21, texture 0: the controller picture of the key assignment page, a PS2 pad, becomes a Vita;
 *   - section 33 (the guide's lines under the screen): lines 24, 39 and 40 (of vibration) say what the new row does.
 * The versus menu (src/menu/menu_g_c.c DuelMenu_Init):
 *   - section 8, textures 0 and 1 (the plates' lettering, plain and highlighted): "1P VS 2P" becomes "Multiplayer".
 * The button icons of the game's text (the boot file's member 8, src/battle/col_c_b.c FontIcon_Init), seen in the
 * skill list, the hints and the tutorials:
 *   - textures 16 and 19, L3 and R3: the PS2's sticks become black plates like L1 and R2, reading L3 and R3;
 *   - textures 26 and 27, L3 and R3 held down: the same plates under the yellow arrow of the held buttons.
 * The pictures are drawn in the screens' own lettering style (port/vita/tools: gen_patches.py made vita_optart.h
 * from the USA disc's files 454, 449 and 5). Each section is changed only when the bytes to replace are those of that
 * disc (FNV-1a of them); otherwise it stays as it is and the log says so.
 */
#include <stdint.h>
#include <string.h>
#include "vita_optart.h"
#include "vita_optart_es.h"

extern void Vita_Log(const char *fmt, ...);

#ifndef PORT_VITA_RELEASE
#include <stdio.h>
#include <stdlib.h>
#include "vita_paths.h"
/* (development build, bt3.txt artdump=1) a screen's pack as unpacked, up to the end of section `last`, written to
   PORT_VITA_HOME/artdump_<name>.bin: what tools/gen_patches.py makes the changes from, for a disc whose art is not the
   USA release's (BT4decomp's). The files are the player's disc's: they stay on the console or under port/build/. */
static void art_dump(const char *name, void *res, unsigned last) {
    uint32_t *w = (uint32_t *)res, n;
    char path[200];
    FILE *f;
    if (getenv("BT3_ARTDUMP") == NULL || res == NULL || w[0] <= last) {
        return;
    }
    n = w[last + 1] & ~3u;
    snprintf(path, sizeof(path), PORT_VITA_HOME "/artdump_%s.bin", name);
    f = fopen(path, "wb");
    if (f != NULL) {
        fwrite(res, 1, n, f);
        fclose(f);
        Vita_Log("artdump: %s, %u bytes\n", path, (unsigned)n);
    }
}
#else
#define art_dump(name, res, last) ((void)0)
#endif

static uint8_t *pack_at(void *res, int n) {
    uint32_t *w = (uint32_t *)res;
    return (uint8_t *)res + ((w[n] >> 2) << 2);
}

/* The guide's text table: word 0 the count of entries, word n the offset of entry n (line n - 1), each a slot of fixed
   size holding UTF-16 with a byte-order mark. The Wii's lines, which this disc does not use, give their slots. */
typedef struct Slot {
    uint8_t *p;
    uint32_t size;
} Slot;

static Slot slot_of(uint8_t *table, int entry) {
    uint32_t *w = (uint32_t *)table;
    Slot s = {NULL, 0};
    if ((uint32_t)entry < w[0] && w[entry + 1] > w[entry] && table[w[entry]] == 0xFF && table[w[entry] + 1] == 0xFE) {
        s.p = table + w[entry];
        s.size = w[entry + 1] - w[entry];
    }
    return s;
}

static int write_slot(Slot s, const char *text) {
    size_t i, n = strlen(text);
    if (s.p == NULL || (n + 2) * 2 > s.size) {
        return 0;
    }
    memset(s.p + 2, 0, s.size - 2);
    for (i = 0; i < n; i++) {
        s.p[2 + i * 2] = (uint8_t)text[i];
    }
    return 1;
}

/* line entry - 1 reads the slot of entry `into`, with the text */
static int put_line(uint8_t *table, int entry, int into, const char *text) {
    Slot s = slot_of(table, into);
    if ((uint32_t)entry > ((uint32_t *)table)[0] || !write_slot(s, text)) {
        return 0;
    }
    ((uint32_t *)table)[entry] = (uint32_t)(s.p - table);
    return 1;
}

/* lines 39 and 40, written by the controller page as its cursor moves (Vita_OptionGuide) */
static Slot sGuide[2];
static int sGuideNext;

typedef struct ArtSection {
    int section, first, count;
    uint32_t hash;
} ArtSection;

/* the runs of each section written over, when the bytes they replace are the USA disc's; returns the sections done */
static unsigned apply(void *res, const ArtSection *secs, unsigned n, const uint32_t (*runs)[3], const uint8_t *bytes, const char *what) {
    unsigned s, k, done = 0;
    for (s = 0; s < n; s++) {
        uint8_t *sec = pack_at(res, secs[s].section);
        uint32_t h = 2166136261u;
        for (k = 0; k < (unsigned)secs[s].count; k++) {
            const uint8_t *p = sec + runs[secs[s].first + k][0];
            uint32_t i;
            for (i = 0; i < runs[secs[s].first + k][1]; i++) {
                h = (h ^ p[i]) * 16777619u;
            }
        }
        if (h != secs[s].hash) {
            Vita_Log("%s: section %d is not the USA disc's (%08x), left alone\n", what, secs[s].section, (unsigned)h);
            continue;
        }
        for (k = 0; k < (unsigned)secs[s].count; k++) {
            memcpy(sec + runs[secs[s].first + k][0], bytes + runs[secs[s].first + k][2], runs[secs[s].first + k][1]);
        }
        done++;
    }
    return done;
}

/* Whether the first section's bytes (section 14, the picker captions) are those the patch is made over (no log). */
static int art_hash_ok(void *res, const ArtSection *secs, const uint32_t (*runs)[3]) {
    uint8_t *sec = pack_at(res, secs[0].section);
    uint32_t h = 2166136261u, k, i;
    for (k = 0; k < (uint32_t)secs[0].count; k++) {
        const uint8_t *p = sec + runs[secs[0].first + k][0];
        for (i = 0; i < runs[secs[0].first + k][1]; i++) {
            h = (h ^ p[i]) * 16777619u;
        }
    }
    return h == secs[0].hash;
}

/* The options screen's patches, by the release whose pictures they are made over: the English disc's (vita_optart.h) and
   the Spanish one's (vita_optart_es.h; its lettering differs: "Volumen, Coleccion de Musica, Tipo de Control", the
   captions "Normal, Personali."). Which one is told by the hash of section 14's bytes. */
typedef struct OptVariant {
    const char *name;
    const void *secs;
    unsigned nsecs;
    const uint32_t (*runs)[3];
    const uint8_t *bytes;
    const uint32_t (*capRuns)[2];
    unsigned ncap;
    const uint8_t *capT, *capV;
} OptVariant;
static const OptVariant kOptVariants[2] = {
    {"English", kOptArtSections, sizeof(kOptArtSections) / sizeof(kOptArtSections[0]), kOptArtRuns, kOptArtBytes, kOptCapRuns,
     sizeof(kOptCapRuns) / sizeof(kOptCapRuns[0]), kOptCapT, kOptCapV},
    {"Spanish", kOptArtEsSections, sizeof(kOptArtEsSections) / sizeof(kOptArtEsSections[0]), kOptArtEsRuns, kOptArtEsBytes,
     kOptCapEsRuns, sizeof(kOptCapEsRuns) / sizeof(kOptCapEsRuns[0]), kOptCapEsT, kOptCapEsV},
};
static const OptVariant *sVar = &kOptVariants[0];

/* section 14 of the options screen as unpacked and patched (NULL: not the USA disc's, or not up), the captions in it */
static uint8_t *sCapSec;
static int sCapSet;

void Vita_OptionArt(void *res) {
    unsigned done, lines = 0, k;
    art_dump("opt", res, 33);
    sCapSec = NULL;
    if (res == NULL || ((uint32_t *)res)[0] < 38) {
        Vita_Log("optart: not the options screen's pack, left alone\n");
        return;
    }
    sVar = &kOptVariants[0];
    if (!art_hash_ok(res, (const ArtSection *)kOptVariants[0].secs, kOptVariants[0].runs) &&
        art_hash_ok(res, (const ArtSection *)kOptVariants[1].secs, kOptVariants[1].runs)) {
        sVar = &kOptVariants[1]; /* (the Spanish release's options screen: the same art, drawn over its own pictures) */
    }
    done = apply(res, (const ArtSection *)sVar->secs, sVar->nsecs, sVar->runs, sVar->bytes, "optart");
    {   /* the captions of Touch Buttons in place (as patched): the other set can take turns with them */
        uint8_t *sec = pack_at(res, 14);
        const uint8_t *b = sVar->capT;
        int same = 1;
        for (k = 0; k < sVar->ncap && same; k++) {
            same = memcmp(sec + sVar->capRuns[k][0], b, sVar->capRuns[k][1]) == 0;
            b += sVar->capRuns[k][1];
        }
        if (same) {
            sCapSec = sec;
            sCapSet = 0;
        }
    }
    Vita_Log("optart: the %s disc's options screen\n", sVar->name);
    /* the guide's lines (entry = line + 1), into slots of the Wii's lines: 24 the row's, 39 and 40 written as the
       page's cursor moves */
    {
        uint8_t *t = pack_at(res, 33);
        lines += put_line(t, 25, 28, "Touch buttons, clashes, a\nsecond controller and mods.");
        lines += put_line(t, 40, 27, "");
        lines += put_line(t, 41, 23, "");
        sGuide[0] = slot_of(t, 27);
        sGuide[1] = slot_of(t, 23);
        sGuideNext = 0;
    }
    Vita_Log("optart: %u of 3 pictures and %u of 3 lines changed for the Vita\n", done, lines);
}

void Vita_VersusArt(void *res) {
    unsigned done;
    art_dump("vs", res, 8);
    if (res == NULL || ((uint32_t *)res)[0] < 27) {
        Vita_Log("vsart: not the versus menu's pack, left alone\n");
        return;
    }
#ifdef NO_VSART
    (void)done;
    Vita_Log("vsart: this disc's versus menu is left as it is\n");
#else
    done = apply(res, (const ArtSection *)kVsArtSections, sizeof(kVsArtSections) / sizeof(kVsArtSections[0]), kVsArtRuns, kVsArtBytes, "vsart");
    Vita_Log("vsart: %u of 1 picture changed for the Vita\n", done);
#endif
}

void Vita_IconArt(void *boot) {
    unsigned done;
    art_dump("boot", boot, 8);
    if (boot == NULL || ((uint32_t *)boot)[0] < 9) {
        Vita_Log("iconart: not the boot file, left alone\n");
        return;
    }
    done = apply(boot, (const ArtSection *)kIconArtSections, sizeof(kIconArtSections) / sizeof(kIconArtSections[0]), kIconArtRuns, kIconArtBytes, "iconart");
    Vita_Log("iconart: %u of 1 icon file changed for the Vita (L3 / R3)\n", done);
}

/* The guide line for the controller page's "Touch Buttons" (src/menu/menu_x_c.c): the text into line 39 or 40, in
   turn, so that the message window takes it as a new line; returns the line (24, the row's, when the slots are not
   there). */
int Vita_OptionGuide(const char *text) {
    int k = sGuideNext;
    if (!write_slot(sGuide[k], text)) {
        return 24;
    }
    sGuideNext ^= 1;
    return 39 + k;
}

/* Vita Settings (src/menu/menu_x_c.c): the second caption strip with the captions of Touch Buttons (set 0: L2, R2, L3,
   Rear, Front, R3) or of the page above them (set 1: Touch, Clashes, 2nd Pad, Mods, Square, Stick). The menus upload
   a picture each time they draw it, so the change shows at once. */
void Vita_OptionCaptions(int set) {
    const uint8_t *b;
    unsigned k;
    if (sCapSec == NULL || set == sCapSet) {
        return;
    }
    b = set ? sVar->capV : sVar->capT;
    for (k = 0; k < sVar->ncap; k++) {
        memcpy(sCapSec + sVar->capRuns[k][0], b, sVar->capRuns[k][1]);
        b += sVar->capRuns[k][1];
    }
    sCapSet = set;
}

/* the options screen is closed: its pack is let go */
void Vita_OptionArtEnd(void) {
    sCapSec = NULL;
}
