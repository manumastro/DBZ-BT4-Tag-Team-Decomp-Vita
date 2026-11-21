/*
 * BT4decomp on the PS Vita: what its PC build's own files give the game, where those files are not in the Vita build.
 *
 * Left out on the Vita, as online play was left out of the BT3 Vita port: online play and its lobbies, the online Tag
 * Team rooms (port/src/gs/tag_team_net.c; its local room, one player and CPUs, is port/vita/vita_tag.c), the F1
 * window (ui.cpp), voice chat, the screen reader, the script mods, online spectating, the PC renderer's own markers
 * and panels, mp3/wav songs and stages added from outside the disc, and the language choice (the Vita reads the one
 * disc in ux0:data/bt4: its screens in that disc's language, port/vita/vita_onlinelang.c).
 *
 * Each function answers as the game without these: no session, nothing added, the game's own settings. A value
 * that is not 0 says why. What none of these covers is a generated stub (port/build/gen/.../stubs.c) that stops the
 * game with its name if it is ever reached.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "plat_songs.h"
#include "plat_stages.h"
#ifdef BT4_VITA_NETLINK
#include "vita_netlink_rename.h" /* (upstream's network files linked: these stand-ins unused) */
#endif

/* ---- variables of the PC build's files ---- */
int gPortResim;                    /* gs/state.c: a frame run again after a rollback (never on the Vita) */
int gPortOverlayOpen;              /* gs/gs_input.c: the F1 window or the quit dialog has the input */
int gPortTagUiCaptureRequest;      /* gs/tag_team_net.c */
int gsSkipFrame;                   /* gs/gs_core.c on PC: a blank not drawn (read by fight_panels.c's side bars, not drawn here) */
volatile int gUiNameIdx, gUiNameReady, gUiSongIdx, gUiSongLit, gUiSongReady; /* gs/gs_draw.c: the PC's name plates */
volatile int gUiNameX, gUiNameY, gUiNameW, gUiNameH, gUiSongX, gUiSongY, gUiSongW, gUiSongH;
int gPortSongCount;                /* plat_songs.c: songs added from outside the disc (none) */
char gPortSongNames[PORT_SONG_MAX][64];
int gPortSongOffsets[PORT_SONG_MAX];
int gPortExtraStageCount;          /* plat_stages.c: stages added from outside the disc (none) */
int gPortExtraStages[PORT_STAGE_MAX];
char gPortStageNames[PORT_STAGE_MAX][64];

/* ---- the game's one double-precision call (plat_libm.c on PC: the replay list's slot number) ---- */
double Port_pow(double a, double b) { return __builtin_pow(a, b); }

/* ---- the list reader's (port/src/gs/gs_core.c is the Vita's own) ---- */
void Gs_RenderListWait(void) {}
const char *Port_GetEnv(const char *name) { extern char *Vita_GsGetenv(const char *name); return Vita_GsGetenv(name); }

