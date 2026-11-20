/* The Vita's own controls, each with a default and a place the player can change it (bt3.txt remap, default 1;
   docs/vita/development.md "remap").

   The game (and BT4decomp's Tag Team, as on PC) reads the PS2's pad: L2, R2, L3 and R3 too, and its Tag Team actions
   are chords of those (src/battle/battle.c: Senzu = L2 held + SELECT pressed; tag_team_fusion.c: R3 + a direction,
   L3 + R3 undoes a fusion; btl_input.c: the target by the method of Camera mode > Target selection). The PC port lets
   its players bind each PS2 button to a key or a pad button (port/src/gs/gs_input.c, its F1 window). The Vita has no
   L2 / R2 / L3 / R3, so the port gives them: the touch panels' corners (vita_touch.c, Vita Settings > Touch) and the
   button pairs below. This file is the list of those pairs and of the port's other buttons, what each gives, its
   default (the buttons fixed before), and the player's choice, saved in bt3_settings.txt (ux0:data/bt4/) beside the
   other Vita Settings as vbind_<name>=<buttons> (absent: the default; an old file is read as it is, nothing to move).

   A binding is SELECT plus at most one other of the handheld's buttons (SELECT is the one button a fight hardly
   uses; every other is the game's), in SceCtrl's bits; 0 is none. What a binding gives:
     L2, R2, L3, R3   that PS2 button while held (defaults SELECT + L, SELECT + R, none, none: the touch panels);
     Senzu            in a Tag Team fight, L2 held and SELECT pressed (the game's chord) for a single press (none);
     Fusion           in a Tag Team fight, R3 while held (with a direction: the fusion's choice) (none);
     Undo fusion      in a Tag Team fight, L3 + R3 (none);
     Quit the game    held 1.5 s, the program ends (START + SELECT, vita_main.c);
     Unlock All       held 1 s in the menus (SELECT alone, vita_main.c).
   The Tag Team target's method (right stick, D-pad, L3) is the PC's own setting (Camera mode > Target selection,
   tag_target_method_p1), shown and changed here too (vita_gfx.c PortInput_TagTargetMethod). The game's chords
   always work as well: a binding is a second way, not a replacement.

   SELECT with a binding: SELECT pressed alone is held back up to SEL_WAIT_US, so that the pair's second button can
   come; then (or at once when let go: a press of two reads) the game gets it. Before, SELECT + L gave the game a
   SELECT press first (a Tag Team fight steps its camera on one: btl_input.c).

   remap=0: the buttons fixed as before (vita_gfx.c's old code), the editor not listed. Only the handheld's buttons
   (and a PS TV's first pad as the handheld) go through here: a DualShock has its own L2 / R2 / L3 / R3. */
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vita_remap.h"

extern int Port_Setting(const char *name, int def);
extern void Port_SettingSave(const char *name, int value);
extern void Port_SettingsWrite(void);
extern void Vita_Log(const char *fmt, ...);
extern int gVitaBt4English;

int gVitaRemap = 1; /* bt3.txt remap: 1 (default) the bindings below, 0 the fixed buttons as before */

/* the PS2 pad's bits (include/sys/pad.h), 1 = pressed */
enum { P_SELECT = 0x0001, P_L3 = 0x0002, P_R3 = 0x0004, P_L2 = 0x0100, P_R2 = 0x0200 };

#define VB_ALL (SCE_CTRL_SELECT | SCE_CTRL_START | SCE_CTRL_UP | SCE_CTRL_RIGHT | SCE_CTRL_DOWN | SCE_CTRL_LEFT | \
                SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_TRIANGLE | SCE_CTRL_CIRCLE | SCE_CTRL_CROSS | SCE_CTRL_SQUARE)
#define VB_MODS (SCE_CTRL_SELECT | SCE_CTRL_START | SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)
#define SEL_WAIT_US 150000

/* contexts: where a binding works (two that share none may have the same buttons) */
enum { CX_MENU = 1, CX_FIGHT = 2, CX_TAG = 4, CX_ANY = 7 };

