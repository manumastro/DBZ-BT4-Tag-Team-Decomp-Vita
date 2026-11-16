/*
 * BT4decomp on the PS Vita: what its PC build's own files give the game, where those files are not in the Vita build.
 *
 * Left out on the Vita, as online play was left out of the BT3 Vita port: online play and its lobbies, the online Tag
 * Team rooms (port/src/gs/tag_team_net.c; its local room, one player and CPUs, is port/vita/vita_tag.c), the F1
 * window (ui.cpp), voice chat, the screen reader, the script mods, spectating, the PC renderer's own markers and
 * panels, mp3/wav songs and stages added from outside the disc, and the language choice (the Vita reads the one
 * disc in ux0:data/bt4/disc; English).
 *
 * Each function answers as the game without these: no session, nothing added, the game's own settings. A value
 * that is not 0 says why. What none of these covers is a generated stub (port/build/gen/.../stubs.c) that stops the
 * game with its name if it is ever reached.
 */
#include <math.h>
#include <stdint.h>
#include "plat_songs.h"
#include "plat_stages.h"

/* ---- variables of the PC build's files ---- */
int gPortResim;                    /* gs/state.c: a frame run again after a rollback (never on the Vita) */
int gPortOverlayOpen;              /* gs/gs_input.c: the F1 window or the quit dialog has the input */
int gPortTagUiCaptureRequest;      /* gs/tag_team_net.c */
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
int PortSpectator_Mode(void) { return 0; } /* the game's own camera */
void Port_StateTouch(void *p, unsigned long n) { (void)p; (void)n; }

/* ---- the F1 window and the PC's presentation (gs/ui.cpp, gs/gs_draw.c) ---- */
int Port_HudLayout(void) { return 0; }
int Port_HudSwap(void) { return 0; }
int Port_SpecialMoveCameras(int change) { (void)change; return 1; } /* the special moves' own cinematic cameras, as the game */
int Port_TagBattleRenderActive(void) { return 0; }
int Port_TagNetwork(void) { return 0; }
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
void Port_CacheFighterNames(void *names, void *forms) {} /* (the fighters' names for the PC's TrueType labels) */

/* (port/src/fight_panels.c) */
void Port_HudFeed(int kind, int side, int a, int b) {}
/* (BT4decomp 0.1.39.6: the Tag Team HUD's allies over their fighters, and the fighters' names over the target) */
void PortPanels_SetBattleActive(int active) {}
void Port_TagAllyHudFeed(int pane,int viewer,int actor,int x,int y) {}
void Port_TagAllyHudReset(void) {}
int Port_TagActorName(int actor,char *out,int cap) { return 0; } /* (no name: the PC writes it in TrueType) */

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
/* The English profile's songs from an installed Spanish profile (the anime soundtrack, F1 setting anime_music): the
   PC reads them from the Spanish install's extracted files (<root>/languages/es/pzs4us1/NNNNN.bin). The Vita has one
   disc, as an .iso or its files, and no second one: the disc's own song. */
int Port_GameProfileAnimeMusicPath(int partition,int file,char *path,int size) { (void)partition; (void)file; (void)path; (void)size; return 0; }

/* (port/src/gs/gs_draw.c) */
int Port_BoundaryDrawing(void) { return 0; }
int Port_FxOff(int bit) { return 0; }
void GsGpu_InterpPump(int ahead_ms, int all) {}
void Port_BoundaryBegin(void) {}
void Port_BoundaryEnd(void) {}
void Port_TagHudTrace(int actor,int front,int kind,const float *basis,const float *own,const float *pos,const float *head,const float *scr,const float *marker,const float *radar) {}
void Port_TagHudView(const float *v,const float *p,const float *nearZ) {}
void Port_WallMesh(const void *key, int second, int culled, const float *screen, const float *cam, const float *vtx, int quadCount, const void *quads, unsigned long long tex0) {}

/* (port/src/gs/gs_input.c) */
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
int PortInput_TagTargetMethod(int player, int method) { return 0; } /* (the PC's choice of L3, right stick or D-pad: the Vita's is L3) */
void Port_ClashLog(int player, const char *what, int fromInput, int total, int frame) {}
void Port_PadVibrate(int socket, int small, int large) {}

/* (port/src/gs/menu_text.c) */
void MenuText_BootLanguageChosen(void) {}
void MenuText_RegisterGlyphs(int tbp, int cbp, const void *glyphs, int count, int lineHeight) {}

/* (port/src/gs/menu_translation.c) */
void MenuTranslation_SaveChoice(const char *locale) {}
int MenuTranslation_Ensure(void) { return 0; } /* (no translated menus' TrueType lines on the Vita) */

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

/* (port/src/gs/online_language.c) */
const char * PortOnline_Text(const char *text) { return text; }
void PortOnline_Raw(int on) {}

/* (port/src/gs/spectator_camera.c) */
int PortSpectator_Camera(const float *native_eye,const float *native_rot,const float *targets,int count, float tan_y,float aspect,float *eye,float *rotation,float *zoom) { return 0; }
void PortSpectator_EndBattle(void) {}
void PortSpectator_ResolveEye(const float *eye) {}
void PortSpectator_SetInputActive(int active) {}
/* (its F1 page's settings, read and kept as on PC; the Vita has no spectator camera) */
int PortSpectator_Target(void) { return 0; }
int PortSpectator_Speed(void) { return 100; }
int PortSpectator_Zoom(void) { return 100; }
void PortSpectator_SetMode(int mode) {}
void PortSpectator_SetTarget(int target) {}
void PortSpectator_SetSpeed(int speed) {}
void PortSpectator_SetZoom(int zoom) {}
void PortSpectator_ResetCamera(void) {}

/* (port/src/gs/stage_art.c) */
unsigned Port_StageArt(int stage,int kind) { return 0; }

/* (port/src/gs/state.c) */
void Port_StateLog(unsigned vblank, const void *fight, unsigned fightSize) {}

/* (port/src/gs/tag_team_net.c): the local room is port/vita/vita_tag.c */

/* (port/src/gs/tag_team_sync_client.inc) */
void Port_TagHashActor(int actor,unsigned x,unsigned y,unsigned z,unsigned hp,unsigned ki,unsigned action,unsigned target,unsigned team,unsigned chara,unsigned costume) {}

/* (port/src/gs/tbo_text.c) */
int PortTbo_TextId(const char *text,int kind) { return 0; }
int PortTbo_TextIdLiteral(const char *text,int kind) { return 0; }
int PortTbo_TextInfo(int id, int what) { return 0; }
int PortTbo_TextMeasure(const char *text,int kind) { return 0; }
int PortTbo_TextMeasureLiteral(const char *text,int kind) { return 0; }
unsigned PortTbo_TextGeneration(void) { return 0; }

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
/* (port/src/gs/tag_menu_movie.c: the PC's Tag Team menu's film) */
void Port_TagMenuMovieBegin(void) {}

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
unsigned short * PortSpecMenu_Text(int k) { return 0; }