/* ---- online play, lobbies, ranked matches, spectators (gs/net*.c, gs/match_supabase.cpp, spec_menu.c) ---- */
int Port_NetActive(void) { return 0; }
int Port_NetMe(void) { return 0; }
int Port_NetSession(void) { return 0; }
int Port_NetSpeedMilli(void) { return 1000; }
int Port_NetSpeedStandard(void) { return 1; }
int Port_NetWarp(void) { return 0; }
void Port_NetArrived(void) {}
void Port_NetRules(int *type, int *dp, int *time) { (void)type; (void)dp; (void)time; }
const char *Port_NetName(int player) { (void)player; return ""; }
void Port_NetNameSet(const char *name) { (void)name; }
void Port_NetOptions(int rollback, int delay) { (void)rollback; (void)delay; }
void Port_NetRulesSet(int type, int dp, int time) { (void)type; (void)dp; (void)time; }
int Port_NetSpecStatus(int *out) { (void)out; return 0; }
void Port_NetSpeedSet(int percent) { (void)percent; }
int Port_NetSpectating(void) { return 0; }
int Port_NetSpecView(void) { return -1; }
int Port_RollCan(void) { return 0; }
const char *Port_OnlineBlocked(void) { return "Online play is not available on the PS Vita."; }
void Port_OnlineNicknameBegin(void) {}
int Port_OnlineNicknameSubmit(const char *name) { (void)name; return 0; }
int Port_OnlineNicknameView(char *name) { (void)name; return 0; }
int Port_OnlinePresence(int active) { (void)active; return 0; }
int Port_RankedActive(void) { return 0; }
int Port_RankedPage(int page) { (void)page; return 0; }
void Port_RankedRefresh(int refreshStatus) { (void)refreshStatus; }
int Port_RankedSearch(int on) { (void)on; return 0; }
int Port_RankedTopTen(void *out) { (void)out; return 0; }
int Port_RankedView(void *view) { (void)view; return 0; }
void Port_LobbyCancel(void) {}
const char *Port_LobbyCode(void) { return ""; }
const char *Port_LobbyError(void) { return "Online play is not available on the PS Vita."; }
void Port_LobbyLaunch(void) {}
int Port_LobbyOther(void) { return 0; }
int Port_LobbyPoll(void) { return 0; }
void Port_LobbyQueuesAsk(void) {}
int Port_LobbyQueueWaiting(int queue) { (void)queue; return 0; }
int Port_LobbyRelease(int *min, int *latest) { (void)min; (void)latest; return 0; }
int Port_LobbyStartCode(int host, const char *code) { (void)host; (void)code; return 0; }
int Port_LobbyStartQuick(int queue) { (void)queue; return 0; }
int Port_TrLine(const char *original, char *out, int size) { (void)original; (void)out; (void)size; return 0; }
void Port_StateTouch(void *p, unsigned long n) { (void)p; (void)n; }

/* ---- the F1 window and the PC's presentation (gs/ui.cpp, gs/gs_draw.c) ---- */
int Port_HudLayout(void) { return 0; }
int Port_HudSwap(void) { return 0; }
/* the special moves' own cinematic cameras (on by default, as the game), the Tag Team screens' Camera mode row: as the
   PC's port/src/mods_game.c, saved as special_move_cameras; src/battle/btl_cam.c reads it */
int Port_SpecialMoveCameras(int change) {
    extern int Port_Setting(const char *, int);
    extern void Port_SettingSave(const char *, int), Port_SettingsWrite(void);
    static int enabled = -1;
    if (enabled < 0) {
        enabled = Port_Setting("special_move_cameras", 1) != 0;
    }
    if (change >= 0) {
        enabled = change != 0;
        Port_SettingSave("special_move_cameras", enabled);
        Port_SettingsWrite();
    }
    return enabled;
}
#ifndef BT4_VITA_NETLINK /* (a build with the network: vita_tag.c's, the rooms between Vitas) */
int Port_TagBattleRenderActive(void) { return 0; }
int Port_TagNetwork(void) { return 0; }
#endif
void Port_TagMenuSet(int a, int b) { (void)a; (void)b; }
void Port_UiSaving(int state) { (void)state; }
void Ui_LanguageChosen(const char *locale) { (void)locale; }
/* Which of Team BT4's two B14 Rev2 releases the disc is: 1 English, 0 Spanish. Vita_LoadGameData (vita_mem.c) sets it
   from the size of SLUS_219.78, as BT4decomp's own table of the two releases has them (port/include/
   bt4_online_compat.h: 2,115,696 bytes the English one's, 2,115,688 the Spanish one's). */
int gVitaBt4English = 1;
int Port_GameProfileEnglish(void) { return gVitaBt4English; }

/* ---- voice chat, the screen reader ---- */
int PortVoice_TestRun(void) { return 0; }
void PortVoice_World(int a, int b, int c, const float *d) { (void)a; (void)b; (void)c; (void)d; }
void PortA11y_Say(const char *text, int interrupt) { (void)text; (void)interrupt; }