static const struct {
    const char *key, *en, *es;
    unsigned out;  /* the PS2 bits it gives (0: not a pad binding) */
    unsigned def;  /* the default, SceCtrl bits */
    int cx;
} kAct[VR_COUNT] = {
    {"l2", "L2", "L2", P_L2, SCE_CTRL_SELECT | SCE_CTRL_LTRIGGER, CX_ANY},
    {"r2", "R2", "R2", P_R2, SCE_CTRL_SELECT | SCE_CTRL_RTRIGGER, CX_ANY},
    {"l3", "L3", "L3", P_L3, 0, CX_ANY},
    {"r3", "R3", "R3", P_R3, 0, CX_ANY},
    {"senzu", "Senzu (Tag)", "Semilla (Tag)", P_L2 | P_SELECT, 0, CX_TAG},
    {"fusion", "Fusion (Tag)", "Fusión (Tag)", P_R3, 0, CX_TAG},
    {"unfuse", "Unfuse (Tag)", "Separar (Tag)", P_L3 | P_R3, 0, CX_TAG},
    {"target", "Target (Tag)", "Objetivo (Tag)", 0, 0, CX_TAG},
    {"quit", "Quit (hold)", "Cerrar (mantén)", 0, SCE_CTRL_START | SCE_CTRL_SELECT, CX_ANY},
    {"unlock", "Unlock All (hold)", "Desbloquear (mantén)", 0, SCE_CTRL_SELECT, CX_MENU},
};

static int sLoaded;
static unsigned sBind[VR_COUNT];
static unsigned sOverride[VR_COUNT], sOverSet; /* bt3.txt vbind_<name>= (over the saved one, this run) */

static int bits(unsigned m) {
    int n = 0;
    while (m) {
        n += m & 1;
        m >>= 1;
    }
    return n;
}

/* a binding the rules allow: none, or SELECT and at most one other (SELECT alone: Unlock All only; two buttons: Quit) */
static int valid(int a, unsigned m) {
    if (m == 0) {
        return 1;
    }
    if ((m & ~VB_ALL) || !(m & SCE_CTRL_SELECT) || bits(m) > 2) {
        return 0;
    }
    return bits(m) == 2 || a == VR_UNLOCK;
}

static void load(void) {
    int a;
    char key[32];
    if (sLoaded) {
        return;
    }
    sLoaded = 1;
    for (a = 0; a < VR_COUNT; a++) {
        int v;
        sBind[a] = kAct[a].def;
        if (a == VR_TARGET) {
            continue;
        }
        snprintf(key, sizeof(key), "vbind_%s", kAct[a].key);
        v = Port_Setting(key, (int)kAct[a].def);
        if (valid(a, (unsigned)v)) {
            sBind[a] = (unsigned)v;
        }
        if (sOverSet & (1u << a)) {
            sBind[a] = sOverride[a];
        }
    }
}

int Vita_RemapOn(void) { return gVitaRemap != 0; }

unsigned Vita_RemapGet(int a) {
    if (a < 0 || a >= VR_COUNT) {
        return 0;
    }
    if (!gVitaRemap) {
        return kAct[a].def;
    }
    load();
    return sBind[a];
}

unsigned Vita_RemapDefault(int a) { return a >= 0 && a < VR_COUNT ? kAct[a].def : 0; }

const char *Vita_RemapName(int a) {
    if (a < 0 || a >= VR_COUNT) {
        return "";
    }
    return gVitaBt4English ? kAct[a].en : kAct[a].es;
}

/* the action that already has `m` where `a` would work too (-1 none): the editor refuses it */
int Vita_RemapConflict(int a, unsigned m) {
    int b;
    if (m == 0) {
        return -1;
    }
    load();
    for (b = 0; b < VR_COUNT; b++) {
        if (b != a && b != VR_TARGET && sBind[b] == m && (kAct[a].cx & kAct[b].cx)) {
            return b;
        }
    }
    return -1;
}

