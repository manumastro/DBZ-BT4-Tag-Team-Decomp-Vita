/*
 * PS Vita: the pace of the pictures and the controller.
 *
 * Vita_GsPresent / Vita_Pace decide which of the game's frames are drawn: all of them, unless the game runs late
 * for its vertical blanks (then up to five in a row are left out, so that the game itself keeps the console's
 * speed). Port_PadRead gives the Vita's buttons and sticks to the game as its first controller, and
 * Vita_GsGetenv answers the PC renderer's environment switches (gs_core.c and gs_vu1.c are compiled with getenv
 * renamed to it).
 */
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gVitaGsSkip;                 /* 1: do not draw the frame being read */
int gVitaGsSkipR;                /* the same for the list in hand (GPU build: set when the list is handed over) */

int gVitaGsFixedSkip = -1;       /* skip=<n> of bt3.txt, or -1: by the time drawing takes */
unsigned gVitaGsDrawn, gVitaGsLastDrawMs, gVitaGsSkipNow;

/* Called by gs_core.c at the end of every frame with the buffer the game shows. A frame is left undrawn only when the game is behind its vertical blanks.
   Vita_GsPresent (the end of every frame's list) counts; Vita_Pace (every vertical blank the game waits for, from
   vita_main.c, with the time the game was late for it: the last wait before a frame is the one that counts)
   decides about the frame to come. At most five frames in a row are left out. */
static int sSkipRun;

void Vita_GsPresent(uint32_t (*read)(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x, uint32_t y), uint32_t bp, uint32_t fbw, uint32_t psm) {
    static unsigned long long last;
    unsigned long long t = sceKernelGetProcessTimeWide();

    (void)read; (void)bp; (void)fbw; (void)psm;
    if (!gVitaGsSkipR) {
        gVitaGsDrawn++;
        gVitaGsLastDrawMs = last != 0 ? (unsigned)((t - last) / 1000) : 0;
        sSkipRun = 0;
    } else {
        sSkipRun++;
    }
    last = t;
}

static int sLateUs, sSkipRunSeen;
/* async: the list is drawn on the render thread while the game goes on (a short wait for it is not "late") */
/* (drawn on another processor core, a picture costs the game nothing: whether one is drawn is decided by whether
   the render thread is free, in gs_core.c, not by the game being late) */
int Vita_SkipWanted(int async) { return async && gVitaGsFixedSkip < 0 ? 0 : gVitaGsSkip; }

void Vita_Pace(int lateUs) {
    sLateUs = lateUs;
    sSkipRunSeen = sSkipRun;
    extern int gVglSafe;
    /* (with the pictures put up in step with the game's clock, vita_vgl.c, a list given a little late is still shown
       on time: only a game more than 10 ms late leaves one out) */
    gVitaGsSkip = gVitaGsFixedSkip >= 0 ? sSkipRun < gVitaGsFixedSkip : lateUs > ((gVglSafe & 134217728) ? 4000 : 10000) && sSkipRun < 5;
    gVitaGsSkipNow = (unsigned)sSkipRun;
}

/* ---- the pads: the Vita's buttons and sticks as the game's first controller, and a second controller (a PS TV's
   second DualShock, or a pad paired to a Vita) as the game's second (the libpad2 packet of port/src/gs/gs_input.c:
   two bytes of buttons, active low, then right x / y, left x / y). The handheld has no L2 / R2 / L3 / R3: they are
   SELECT + L / R, or the touch panels (vita_touch.c); a DualShock has them. ---- */
enum { B_SELECT = 0x0001, B_L3 = 0x0002, B_R3 = 0x0004, B_START = 0x0008, B_UP = 0x0010, B_RIGHT = 0x0020, B_DOWN = 0x0040,
       B_LEFT = 0x0080, B_L2 = 0x0100, B_R2 = 0x0200, B_L1 = 0x0400, B_R1 = 0x0800, B_TRIANGLE = 0x1000, B_CIRCLE = 0x2000,
       B_CROSS = 0x4000, B_SQUARE = 0x8000 };

extern int Port_Setting(const char *name, int def);
extern void Port_SettingSave(const char *name, int value);
extern void Port_SettingsWrite(void);