/* ---- mp3 / wav / flac songs from outside the disc (gs/snd_ma.c, the PCM cache): none on the Vita ---- */
int PortMa_Format(const char *path) { (void)path; return 0; } /* (0: an ADX of the disc, played as always) */
int PortMa_Blanks(const char *path) { (void)path; return 0; }
int PortMa_AtEnd(void *handle) { (void)handle; return 1; }
void PortMa_Pause(void *handle, int paused) { (void)handle; (void)paused; }
void PortMa_SetMono(int mono) { (void)mono; }
void PortMa_SetPan(void *handle, int pan0, int pan1) { (void)handle; (void)pan0; (void)pan1; }
void PortMa_SetVolume(void *handle, float gain) { (void)handle; (void)gain; }
void PortMa_Stop(void *handle) { (void)handle; }
void *PortPcm_Open(const char *path, unsigned a, unsigned b, unsigned long long c) { (void)path; (void)a; (void)b; (void)c; return 0; }
unsigned PortPcm_Read(void *h, int16_t *out, unsigned n) { (void)h; (void)out; (void)n; return 0; }
int PortPcm_Seek(void *h, unsigned at) { (void)h; (void)at; return 0; }
void PortPcm_Close(void *h) { (void)h; }

/* ---- the PC build's other files: generated from their definitions, each answering as the game without them ---- */

/* (port/src/a11y_game.c) */
void PortA11y_FontText(int kind, int arg, int flags) {}
void PortA11y_StreamStart(const char *path) {}
/* The fighters' names, read once from a character select's own name and form texts (menu_c_e.c, menu_e_b.c), for
   the Tag Team fight's name over the target (port/src/fight_panels.c Port_TagActorName, src/battle/tag_team_hud.inc).
   a11y_game.c's cache (its chara_name, text_line, name_cache_fill), without the rest of the screen reader. */
#define FIGHTER_NAME_CACHE 512
static char sNameCache[FIGHTER_NAME_CACHE][64];
/* line n of one of the game's text files as UTF-8 (a table of byte offsets, entry 0 the count, to UTF-16 strings
   that begin with U+FEFF; the table ends where the first string starts), on one line: line breaks and spaces one
   space between words (a11y_game.c text_line with gs/a11y.c PortA11y_Utf8, without its translations). Its length;
   0 when it is not such a line. */