int Vita_RemapValid(int a, unsigned m) { return a >= 0 && a < VR_COUNT && a != VR_TARGET && valid(a, m); }

void Vita_RemapSet(int a, unsigned m) {
    char key[32];
    if (a < 0 || a >= VR_COUNT || a == VR_TARGET || !valid(a, m)) {
        return;
    }
    load();
    sBind[a] = m;
    sOverSet &= ~(1u << a);
    snprintf(key, sizeof(key), "vbind_%s", kAct[a].key);
    Port_SettingSave(key, (int)m);
    Port_SettingsWrite();
    Vita_Log("vita: remap: %s = 0x%x\n", kAct[a].key, m);
}

void Vita_RemapDefaults(void) {
    extern int PortInput_TagTargetMethod(int player, int method);
    extern int gVitaTagTargetDefault;
    char key[32];
    int a;
    load();
    for (a = 0; a < VR_COUNT; a++) {
        if (a == VR_TARGET) {
            continue;
        }
        sBind[a] = kAct[a].def;
        snprintf(key, sizeof(key), "vbind_%s", kAct[a].key);
        Port_SettingSave(key, (int)kAct[a].def);
    }
    sOverSet = 0;
    /* the PC's default method (gs_input.c: tag_target_method_p1 from target_stick_p1, 0 unless set: L3) */
    PortInput_TagTargetMethod(0, gVitaTagTargetDefault >= 0 && gVitaTagTargetDefault <= 2 ? gVitaTagTargetDefault : 0); /* (writes the file) */
    Vita_Log("vita: remap: the defaults back\n");
}

/* ---- bt3.txt vbind_<name>=<buttons>: "none", "select", "select+l", "start+select", "select+cross", or a number ---- */
static const struct { const char *word; unsigned bit; } kWords[] = {
    {"select", SCE_CTRL_SELECT}, {"start", SCE_CTRL_START}, {"up", SCE_CTRL_UP}, {"right", SCE_CTRL_RIGHT},
    {"down", SCE_CTRL_DOWN}, {"left", SCE_CTRL_LEFT}, {"l", SCE_CTRL_LTRIGGER}, {"r", SCE_CTRL_RTRIGGER},
    {"triangle", SCE_CTRL_TRIANGLE}, {"circle", SCE_CTRL_CIRCLE}, {"cross", SCE_CTRL_CROSS}, {"square", SCE_CTRL_SQUARE},
};
#define WORDS ((int)(sizeof(kWords) / sizeof(kWords[0])))

int Vita_RemapOverride(const char *name, const char *value) {
    unsigned m = 0;
    int a, k;
    for (a = 0; a < VR_COUNT && strcmp(kAct[a].key, name) != 0; a++) {
    }
    if (a >= VR_COUNT || a == VR_TARGET) {
        return 0;
    }
    if (value[0] >= '0' && value[0] <= '9') {
        m = (unsigned)strtoul(value, NULL, 0);
    } else if (strcmp(value, "none") != 0) {
        const char *p = value;
        while (*p) {
            size_t n = strcspn(p, "+ ");
            for (k = 0; k < WORDS; k++) {
                if (strlen(kWords[k].word) == n && strncmp(p, kWords[k].word, n) == 0) {
                    m |= kWords[k].bit;
                }
            }
            p += n;
            while (*p == '+' || *p == ' ') {
                p++;
            }
        }
    }
    if (!valid(a, m)) {
        return 0;
    }
    sOverride[a] = m;
    sOverSet |= 1u << a;
    if (sLoaded) {
        sBind[a] = m;
    }
    return 1;
}

/* ---- the pad (vita_gfx.c Port_PadRead, the handheld's buttons): what the bindings give, the buttons they took ---- */
static unsigned sSenzuReads; /* reads since the Senzu binding came down (L2 alone first, then L2 + SELECT) */
static int sSel;             /* SELECT: 0 up, 1 down alone and held back, 2 given to the game, 3 taken by a binding */
static unsigned long long sSelT0;
static int sSelPulse;
int gVitaRemapPadLog;