/* Which of the system's controller ports each player reads (after the PS TV support of limeking614-dev's fork).
   Port 0 is the handheld's own buttons (type PHY), or on a PS TV a virtual pad that merges every DualShock (VIRT);
   ports 1..4 are the paired DualShocks. Player 1: the handheld's buttons, or on a PS TV the first DualShock (not the
   merged pad, which would give the second player's presses to the first too). Player 2: the next DualShock, when
   the setting "pad2" (Options > Controller > Vita Settings, on unless set) allows it. Asked of the system about twice
   a second. */
static int sPort[2] = {0, -1};
static unsigned char sPortType[2] = {SCE_CTRL_TYPE_PHY, 0};
static int sPad2 = -1; /* the setting */
int gVitaPad2Fake; /* (development build, the test runs' split-screen fights: bt3.txt demosplit=1) a second controller
                      that nobody holds, when there is none */

int Vita_Pad2Setting(void) {
    if (sPad2 < 0) {
        sPad2 = Port_Setting("pad2", 1) != 0;
    }
    return sPad2;
}

void Vita_Pad2SettingSet(int on) {
    sPad2 = on != 0;
    Port_SettingSave("pad2", sPad2);
    Port_SettingsWrite();
}

static void ports_update(void) {
    static int countdown;
    SceCtrlPortInfo info;
    int k, n = 0, found[2] = {-1, -1};
    unsigned char type[2] = {0, 0};

    if (countdown-- > 0) {
        return;
    }
    countdown = 30;
    memset(&info, 0, sizeof(info));
    if (sceCtrlGetControllerPortInfo(&info) < 0) {
        info.port[0] = SCE_CTRL_TYPE_PHY;
    }
    for (k = 0; k < 5 && n < 2; k++) {
        if (info.port[k] == SCE_CTRL_TYPE_UNPAIRED || info.port[k] == SCE_CTRL_TYPE_VIRT) {
            continue; /* (the PS TV's merged pad: not a player's) */
        }
        found[n] = k;
        type[n++] = info.port[k];
    }
    if (found[0] < 0) { /* (nothing paired that is a player's: the merged pad, or port 0 as before) */
        found[0] = 0;
        type[0] = info.port[0] == SCE_CTRL_TYPE_VIRT ? SCE_CTRL_TYPE_VIRT : SCE_CTRL_TYPE_PHY;
    }
    if (found[0] != sPort[0] || found[1] != sPort[1]) {
        extern void Vita_Log(const char *fmt, ...);
        Vita_Log("pads: player 1 on port %d (type %d), player 2 %s%d (type %d); ports %d %d %d %d %d\n", found[0], type[0],
                 found[1] < 0 ? "none " : "on port ", found[1], type[1], info.port[0], info.port[1], info.port[2], info.port[3], info.port[4]);
    }
    sPort[0] = found[0];
    sPort[1] = found[1];
    sPortType[0] = type[0];
    sPortType[1] = type[1];
}

/* The second controller is there (the game's socket 1: scePad2GetState, port/src/plat_stub.c). */
int Vita_Pad2Present(void) {
    ports_update();
    return (sPort[1] >= 0 && Vita_Pad2Setting()) || gVitaPad2Fake;
}

/* ---- Clashes (setting clash_square: on unless set to 0; Options > Controller > Vita Settings > Clashes).
   BT4 counts presses in a clash, where BT3 counted newly pressed directions: in a beam or rush struggle (actions
   0x130..0x132, BtlAct_ClashStruggleHandler, src/battle/btl_act_f.c) each press of Ki Blast (Triangle; input 0x50),
   in a clash of blows (actions 0xFA..0xFC, BtlAct_ClashBlowsHandler, btl_act_e.c) each press of Fight (Square;
   input 0x4F). The BT3 port's version of this option took Square off the pad in a clash and gave newly pressed
   directions for it: in BT4 that lost the Square presses of a clash of blows and counted nothing in a struggle.
     Square (on): while this player's fighter is in a clash, a press of Square is a press of Triangle as well, so
       that Square wins both kinds, as the prompt then shows (src/battle/hud_a.c); Triangle works as it does.
     Stick (off): BT4decomp's PC option clash_stick (port/src/gs/gs_input.c): each time the left stick, pushed far,
       comes into another of its four quarters, a press of Square and Triangle, released the frame after; turning
       the stick round gives four a turn. The buttons go on working beside it.
   (A Custom control type that puts Fight or Ki Blast on other buttons: these are still Square and Triangle.)
   The game sees an ordinary pad. */