static int name_line(char *out, int cap, const uint32_t *file, int n) {
    const uint16_t *str;
    uint32_t ofs, c;
    int k = 0, i, space = 0, need;
    out[0] = '\0';
    if (file == 0 || n < 0 || n >= 0x1000 || (uint32_t)n + 1 >= file[1] >> 2) {
        return 0;
    }
    ofs = file[n + 1] >> 2 << 2;
    if (ofs == 0 || ofs >= 0x200000) {
        return 0;
    }
    str = (const uint16_t *)(const void *)((const uint8_t *)file + ofs);
    if (str[0] != 0xFEFF) {
        return 0;
    }
    for (i = 0; str[i] != 0 && i < 4096; i++) {
        c = str[i];
        if (c >= 0xD800 && c < 0xDC00 && str[i + 1] >= 0xDC00 && str[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (str[i + 1] - 0xDC00);
            i++;
        } else if (c == 0xFEFF || (c >= 0xD800 && c < 0xE000)) {
            continue;
        }
        if (c <= ' ') {
            space = k > 0;
            continue;
        }
        need = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
        if (k + space + need >= cap) {
            break;
        }
        if (space) {
            out[k++] = ' ';
            space = 0;
        }
        if (need == 1) {
            out[k++] = (char)c;
        } else if (need == 2) {
            out[k++] = (char)(0xC0 | c >> 6);
            out[k++] = (char)(0x80 | (c & 0x3F));
        } else if (need == 3) {
            out[k++] = (char)(0xE0 | c >> 12);
            out[k++] = (char)(0x80 | (c >> 6 & 0x3F));
            out[k++] = (char)(0x80 | (c & 0x3F));
        } else {
            out[k++] = (char)(0xF0 | c >> 18);
            out[k++] = (char)(0x80 | (c >> 12 & 0x3F));
            out[k++] = (char)(0x80 | (c >> 6 & 0x3F));
            out[k++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[k] = '\0';
    return k;
}
void Port_CacheFighterNames(void *names, void *forms) {
    const uint32_t *nameText = (const uint32_t *)names, *formText = (const uint32_t *)forms;
    int chara, lines = FIGHTER_NAME_CACHE;
    memset(sNameCache, 0, sizeof(sNameCache));
    if (nameText == 0) {
        return;
    }
    if (nameText[1] >> 2 != 0 && (int)(nameText[1] >> 2) - 1 < lines) {
        lines = (int)(nameText[1] >> 2) - 1;
    }
    for (chara = 0; chara < lines; chara++) {
        char name[64], form[64];
        size_t len;
        if (name_line(name, sizeof(name), nameText, chara) == 0) {
            continue;
        }
        if (name_line(form, sizeof(form), formText, chara) > 0 && strcmp(form, name) != 0) {
            len = strlen(name);
            snprintf(name + len, sizeof(name) - len, ", %s", form);
        }
        snprintf(sNameCache[chara], sizeof(sNameCache[chara]), "%s", name);
    }
}
int Port_CachedFighterName(int chara, char *out, int cap) {
    if (cap <= 0) {
        return 0;
    }
    out[0] = 0;
    if (chara < 0 || chara >= FIGHTER_NAME_CACHE || !sNameCache[chara][0]) {
        return 0;
    }
    snprintf(out, (size_t)cap, "%s", sNameCache[chara]);
    return 1;
}

/* (port/src/fight_panels.c is the PC's own now: the Tag Team fight's names and its allies' places, read by the HUD. Its
   side bars of a wide window are not drawn on the Vita; what they read is answered here) */
int PortMods_ViewActor(int side) { return side; } /* (port/src/mods_game.c: no script mods) */

/* (port/src/gs/a11y.c) */
float PortA11y_StreamGain(const void *player) { (void)player; return 1.0f; } /* the game's own volume (no accessibility options on the Vita) */

/* (port/src/gs/build_identity.c) */
void Port_DevRulesEncode(unsigned char out[8]) {}

/* (port/src/gs/game_profile.c) */
int Port_GameProfileAvailable(int language) { return language == gVitaBt4English; } /* (the one disc there is) */
int Port_GameProfileChoose(int english) { return 0; }
int Port_GameProfileLanguage(void) { return gVitaBt4English; /* 0 Spanish, 1 English (2, Brazilian Portuguese, is a PC pack) */ }
int Port_GameProfileMenuActive(void) { return 0; }
int Port_GameProfilePrompt(void) { return 0; }
void Port_GameProfileMenuSet(int active,int choice,int alpha) {}
int Port_GameProfilePortuguese(void) { return 0; } /* (the Brazilian Portuguese pack is the PC's) */
/* (Port_GameProfileAnimeMusicPath, the English profile's songs from the Spanish disc: vita_file.c) */

/* (port/src/gs/gs_draw.c) */
int Port_BoundaryDrawing(void) { return 0; }
int Port_FxOff(int bit) { return 0; }
void GsGpu_InterpPump(int ahead_ms, int all) {}
void Port_BoundaryBegin(void) {}
void Port_BoundaryEnd(void) {}
void Port_TagHudTrace(int actor,int front,int kind,const float *basis,const float *own,const float *pos,const float *head,const float *scr,const float *marker,const float *radar) {}
void Port_TagHudView(const float *v,const float *p,const float *nearZ) {}
void Port_WallMesh(const void *key, int second, int culled, const float *screen, const float *cam, const float *vtx, int quadCount, const void *quads, unsigned long long tex0) {}

/* (port/src/gs/gs_input.c; PortInput_TagTargetMethod is the Vita's own: vita_gfx.c) */
int PortInput_XboxGlyphs(int player) { (void)player; return 0; } /* (R1.20: Xbox prompts; the Vita's own buttons) */
int PortTbo_CommunityTake(void) { return 0; }
int PortTbo_TextRead(void) { return 0; }
int Port_RumbleTest(void) { return 0; }
void PortInput_Fresh(void) {}
void PortTbo_CommunityBounds(int active, float left, float top, float right, float bottom) {}
void PortTbo_Copy(const char *text) {}
void PortTbo_OpenCommunity(void) {}
void PortTbo_Paste(void) {}
void PortTbo_PasteText(void) {}
void PortTbo_TextMode(int on) {}
void Port_ClashLog(int player, const char *what, int fromInput, int total, int frame) {}
void Port_PadVibrate(int socket, int small, int large) {}

/* (port/src/gs/menu_text.c, menu_labels.c and tbo_text.c are the PC's own: BT4decomp's own screens' TrueType lines,
   port/vita/vita_tbo.c. The list of the menus' labels made from the disc is off, vita_gfx.c Vita_GsGetenv) */
/* (port/src/gs/menu_translation.c: the PC's menus in another language, from its translations folder. On the PC the
   Spanish and the English disc both leave it at "en", the disc's own text (game_profile.c Port_GameProfileChoose); only
   its Brazilian Portuguese pack has a catalog. The screens' Spanish is online_language.c's: port/vita/vita_onlinelang.c) */
void MenuTranslation_SaveChoice(const char *locale) {}
int MenuTranslation_Ensure(void) { return 0; }
int MenuTranslation_BootPending(void) { return 0; }
int MenuTranslation_Load(const char *outDir, char *why, size_t whyLen) { (void)outDir; if (whyLen) why[0] = 0; return 1; }
const char *MenuTranslation_Locale(void) { return 0; }
int MenuTranslation_NativeLabelsPath(char *out, size_t size) { (void)out; (void)size; return 0; }
const char *MenuTranslation_Text(const char *id) { (void)id; return 0; }
const char *MenuTranslation_Line(const char *original) { (void)original; return 0; }
const char *MenuTranslation_Original(const char *id) { (void)id; return 0; }
const char *MenuTranslation_Group(const char *id) { (void)id; return 0; }
const char *MenuTranslation_MainOriginal(void) { return 0; }
const char *MenuTranslation_ItemOriginal(void) { return 0; }
const char *const *MenuTranslation_StageNames(void) { return 0; }
const char *const *MenuTranslation_SongNames(void) { return 0; }
/* (port/src/gs/ui_text_extract.c: the European disc's languages, a PC setup's) */
int UiText_EuropeanOnStartup(const char *gameTextDir, char *why, size_t whyLen) { (void)gameTextDir; if (whyLen) why[0] = 0; return 0; }
/* (the PC's program folder, when its fonts are not found: the Vita's are app0:fonts) */
#include <sys/types.h>
ssize_t readlink(const char *path, char *buf, size_t size) { (void)path; (void)buf; (void)size; return -1; }

/* (port/src/gs/namefont.c) */
void Port_NameFontSheets(int stage, int song, int songLit) {}

/* (port/src/gs/net.c) */
const char * Port_LobbyRivalRegion(void) { return ""; }
const char * Port_NetLanAddress(void) { return ""; }
const char * Port_NetNameGet(void) { return ""; }
int Port_LobbyInBackground(void) { return 0; }
int Port_LobbyWiden(void) { return 0; }
int Port_NetDevRulesPad(int player,unsigned char out[8]) { return 0; }
int Port_NetFixedStart(void) { return 0; }
int Port_NetLobbyOwnerSide(void) { return -1; }
int Port_NetResim(void) { return 0; }
int Port_NetSessionsEnded(void) { return 0; }
int Port_NetSideOptions(int *camera, int *shake) { return 0; }
int Port_NetView(void) { return -1; }
int Port_NetVoiceAlt(void) { return 0; }
unsigned Port_NetSeed(void) { return 0; }
unsigned Port_NetTick(void) { return 0; }
void Port_LobbyBackground(int on) {}
void Port_LobbyQuickArea(const char *area) {}
void Port_NetInput(int player,unsigned char *data) {}
void Port_NetLeave(void) {}
void Port_NetNameSave(const char *name) {}
/* (BT4decomp 0.1.39.6: ranked, tournaments and the host's rules, answered as no session) */
void Port_NetAutomaticOptions(void) {}
int Port_NetItemsRestricted(void) { return 0; }
int Port_NetLongBeamClash(void) { return 0; }
int Port_NetRankedRequeueSupported(void) { return 0; }
void Port_NetRequestRankedRequeue(void) {}
void Port_NetRequestTournamentExit(void) {}
int Port_NetTakeRankedRequeue(void) { return 0; }
int Port_NetTakeTournamentExit(void) { return 0; }
int Port_NetTournamentResult(void) { return 0; }
/* (port/src/gs/match_supabase.cpp: the ranked stage's rock with a Dragon Ball, none offline) */
unsigned Port_RankedRockSeed(void) { return 0; }

/* (port/src/gs/net_lobby.c) */
int Port_LobbyCreate(int byCode, int port, int battle, int time, int speed, int rollback, int delay) { return 0; }
int Port_LobbyJoin(const char *address, int port) { return 0; }
int Port_LobbyView(void *out) { return 0; }
int Port_LobbyWantsScreen(void) { return 0; }
void Port_LobbyDo(int what, int a, int b) {}
void Port_LobbyReviewFixture(int entrants) {}
void Port_LobbySay(const char *text) {}

/* (port/src/gs/online_language.c is the PC's own: port/vita/vita_onlinelang.c) */

/* (port/src/gs/spectator_camera.c is the PC's own: port/vita/vita_spectator.c; its pad, vita_gfx.c) */

/* (port/src/gs/stage_art.c) */
unsigned Port_StageArt(int stage,int kind) { return 0; }

/* (port/src/gs/state.c: Port_StateLog is port/vita/vita_m0.c's) */

/* (port/src/gs/tag_team_net.c): the local room is port/vita/vita_tag.c */

/* (port/src/gs/tag_team_sync_client.inc) */
#ifndef BT4_VITA_NETLINK
void Port_TagHashActor(int actor,unsigned x,unsigned y,unsigned z,unsigned hp,unsigned ki,unsigned action,unsigned target,unsigned team,unsigned chara,unsigned costume) {}
#endif


/* (port/src/gs/ui.cpp) */
int Ui_QuitAsk(void) { return 0; }

/* (port/src/gs/ui_text_translate.c) */
const uint16_t * UiText_TranslateLine(const uint32_t *table, int line, const uint16_t *original) { (void)table; (void)line; return original; }
const uint16_t * UiText_TranslateString(const uint16_t *original) { return original; }

/* (port/src/mods_game.c) */
int Port_ModsCamCollide(int side) { return 0; }
int Port_ModsCamZoom(int side) { return 0; }
void Port_ModsCamera(int side, int objId, int pos, int rot, int paused) {}
void Port_NaturalFightCamera(int objId,int pos,int rot) {}
/* (BT4decomp 0.1.39.6: the Lua mods' split-screen presentation; no Lua mods on the Vita) */
int Port_PresentationSplitDrawing(int change) { return 0; }
int Port_PresentationSplitActor(int pane) { return pane != 0; }
/* (port/src/gs/mods.c) */
int Mods_SplitScreen(void) { return 0; }
int Port_ModernHudActive(void) { return 0; }
/* (the PC's Tag Team menu's film: Port_TagMenuMovieBegin is in port/vita/vita_tagmovie.c) */

/* (port/src/plat_fastvec.c) */
int Port_FastMtxApply(const uint32_t *r, const uint32_t *v, uint32_t *out) { return 0; }

/* (port/src/plat_fprint.c) */
const char * Port_DataFingerprintText(void) { return ""; }
int Port_DataFingerprint(unsigned long long *out) { return 0; }
int Port_DataFingerprintNeed(void) { return 0; }
int Port_DataFingerprintOriginal(void) { return 0; }

/* (port/src/plat_libm.c) */

/* (port/src/plat_songs.c) */
int PortSongs_IsFile(const char *path) { return 0; }
int Port_TestBgm(void) { return -1; }
void PortSongs_Init(void) {}
void PortSongs_Refresh(void) {}

/* (port/src/plat_stages.c) */
int Port_StageBufSize(void) { return 0x6CB800; /* the game's own stage buffer (no stages added from outside the disc) */ }
int Port_TestScreen(void) { return -1; }
int Port_TestStage(void) { return -1; }
void PortStages_Init(void) {}

/* (port/src/plat_verify.c) */
void Port_VerifyStart(void) {}

/* (port/src/quit_dialog.c) */
void PortQuit_Draw(int vsyncs) {}

/* (port/src/spec_menu.c) */
int PortSpecMenu_Frame(int vsyncs) { return 0; }
/* The port's own lines of the battle's pause and result menus (src/battle/hud_0_b.c BtlMenu_GetItemText, 0x6FF9..0x70FF):
   upstream's spec_menu.c makes them; its spectator menu is not on the Vita, its lines are. Without them a lobby's match
   ended in a crash on the result menu's "Back to the Lobby" (a NULL line measured: console run online_sess_s3). */
unsigned short *PortSpecMenu_Text(int k) {
    extern const char *PortOnline_Text(const char *text); /* vita_onlinelang.c */
    static unsigned short text[14][96];
    const char *s;
    char name[96];
    int i, n = 0;
    if (k < 0 || k >= 14) {
        k = 0;
    }
    switch (k) {
    case 7: s = "Watching"; break;
    case 8: s = "Back to the Lobby"; break;
    case 9: s = "Keep Searching"; break;
    case 10: s = "Congratulations!"; break;
    case 11:
#ifdef BT4_VITA_NETLINK
        {
            extern int Port_BattleOutcome(int *winner) __attribute__((weak));
            extern const char *Port_NetName(int player);
            int bits = 0;
            if (Port_BattleOutcome != 0) {
                Port_BattleOutcome(&bits);
            }
            snprintf(name, sizeof(name), "%s", Port_NetName((bits & 3) == 2 ? 1 : 0));
            s = name;
        }
#else
        s = "Winner";
#endif
        break;
    case 12: s = "Return to Tournament"; break;
    case 13: s = "Exit to Tenkaichi Budokai Online"; break;
    default: s = "Continue"; break;
    }
    if (k != 11) {
        s = PortOnline_Text(s);
    }
    for (i = 0; s != NULL && s[i] != 0 && n < 95; ) { /* (UTF-8 into the engine's 16-bit text, as spec_menu.c put_text) */
        unsigned c = (unsigned char)s[i++];
        if (c >= 0xC0 && c < 0xE0 && s[i]) {
            c = ((c & 0x1F) << 6) | ((unsigned char)s[i++] & 0x3F);
        } else if (c >= 0xE0 && c < 0xF0 && s[i] && s[i + 1]) {
            c = ((c & 0x0F) << 12) | (((unsigned char)s[i] & 0x3F) << 6) | ((unsigned char)s[i + 1] & 0x3F);
            i += 2;
        }
        text[k][n++] = (unsigned short)(c == '<' ? '(' : c == '>' ? ')' : c);
    }
    text[k][n] = 0;
    return text[k];
}