static int tag_fight(void) {
    extern int TagBattle_Active(void), Vita_InBattle(void);
    return TagBattle_Active() && Vita_InBattle();
}

/* *raw: the handheld's buttons (SceCtrl bits); the ones a binding takes are cleared, SELECT held back or let through
   as above. Returns the PS2 bits the bindings give. */
unsigned Vita_RemapPad(unsigned *raw) {
    unsigned r = *raw, out = 0, used = 0, selPairs = 0;
    int a, tag = -1, senzu = 0;

    load();
    for (a = 0; a <= VR_UNFUSE; a++) {
        unsigned m = sBind[a];
        if (m == 0 || bits(m) < 2) {
            continue;
        }
        selPairs |= m;
        if ((r & m) != m || (used & m & ~SCE_CTRL_SELECT)) {
            continue;
        }
        if (kAct[a].cx == CX_TAG) {
            if (tag < 0) {
                tag = tag_fight();
            }
            if (!tag) {
                continue;
            }
        }
        used |= m;
        if (a == VR_SENZU) {
            senzu = 1;
            out |= sSenzuReads >= 2 ? (P_L2 | P_SELECT) : P_L2;
        } else {
            out |= kAct[a].out;
        }
    }
    sSenzuReads = senzu ? sSenzuReads + 1 : 0;
    r &= ~(used & ~SCE_CTRL_SELECT);
    /* SELECT */
    if (!(r & SCE_CTRL_SELECT)) {
        if (sSel == 1) {
            sSelPulse = 2; /* (let go before its pair came: a press after all) */
        }
        sSel = 0;
    } else if (used & SCE_CTRL_SELECT) {
        sSel = 3;
    } else if (sSel == 0) {
        sSel = (selPairs & ~SCE_CTRL_SELECT) ? 1 : 2; /* (nothing to wait for: no pair bound) */
        sSelT0 = sceKernelGetProcessTimeWide();
    } else if (sSel == 1 && sceKernelGetProcessTimeWide() - sSelT0 >= SEL_WAIT_US) {
        sSel = 2;
    }
    r &= ~SCE_CTRL_SELECT;
    if (sSel == 2 || sSelPulse > 0) {
        r |= SCE_CTRL_SELECT;
        if (sSelPulse > 0) {
            sSelPulse--;
        }
    }
    if (gVitaRemapPadLog && (out || used)) {
        Vita_Log("vita: remap: pad 0x%x -> 0x%x, gives 0x%x\n", *raw, r, out);
    }
    *raw = r;
    return out;
}

/* vita_main.c: the binding of Quit / Unlock All held now (the modifiers L, R, START, SELECT not in it are up) */
static int capturing;
int Vita_RemapHeld(int a, unsigned b) {
    unsigned m = Vita_RemapGet(a);
    if (m == 0 || capturing) {
        return 0;
    }
    return (b & m) == m && (b & VB_MODS & ~m) == 0;
}

/* ---- the editor's capture (vita_guide.c): the next binding pressed on the handheld ---- */
int gVitaRemapTest; /* bt3.txt remaptest (development: the console runner taps, it cannot hold): while the editor
                       captures, the buttons are made up: 5 s none (the prompt), 0.5 s SELECT, 0.5 s SELECT + the button
                       remaptest names (SceCtrl bits, e.g. 4096 TRIANGLE), then none */
static unsigned sTestReads;
static unsigned raw_now(void) {
    SceCtrlData pad;
    if (gVitaRemapTest && capturing) {
        unsigned t = sTestReads++;
        return t < 300 ? 0 : t < 330 ? SCE_CTRL_SELECT : t < 360 ? (SCE_CTRL_SELECT | ((unsigned)gVitaRemapTest & VB_ALL)) : 0;
    }
    memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(0, &pad, 1);
    return pad.buttons & VB_ALL;
}

static unsigned sCapPrev, sCapSelOther;
static int sCapSelSeen, sCapArmed;