static int sClashSquare = -1;

int Port_ClashSquare(void) {
    if (sClashSquare < 0) {
        sClashSquare = Port_Setting("clash_square", 1) != 0;
    }
    return sClashSquare;
}

void Port_ClashSquareSet(int on) {
    sClashSquare = on != 0;
    Port_SettingSave("clash_square", sClashSquare);
    Port_SettingsWrite();
}

/* Clash assist (setting clash_assist: off unless set to 1; Vita Settings > Clashes, ON / OFF): in a clash, Square
   or Triangle held down is pressed again for the player every CLASH_ASSIST_STEP frames (the pad is read once a
   step of the fight, 30 a second: ten presses a second, a fast player's pace; the game takes a press only after a
   frame with the button up). The first frame held is the player's own press. */
#define CLASH_ASSIST_STEP 3
static int sClashAssist = -1;

int Vita_ClashAssist(void) {
    if (sClashAssist < 0) {
        sClashAssist = Port_Setting("clash_assist", 0) != 0;
    }
    return sClashAssist;
}

void Vita_ClashAssistSet(int on) {
    sClashAssist = on != 0;
    Port_SettingSave("clash_assist", sClashAssist);
    Port_SettingsWrite();
}

/* the fighter this socket plays, NULL outside a fight: the side's in a fight of two, in a Tag Team fight the
   player's own (socket 0; the other players there are the computer) */
static void *clash_fighter(int socket) {
    extern void *BtlChar_FindByObjId(int objId), *BtlChar_FindBySide(int side); /* the game */
    extern int TagBattle_Active(void), TagBattle_LocalActor(void);
    if (TagBattle_Active()) {
        return socket == 0 ? BtlChar_FindBySide(TagBattle_LocalActor()) : NULL;
    }
    return BtlChar_FindByObjId(socket);
}

/* rawX / rawY: the left stick as read, before stick_shape (below) */
static void clash_pad(int socket, unsigned *btn, unsigned char rawX, unsigned char rawY) {
    extern int BtlAct_GetCurrent(void *chr); /* the game: the fighter's action */
    extern int gVitaInMenus;
    static int held[2][2], sector[2] = {-1, -1}, pulse[2];
    static const unsigned kMash[2] = {B_SQUARE, B_TRIANGLE};
    int in = 0, k, x = (int)rawX - 128, y = (int)rawY - 128, next = -1;
    void *chr;

    if (!gVitaInMenus && (chr = clash_fighter(socket)) != NULL) {
        int action = BtlAct_GetCurrent(chr);
        in = (action >= 0x130 && action <= 0x132) || action == 0xFA || action == 0xFB || action == 0xFC;
    }
    if (!in) {
        held[socket][0] = held[socket][1] = 0;
        sector[socket] = -1;
        pulse[socket] = 0;
        return;
    }
    if (Vita_ClashAssist()) { /* held: down one frame in CLASH_ASSIST_STEP, up the others */
        for (k = 0; k < 2; k++) {
            if (!(*btn & kMash[k])) {
                held[socket][k] = 0;
            } else if (held[socket][k]++ % CLASH_ASSIST_STEP != 0) {
                *btn &= ~kMash[k];
            }
        }
    }
    if (Port_ClashSquare()) {
        if (*btn & B_SQUARE) {
            *btn |= B_TRIANGLE;
        }
        return;
    }
    if (x > 80 || x < -80 || y > 80 || y < -80) { /* the quarter the stick is pushed far into */
        next = (x < 0 ? -x : x) > (y < 0 ? -y : y) ? (x > 0 ? 1 : 3) : (y > 0 ? 2 : 0);
    }
    if (pulse[socket]) { /* (the frame after a press: the buttons up, so that the next one is a press) */
        pulse[socket] = 0;
        return;
    }
    if (next >= 0 && next != sector[socket]) {
        *btn |= B_SQUARE | B_TRIANGLE;
        pulse[socket] = 1;
    }
    sector[socket] = next;
}