void Vita_RemapCaptureBegin(void) {
    capturing = 1;
    sTestReads = 0;
    sCapArmed = 0; /* (until every button is up: the CROSS that began it) */
    sCapSelSeen = 0;
    sCapSelOther = 0;
    sCapPrev = raw_now();
}

void Vita_RemapCaptureEnd(void) { capturing = 0; }
int Vita_RemapCapturing(void) { return capturing; }

/* every frame while capturing: VR_CAP_WAIT, VR_CAP_GOT (*m the buttons), VR_CAP_NONE (CIRCLE: none),
   VR_CAP_CANCEL (TRIANGLE), VR_CAP_ALONE (a button without SELECT: the editor says to hold SELECT) */
int Vita_RemapCapture(unsigned *m) {
    unsigned now = raw_now(), press = now & ~sCapPrev;
    int res = VR_CAP_WAIT;
    sCapPrev = now;
    if (!sCapArmed) {
        sCapArmed = now == 0;
        return VR_CAP_WAIT;
    }
    if (now & SCE_CTRL_SELECT) {
        sCapSelSeen = 1;
        if (press & ~SCE_CTRL_SELECT) {
            unsigned b = press & ~SCE_CTRL_SELECT;
            b &= ~(b - 1); /* (one: the lowest) */
            *m = SCE_CTRL_SELECT | b;
            res = VR_CAP_GOT;
        }
    } else if (sCapSelSeen) { /* SELECT let go alone */
        *m = SCE_CTRL_SELECT;
        res = VR_CAP_GOT;
    } else if (press & SCE_CTRL_CIRCLE) {
        *m = 0;
        res = VR_CAP_NONE;
    } else if (press & SCE_CTRL_TRIANGLE) {
        res = VR_CAP_CANCEL;
    } else if (press) {
        res = VR_CAP_ALONE;
    }
    if (res != VR_CAP_WAIT && res != VR_CAP_ALONE) {
        sCapSelSeen = 0;
    }
    return res;
}

/* every button up (the editor waits for it after a capture, so that its buttons do not reach the menus) */
int Vita_RemapAllUp(void) { return raw_now() == 0; }

/* ---- the words: a binding as the guide's marks ({SEL} + {X}) or as words (SELECT + Cross) ---- */
static const struct { unsigned bit; const char *mark, *en, *es; } kNames[] = {
    {SCE_CTRL_SELECT, "{SEL}", "SELECT", "SELECT"}, {SCE_CTRL_START, "{ST}", "START", "START"},
    {SCE_CTRL_LTRIGGER, "{L1}", "L", "L"}, {SCE_CTRL_RTRIGGER, "{R1}", "R", "R"},
    {SCE_CTRL_CROSS, "{X}", "Cross", "Equis"}, {SCE_CTRL_CIRCLE, "{O}", "Circle", "Círculo"},
    {SCE_CTRL_SQUARE, "{S}", "Square", "Cuadrado"}, {SCE_CTRL_TRIANGLE, "{T}", "Triangle", "Triángulo"},
    {SCE_CTRL_UP, "{U}", "Up", "Arriba"}, {SCE_CTRL_DOWN, "{D}", "Down", "Abajo"},
    {SCE_CTRL_LEFT, "{LT}", "Left", "Izquierda"}, {SCE_CTRL_RIGHT, "{RT}", "Right", "Derecha"},
};
#define NAMES ((int)(sizeof(kNames) / sizeof(kNames[0])))

/* SELECT first, START next, then the other: "{SEL} + {X}" / "SELECT + Cross"; "" for none */
int Vita_RemapMaskText(unsigned m, char *out, int size, int words) {
    int k, o = 0;
    out[0] = 0;
    for (k = 0; k < NAMES && o < size - 1; k++) {
        if (m & kNames[k].bit) {
            o += snprintf(out + o, (size_t)(size - o), "%s%s", o ? " + " : "",
                          words ? (gVitaBt4English ? kNames[k].en : kNames[k].es) : kNames[k].mark);
        }
    }
    return o;
}

/* An action's controls as the player has them now, for every text that shows them (the guide, the pause page, the
   reminder, the Tag legend, Vita Settings' lines): the binding, else the game's own chord. words: plain words (the
   Tag screens' TrueType lines), else the guide's marks. */
void Vita_RemapActionText(int a, char *out, int size, int words) {
    int en = gVitaBt4English;
    unsigned m = Vita_RemapGet(a);
    char b[64];
    out[0] = 0;
    if (a == VR_TARGET) {
        extern int PortInput_TagTargetMethod(int player, int method);
        int method = PortInput_TagTargetMethod(0, -1);
        snprintf(out, (size_t)size, "%s", method == 1 ? (en ? "Right stick" : "Stick derecho")
                                       : method == 2 ? (words ? (en ? "D-pad Left / Right" : "Cruceta izq. / der.") : (en ? "D-pad {LT} {RT}" : "Cruceta {LT} {RT}"))
                                                     : (words ? "L3" : "{L3}"));
        return;
    }
    if (m != 0) {
        Vita_RemapMaskText(m, b, sizeof(b), words);
        snprintf(out, (size_t)size, "%s", b); /* (Quit / Unlock All: the texts say "hold") */
        return;
    }
    switch (a) { /* none: the game's own chord (or nothing) */
    case VR_SENZU:
        snprintf(out, (size_t)size, words ? "L2 + SELECT" /* (the Tag legend's short lines) */
                                          : (en ? "Hold {L2} + press {SEL}" : "Mantén {L2} + pulsa {SEL}"));
        break;
    case VR_FUSION:
        snprintf(out, (size_t)size, words ? "R3" : "{R3}");
        break;
    case VR_UNFUSE:
        snprintf(out, (size_t)size, words ? "L3 + R3" : "{L3} + {R3}");
        break;
    case VR_L2: case VR_R2: case VR_L3: case VR_R3: { /* no pair: on the touch panels, or nowhere */
        extern int Vita_TouchGetSpot(int b);
        int sp = Vita_TouchGetSpot(a);
        snprintf(out, (size_t)size, "%s", sp == 0 || sp == 1 ? (en ? "touch only" : "solo táctil") : (en ? "none" : "ninguno"));
        break;
    }
    default:
        snprintf(out, (size_t)size, "%s", en ? "none" : "ninguno");
        break;
    }
}

/* Texts with %K:<name>% put in: that action's controls now (marks, or words with words=1); %KEYLINES% the guide's
   lines of the button pairs that give L2 / R2 / L3 / R3 ("{SEL} + {L1}   Same as {L2}"), or a line saying there are
   none. Returns out. */