/* ---- The sticks' dead zone and response (settings stick_deadzone, 0..40 percent in steps of 10, and stick_curve,
   0 or 1; 0 and 0 unless set: the sticks as they are; Vita Settings > Stick). The game gives each axis a dead zone of
   its own, 50 of 127.5 (39%, src/sys/pad.c), and takes a direction (the menus, dashes) when an axis is past 0.5 of
   what is left. This makes the stick less quick on top of that: with m the stick's distance from the centre as the
   larger of its two axes (0..1, so that the eight full tilts stay as they are), nothing below the dead zone d, then
       out = (m - d) / (1 - d),   squared with stick_curve=1 (half way out gives a quarter)
   in the stick's own direction. Both sticks; in the clashes the Stick option reads the stick as it is. ---- */
static int sStickDz = -1, sStickCurve = -1;

int Vita_StickDeadzone(void) {
    if (sStickDz < 0) {
        int d = Port_Setting("stick_deadzone", 0);
        sStickDz = d < 0 ? 0 : d > 40 ? 40 : d / 10 * 10;
        sStickCurve = Port_Setting("stick_curve", 0) != 0;
    }
    return sStickDz;
}

int Vita_StickCurve(void) {
    Vita_StickDeadzone();
    return sStickCurve;
}

void Vita_StickSet(int deadzone, int curve) {
    sStickDz = deadzone < 0 ? 0 : deadzone > 40 ? 40 : deadzone / 10 * 10;
    sStickCurve = curve != 0;
    Port_SettingSave("stick_deadzone", sStickDz);
    Port_SettingSave("stick_curve", sStickCurve);
    Port_SettingsWrite();
}

static void stick_shape(unsigned char *bx, unsigned char *by) {
    int dz = Vita_StickDeadzone();
    float x, y, m, d, out, s;
    int nx, ny;

    if (dz == 0 && !sStickCurve) {
        return; /* (the bytes as they are) */
    }
    x = (float)*bx - 127.5f;
    y = (float)*by - 127.5f;
    m = ((x < 0.0f ? -x : x) > (y < 0.0f ? -y : y) ? (x < 0.0f ? -x : x) : (y < 0.0f ? -y : y)) / 127.5f;
    d = (float)dz / 100.0f;
    if (m <= d || m <= 0.0f) {
        *bx = *by = 0x80;
        return;
    }
    out = ((m > 1.0f ? 1.0f : m) - d) / (1.0f - d);
    if (sStickCurve) {
        out *= out;
    }
    s = out / m;
    nx = (int)(127.5f + x * s + 0.5f);
    ny = (int)(127.5f + y * s + 0.5f);
    *bx = (unsigned char)(nx < 0 ? 0 : nx > 255 ? 255 : nx);
    *by = (unsigned char)(ny < 0 ? 0 : ny > 255 ? 255 : ny);
}

/* ---- Tag Team's target choice (BT4decomp's Camera mode page, "Target selection": src/menu/port_tbo.c), as the PC's
   gs_input.c: 0 L3 moves to the next enemy (the Vita's L3: the rear touch pad or a front corner, vita_touch.c), 1 the
   right stick's push (up, down, left, right: the enemy that way; the stick does not turn the camera then), 2 the
   D-pad's left / right (not while L2, L3 or R3 is held: transformations and techniques). Saved as the PC saves it
   (tag_target_method_p1 / _p2). The pad report's byte 17 carries it to the game (src/battle/btl_input.c
   BtlInput_Sample): 0x80 | the direction (1 up, 2 down, 3 left, 4 right) | the method << 4, in a Tag Team fight. ---- */