char *Vita_RemapExpand(const char *in, char *out, int size, int words) {
    int o = 0, a;
    while (*in && o < size - 1) {
        if (in[0] == '%' && in[1] == 'K' && in[2] == ':') {
            const char *e = strchr(in + 3, '%');
            if (e != NULL) {
                char name[16], t[96];
                size_t n = (size_t)(e - (in + 3));
                snprintf(name, sizeof(name), "%.*s", (int)(n < 15 ? n : 15), in + 3);
                for (a = 0; a < VR_COUNT && strcmp(kAct[a].key, name) != 0; a++) {
                }
                if (a < VR_COUNT) {
                    Vita_RemapActionText(a, t, sizeof(t), words);
                    o += snprintf(out + o, (size_t)(size - o), "%s", t);
                    in = e + 1;
                    continue;
                }
            }
        }
        if (strncmp(in, "%TOUCHLINES%", 12) == 0) { /* the guide's lines: "{L2}   Rear pad, top left" (vita_touch.c spots) */
            extern int Vita_TouchGetSpot(int b);
            static const char *const kTm[4] = {"{L2}", "{R2}", "{L3}", "{R3}"};
            static const char *const kRearEn[4] = {"top left", "top right", "bottom left", "bottom right"};
            static const char *const kRearEs[4] = {"arriba a la izquierda", "arriba a la derecha", "abajo a la izquierda", "abajo a la derecha"};
            /* (the screen's corners: vita_touch.c corner_btn, L2 / R2 the lower ones, L3 / R3 the upper) */
            static const char *const kFrontEn[4] = {"bottom left", "bottom right", "top left", "top right"};
            static const char *const kFrontEs[4] = {"abajo a la izquierda", "abajo a la derecha", "arriba a la izquierda", "arriba a la derecha"};
            int b2, en = gVitaBt4English;
            for (b2 = 0; b2 < 4 && o < size - 1; b2++) {
                int sp = Vita_TouchGetSpot(b2);
                if (sp == 0) {
                    o += snprintf(out + o, (size_t)(size - o), "%s%s   %s %s", b2 ? "\n" : "", kTm[b2], en ? "Rear pad," : "Panel trasero,",
                                  (en ? kRearEn : kRearEs)[b2]);
                } else if (sp == 1) {
                    o += snprintf(out + o, (size_t)(size - o), "%s%s   %s %s", b2 ? "\n" : "", kTm[b2], en ? "Screen corner," : "Esquina de la pantalla,",
                                  (en ? kFrontEn : kFrontEs)[b2]);
                } else {
                    o += snprintf(out + o, (size_t)(size - o), "%s%s   %s", b2 ? "\n" : "", kTm[b2], en ? "Not on touch" : "Sin táctil");
                }
            }
            if (o >= size - 1) {
                o = size - 1;
            }
            in += 12;
            continue;
        }
        if (strncmp(in, "%TOUCHW%", 8) == 0) { /* where L2 / R2 / L3 / R3 are touched now, grouped: "L2 R2 L3 R3: rear pad" */
            extern int Vita_TouchGetSpot(int b);
            static const char *const kTb[4] = {"L2", "R2", "L3", "R3"};
            static const char *const kSpotEn[2] = {"rear pad", "screen corners"}, *const kSpotEs[2] = {"panel trasero", "esquinas"};
            int sp, any = 0;
            for (sp = 0; sp < 2; sp++) {
                int b2, n = 0;
                for (b2 = 0; b2 < 4; b2++) {
                    if (Vita_TouchGetSpot(b2) == sp) {
                        o += snprintf(out + o, (size_t)(size - o), "%s%s", n ? " " : any ? ", " : "", kTb[b2]);
                        n++;
                    }
                }
                if (n) {
                    o += snprintf(out + o, (size_t)(size - o), ": %s", (gVitaBt4English ? kSpotEn : kSpotEs)[sp]);
                    any = 1;
                }
                if (o >= size - 1) {
                    o = size - 1;
                }
            }
            if (!any) {
                o += snprintf(out + o, (size_t)(size - o), "%s", gVitaBt4English ? "Touch buttons: off" : "Botones táctiles: no");
            }
            if (o >= size - 1) {
                o = size - 1;
            }
            in += 8;
            continue;
        }
        if (strncmp(in, "%KEYLINES%", 10) == 0) {
            static const char *const kBtn[4] = {"{L2}", "{R2}", "{L3}", "{R3}"};
            int any = 0;
            for (a = VR_L2; a <= VR_R3 && o < size - 1; a++) {
                char t[64];
                unsigned m = Vita_RemapGet(a);
                if (m == 0) {
                    continue;
                }
                Vita_RemapMaskText(m, t, sizeof(t), 0);
                o += snprintf(out + o, (size_t)(size - o), "%s%s   %s %s", any ? "\n" : "", t,
                              gVitaBt4English ? "Same as" : "Igual que", kBtn[a]);
                any = 1;
            }
            if (!any) {
                o += snprintf(out + o, (size_t)(size - o), "%s", gVitaBt4English ? "None: Vita Guide > Vita Buttons" : "Ninguno: Guía Vita > Botones Vita");
            }
            in += 10;
            continue;
        }
        out[o++] = *in++;
    }
    out[o < size ? o : size - 1] = 0;
    return out;
}