static int sTagTargetMethod[2] = {-1, -1}, sTargetLatch[2];
static unsigned sTagTargetDpadHeld[2];
int PortInput_TagTargetMethod(int player, int method) {
    char key[40];
    if (player < 0 || player > 1) {
        return 0;
    }
    if (sTagTargetMethod[player] < 0) {
        int previous;
        snprintf(key, sizeof(key), "target_stick_p%d", player + 1);
        previous = Port_Setting("tag_target_dpad", 0) ? 2 : Port_Setting(key, 0) ? 1 : 0;
        snprintf(key, sizeof(key), "tag_target_method_p%d", player + 1);
        sTagTargetMethod[player] = Port_Setting(key, previous);
        if (sTagTargetMethod[player] < 0 || sTagTargetMethod[player] > 2) {
            sTagTargetMethod[player] = 0;
        }
    }
    if (method >= 0 && method <= 2) {
        sTagTargetMethod[player] = method;
        snprintf(key, sizeof(key), "tag_target_method_p%d", player + 1);
        Port_SettingSave(key, method);
        snprintf(key, sizeof(key), "target_stick_p%d", player + 1);
        Port_SettingSave(key, method == 1);
        sTargetLatch[player] = 0;
        sTagTargetDpadHeld[player] = 0;
        Port_SettingsWrite();
    }
    return sTagTargetMethod[player];
}
static float tag_axis(unsigned char raw) { /* (gs_input.c game_axis: the game's dead zone) */
    float value = (float)raw - 127.5f;
    if (value > 0.0f) {
        value = value > 50.0f ? value - 50.0f : 0.0f;
    } else {
        value = value < -50.0f ? value + 50.0f : 0.0f;
    }
    return value / (127.5f - 50.0f);
}
/* ---- BT4decomp's free camera (Camera mode > Camera mode: Free camera, Follow fighter, Fight overview), upstream's
   gs/spectator_camera.c built for the Vita (port/vita/vita_spectator.c). As the PC's gs_input.c: while it is on in a
   fight, player 1's pad is the camera's (spectator_camera.c PortSpectator_ConsumesInput): the game gets START and SELECT
   only, sticks centred, no target choice; PortInput_Spectator reads the pad as it was. The PC's pad gives: the left stick
   moves (Free camera) or turns and comes closer / goes back (Follow fighter, Fight overview), the right stick turns,
   R1 / L1 up / down, the triggers R2 / L2 zoom in / out (held), L3 three times as fast. The handheld's R2 / L2 / L3 are as
   in a fight: SELECT + R / L, or the touch panels (vita_touch.c). bt3.txt tagcam=0: none of it (vita_spectator.c) ---- */
extern int gVitaTagCam;
extern int PortSpectator_ConsumesInput(void);
extern unsigned PortSpectator_FilterButtons(unsigned buttons);
static struct { unsigned btn; unsigned char lx, ly, rx, ry; } sSpecPad = {0, 0x80, 0x80, 0x80, 0x80};
static int spec_consumes(void) {
    return gVitaTagCam && PortSpectator_ConsumesInput();
}
static void spec_stick(unsigned char bx, unsigned char by, float out[2]) { /* (gs_input.c game_stick) */
    float len;
    out[0] = tag_axis(bx);
    out[1] = tag_axis(by);
    len = out[0] * out[0] + out[1] * out[1];
    if (len > 1.0f) {
        len = 1.0f / sqrtf(len);
        out[0] *= len;
        out[1] *= len;
    }
}
/* (spectator_camera.c PortSpectator_Camera, the game's thread): right, forward, up, yaw, pitch, zoom, boost */
void PortInput_Spectator(float *axes) {
    float l[2], r[2];
    unsigned btn = sSpecPad.btn;
    int i;
    for (i = 0; i < 7; i++) {
        axes[i] = 0.0f;
    }
    if (!gVitaTagCam) {
        return;
    }
    spec_stick(sSpecPad.lx, sSpecPad.ly, l);
    spec_stick(sSpecPad.rx, sSpecPad.ry, r);
    axes[0] = l[0];
    axes[1] = -l[1];
    axes[3] = r[0];
    axes[4] = -r[1];
    axes[2] = (float)((btn & B_R1) != 0) - (float)((btn & B_L1) != 0);
    axes[5] = (float)((btn & B_R2) != 0) - (float)((btn & B_L2) != 0);
    axes[6] = (btn & B_L3) != 0;
}

static void tag_target(int socket, unsigned btn, unsigned char *data) {
    extern int TagBattle_Active(void), Vita_InBattle(void);
    int method, tag = TagBattle_Active() && Vita_InBattle() && !spec_consumes();
    if (!tag) {
        sTargetLatch[socket] = 0;
        sTagTargetDpadHeld[socket] = 0;
        return;
    }
    method = PortInput_TagTargetMethod(socket, -1);
    if (method == 1) {
        float x = tag_axis(data[2]), y = tag_axis(data[3]);
        int dir;
        if ((x < 0 ? -x : x) > (y < 0 ? -y : y)) {
            dir = x > 0.5f ? 4 : x < -0.5f ? 3 : 0;
        } else {
            dir = y > 0.5f ? 2 : y < -0.5f ? 1 : 0;
        }
        data[17] = (unsigned char)(0x80 | (dir && !sTargetLatch[socket] ? dir : 0));
        sTargetLatch[socket] = dir != 0;
        data[2] = data[3] = 0x80;
    } else {
        sTargetLatch[socket] = 0;
    }
    if (method == 2) {
        unsigned held = btn & (B_LEFT | B_RIGHT), pressed = held & ~sTagTargetDpadHeld[socket];
        if (!(data[17] & 7) && !(btn & (B_L2 | B_L3 | B_R3))) {
            if (held == B_LEFT && pressed == B_LEFT) data[17] = 0x80 | 3;
            if (held == B_RIGHT && pressed == B_RIGHT) data[17] = 0x80 | 4;
        }
        sTagTargetDpadHeld[socket] = held;
    } else {
        sTagTargetDpadHeld[socket] = 0;
    }
    data[17] |= (unsigned char)(method << 4);
}

int Port_PadRead(int socket, unsigned char *data) {
    static const struct { unsigned vita, ps2; } map[] = {
        {SCE_CTRL_START, B_START}, {SCE_CTRL_UP, B_UP}, {SCE_CTRL_RIGHT, B_RIGHT}, {SCE_CTRL_DOWN, B_DOWN}, {SCE_CTRL_LEFT, B_LEFT},
        {SCE_CTRL_TRIANGLE, B_TRIANGLE}, {SCE_CTRL_CIRCLE, B_CIRCLE}, {SCE_CTRL_CROSS, B_CROSS}, {SCE_CTRL_SQUARE, B_SQUARE}};
    SceCtrlData pad;
    unsigned btn = 0, i;
    int ds;

    if (socket < 0 || socket > 1) {
        return 0;
    }
    ports_update();
    if (socket == 1 && !Vita_Pad2Present()) {
        return 0;
    }
    memset(&pad, 0, sizeof(pad));
    pad.lx = pad.ly = pad.rx = pad.ry = 0x80;
    ds = sPortType[socket] == SCE_CTRL_TYPE_DS3 || sPortType[socket] == SCE_CTRL_TYPE_DS4;
    if (socket == 1 && sPort[1] < 0) { /* (gVitaPad2Fake: nothing pressed) */
        ds = 0;
    } else if (ds) { /* a DualShock: L1 / R1 and L2 / R2 / L3 / R3 of its own */
        sceCtrlPeekBufferPositiveExt2(sPort[socket], &pad, 1);
    } else { /* the handheld's buttons (L / R are its triggers' bits), or the PS TV's merged pad as before */
        sceCtrlPeekBufferPositive(0, &pad, 1);
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (pad.buttons & map[i].vita) {
            btn |= map[i].ps2;
        }
    }
    if (ds) {
        if (pad.buttons & SCE_CTRL_L1) { btn |= B_L1; }
        if (pad.buttons & SCE_CTRL_R1) { btn |= B_R1; }
        if (pad.buttons & SCE_CTRL_L2) { btn |= B_L2; }
        if (pad.buttons & SCE_CTRL_R2) { btn |= B_R2; }
        if (pad.buttons & SCE_CTRL_L3) { btn |= B_L3; }
        if (pad.buttons & SCE_CTRL_R3) { btn |= B_R3; }
        if (pad.buttons & SCE_CTRL_SELECT) { btn |= B_SELECT; }
    } else if (pad.buttons & SCE_CTRL_SELECT) { /* SELECT + L / R = L2 / R2; SELECT alone = SELECT */
        if (pad.buttons & SCE_CTRL_LTRIGGER) { btn |= B_L2; }
        if (pad.buttons & SCE_CTRL_RTRIGGER) { btn |= B_R2; }
        if (!(pad.buttons & (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER))) { btn |= B_SELECT; }
    } else {
        if (pad.buttons & SCE_CTRL_LTRIGGER) { btn |= B_L1; }
        if (pad.buttons & SCE_CTRL_RTRIGGER) { btn |= B_R1; }
    }
    if (socket == 0) { /* the touch panels: L2 / R2 on the rear pad or the front corners, the menus on the screen (vita_touch.c) */
        extern unsigned Vita_TouchPad(void);
        btn |= Vita_TouchPad();
    }
    clash_pad(socket, &btn, pad.lx, pad.ly);
    stick_shape(&pad.lx, &pad.ly);
    stick_shape(&pad.rx, &pad.ry);
    if (socket == 0) { /* (the free camera: the pad as it was for it, then the game's part of it, as gs_input.c) */
        sSpecPad.btn = btn;
        sSpecPad.lx = pad.lx;
        sSpecPad.ly = pad.ly;
        sSpecPad.rx = pad.rx;
        sSpecPad.ry = pad.ry;
        if (spec_consumes()) {
            btn = PortSpectator_FilterButtons(btn);
            pad.lx = pad.ly = pad.rx = pad.ry = 0x80;
        }
    }
    memset(data, 0, 18);
    data[0] = (unsigned char)(~btn & 0xFF);
    data[1] = (unsigned char)(~(btn >> 8) & 0xFF);
    data[2] = pad.rx; data[3] = pad.ry; data[4] = pad.lx; data[5] = pad.ly;
    tag_target(socket, btn, data);
    return 18;
}

/* ---- the renderer's switches: gs_core.c and gs_vu1.c ask the environment in their innermost loops (developer
   switches of the PC build). Here every such question is answered at once: the GPU renderer is on
   (BT3_GS=gpu), the PC's developer switches are off. build.py compiles the two files with getenv renamed to this. ---- */
char *Vita_GsGetenv(const char *name) {
    if (name[4] == 'V' && strcmp(name, "BT3_VU_INTERP") == 0) { /* the interpreter unless vita_vgl.c has its shader */
        extern int gVglVuReady;
        return gVglVuReady ? NULL : "1";
    }
    if (name[4] == 'N' && strcmp(name, "BT3_NOSOUND") == 0) {
        extern int gVglSafe;
        return (gVglSafe & 1024) ? "1" : NULL;
    }
    if (name[4] == 'S' && strcmp(name, "BT3_SND_VERBOSE") == 0) {
        /* (off) every sound effect and stream start was printed into err-vgl.txt, unbuffered, while the
           sound's lock was held: a few writes to the memory card per effect, which held up the sound thread and the
           game's thread (Snd_Update up to 211 ms a frame, the picture rate down to 3 a second) */
        return NULL;
    }
    if (name[4] == 'G' && strcmp(name, "BT3_GS_THREAD") == 0) { /* lists on a thread of their own (TRIANGLE: not) */
        extern int gVglSafe;
        return (gVglSafe & 512) ? NULL : "1";
    }
    /* BT4decomp's TrueType lines of its own screens (gs/menu_text.c, gs/tbo_text.c; port/vita/vita_tbo.c): the fonts
       are in the package (fonts/, with fonts.tsv), and the list of the menus' labels made from the disc (the PC's
       "Menu font", its translated menus) is off: an empty name switches it off there, so no thread reads the disc */
    if (name[4] == 'F' && strcmp(name, "BT3_FONTS") == 0) {
        return "app0:fonts";
    }
    if (name[4] == 'M' && strcmp(name, "BT3_MENU_FONT") == 0) {
        return "";
    }
    return strcmp(name, "BT3_GS") == 0 ? "gpu" : NULL;
}

