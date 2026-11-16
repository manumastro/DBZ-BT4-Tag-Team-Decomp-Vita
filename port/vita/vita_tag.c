/*
 * Tag Team on the PS Vita: the local room only (one player and CPU fighters, offline). GPL-3.0-or-later, as
 * port/src/tag_team.c, whose room model (Tt_*) this drives.
 *
 * BT4decomp's port/src/gs/tag_team_net.c holds the local room together with the online ones (TCP, WebSocket,
 * WebRTC through libdatachannel, SDL threads), so it is not built for the Vita. What follows is its local_room
 * branches, as they are there: Port_TagConnect(NULL) opens a room in this process, every change goes straight to
 * the room model, and the network's calls answer as no network (Port_TagNetwork 0). The battle side
 * (src/battle/tag_team_*.inc, battle_work.c TagBattle_ConfigureLocal) reads the room through these functions.
 * The plan: docs/vita/tagteam.md.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <psp2/kernel/processmgr.h>
#include "tag_team.h"
#include "tag_team_wire.h"

static TtRoom state;
static uint32_t member;
static int local_room, cpu_fill, cpu_difficulty = 2, configured_ffa, configured_quick;
static int offline_mode, offline_custom, offline_beans; /* (BT4decomp 0.1.39.6: its dedicated offline menu's room) */
static char room_code[7], error[128];

/* ---- the room ---- */
int Port_TagConnect(const char *server) {
    extern void Port_TagClose(void);
    Port_TagClose();
    if (server && server[0]) { /* no network on the Vita */
        snprintf(error, sizeof(error), "Online Tag Team is not available on the PS Vita.");
        return 0;
    }
    local_room = 1;
    member = 1;
    return 1;
}
void Port_TagClose(void) {
    offline_mode = 0;
    memset(&state, 0, sizeof(state));
    member = 0;
    local_room = 0;
    room_code[0] = error[0] = 0;
}
void Port_TagCPUOptions(int fill, int difficulty) { cpu_fill = fill == 1; cpu_difficulty = difficulty >= 0 && difficulty <= 4 ? difficulty : 2; }
void Port_TagModeOptions(int free_for_all, int quick) { configured_ffa = free_for_all == 1; configured_quick = quick == 1; }
void Port_TagOpenWithRules(int size, int limit, int delay, const char *name, uint64_t content, unsigned rules) {
    (void)delay;
    if (!local_room) return;
    if (configured_ffa && configured_quick) size = 3;
    Tt_Open(&state, member, name, size, content);
    state.time = limit;
    state.delay = 0;
    state.battle_rules = rules;
    Tt_SetMode(&state, member, state.epoch, configured_ffa);
    if (cpu_fill) Tt_FillCPU(&state, member, state.epoch, cpu_difficulty);
    memcpy(room_code, "LOCAL0", 7);
}
void Port_TagOpen(int size, int limit, int delay, const char *name, uint64_t content) {
    Port_TagOpenWithRules(size, limit, delay, name, content, 0);
}
int Port_TagTeams(int left, int right) { return local_room && Tt_SetTeams(&state, member, state.epoch, left, right) == TT_OK; }
void Port_TagInitialTeams(int left, int right) { Port_TagTeams(left, right); }
int Port_TagSlot(int seat, int cpu, int difficulty) { return local_room && Tt_SetSlot(&state, member, state.epoch, seat, cpu, difficulty) == TT_OK; }
int Port_TagMovePlayer(int from, int to) { return local_room && Tt_MovePlayer(&state, member, state.epoch, from, to) == TT_OK; }

int Port_TagDraftSeat(void) {
    int team, seat = Tt_Seat(&state, member);
    if (seat < 0) return -1;
    if (Tt_Turn(&state, state.players[seat].team) == seat) return seat;
    if (member == state.host)
        for (team = 0; team < 2; ++team) {
            int turn = Tt_Turn(&state, team);
            if (turn >= 0 && state.players[turn].cpu) return turn;
        }
    return -1;
}
void Port_TagCommand(int command, int a, int b) {
    int rc = TT_BAD_PHASE;
    if (!local_room) return;
    if (command == TTW_RESULT && a == 1) { /* the results' Rematch: back to the character select, same room */
        int i;
        state.result_choice = 1; ++state.epoch; ++state.revision; state.phase = TT_SELECT;
        for (i = 0; i < Tt_Count(&state); ++i) {
            state.players[i].confirmed = 0; state.players[i].chara = -1;
            state.players[i].ended = state.players[i].loaded = state.players[i].armed = 0;
        }
        rc = TT_OK;
    }
    if (command == TTW_SELECT) rc = Tt_BeginSelect(&state, member, state.epoch);
    if (command == TTW_CANCEL_SELECT) rc = Tt_CancelSelect(&state, member, state.epoch);
    if (command == TTW_CHOOSE) rc = Tt_ChooseSeat(&state, member, state.epoch, Port_TagDraftSeat(), a, b);
    if (rc) snprintf(error, sizeof(error), "Cannot apply room action (%d).", rc);
    else error[0] = 0;
}
int Port_TagReselect(void) {
    if (!local_room || state.host != member || state.phase != TT_SELECT) return 0;
    if (Tt_CancelSelect(&state, member, state.epoch) != TT_OK) return 0;
    error[0] = 0;
    return Tt_BeginSelect(&state, member, state.epoch) == TT_OK;
}
int Port_TagCancelSelect(void) {
    if (state.phase != TT_SELECT || Tt_Seat(&state, member) < 0) return 0;
    Port_TagCommand(TTW_CANCEL_SELECT, 0, 0);
    return !error[0];
}

/* ---- BT4decomp 0.1.39.6 R1 1.2: the dedicated offline menu's room (Port_TagStartOffline), a player's choice taken
   back, the room's battle rules, and the private rooms' shared options. A local room has no shared options
   (state.options.version 0): those read as the PC's local room reads them (the player's own settings, the defaults). */
int Port_TagOfflineMode(void) { return local_room ? offline_mode : 0; }
int Port_TagOfflineCustom(void) { return !Port_TagOfflineMode() || offline_custom; }
int Port_TagOfflineBeans(void) { return Port_TagOfflineMode() ? offline_beans : 0; }
int Port_TagStartOffline(int mode, int left, int right, int time_limit, int difficulty, int custom, int beans) {
    int seat;
    if (mode < 1 || mode > 4 || (mode == 4 && left < 2) || left < 1 || left > 5 || right < 1 || right > 5 ||
        time_limit < 0 || time_limit > 4 || difficulty < 0 || difficulty > 4 || beans < 0 || beans > 5)
        return 0;
    if (!Port_TagConnect(NULL)) return 0;
    offline_mode = 0;
    offline_custom = custom != 0;
    offline_beans = beans;
    Tt_Open(&state, member, "Player 1", 2, 0);
    state.time = time_limit;
    state.delay = 0;
    if (Tt_SetTeams(&state, member, state.epoch, left, right) != TT_OK) goto failed;
    for (seat = 1; seat < left + right; ++seat)
        if (Tt_SetSlot(&state, member, state.epoch, seat, 1, difficulty) != TT_OK) goto failed;
    state.players[0].difficulty = difficulty;
    if (Tt_BeginSelect(&state, member, state.epoch) != TT_OK) goto failed;
    if (mode == 4)
        for (seat = 0; seat < left + right; ++seat)
            state.players[seat].team = seat < 2 || seat >= right + 2 ? 0 : 1;
    offline_mode = mode == 4 ? 2 : mode;
    memcpy(room_code, "LOCAL0", 7);
    return 1;
failed:
    Port_TagClose();
    return 0;
}
int Port_TagWithdrawSeat(int seat) {
    extern int Port_TagHost(void), Port_TagSeat(void);
    if (!local_room || state.phase != TT_SELECT || seat < 0 || seat >= Tt_Count(&state) || !state.players[seat].confirmed) return 0;
    if (state.players[seat].cpu ? !Port_TagHost() : seat != Port_TagSeat()) return 0;
    if (Tt_ChooseSeat(&state, member, state.epoch, seat, -1, 0)) return 0;
    error[0] = 0;
    return 1;
}
int Port_TagWithdrawChoice(void) { return Port_TagWithdrawSeat(Tt_Seat(&state, member)); }
void Port_TagChoiceItems(int chara, int costume, const uint16_t items[8]) { (void)items; Port_TagCommand(TTW_CHOOSE, chara, costume); }
const uint16_t *Port_TagPlayerItems(int actor) { return actor >= 0 && actor < Tt_Count(&state) && state.options.version ? state.players[actor].items : NULL; }
unsigned Port_TagBattleRules(void) { return state.battle_rules; }
int Port_TagSetBattleRules(unsigned rules) {
    if (!local_room || configured_quick || rules > TT_RULE_MASK || state.phase != TT_LOBBY || member != state.host) return 0;
    return Tt_SetBattleRules(&state, member, state.epoch, rules) == TT_OK;
}
int Port_TagRulesV2(void) { return state.options.version == TT_OPTION_COMBAT_VERSION; }
int Port_TagSharedReady(void) { return state.options.version != 0; }
unsigned Port_TagSharedCombat(int team, int slot, unsigned value, int save) { (void)team; (void)slot; (void)value; (void)save; return TT_COMBAT_DEFAULT; }
unsigned Port_TagActorCombat(int actor) { (void)actor; return TT_COMBAT_DEFAULT; }
int Port_TagSharedCombatTeam(int team, unsigned value) { (void)team; (void)value; return 0; }
int Port_TagOption(const char *key, int fallback) { extern int Port_Setting(const char *, int); return Port_Setting(key, fallback); }
int Port_TagSharedOption(const char *key, int fallback) { return Port_TagOption(key, fallback); }
int Port_TagSharedSave(const char *key, int value) { extern void Port_SettingSave(const char *, int); Port_SettingSave(key, value); return 1; }
int Port_TagSharedHealth(int team, int slot, int32_t values[5], int save) { (void)team; (void)slot; (void)values; (void)save; return 0; }
int Port_TagSharedHealthAll(int32_t values[2][5][5]) { (void)values; return 0; }
int Port_TagActorHealth(int actor, int32_t values[5]) { (void)actor; (void)values; return 0; }

/* ---- what the menus and the battle read ---- */
int Port_TagPhase(void) { return state.phase; }
int Port_TagCount(void) { return Tt_Count(&state); }
int Port_TagCapacity(void) { return state.phase >= TT_SELECT && state.phase <= TT_FIGHT ? Tt_Count(&state) : Tt_Capacity(&state); }
int Port_TagSlots(void) { return Tt_Capacity(&state); }
int Port_TagTeam(int seat) { return seat >= 0 && seat < Tt_Capacity(&state) ? state.players[seat].team : seat & 1; }
int Port_TagTeamIndex(int seat) { int i, n = 0; for (i = 0; i < seat; ++i) if (Port_TagTeam(i) == Port_TagTeam(seat)) ++n; return n; }
int Port_TagTeamCount(int team) { return Tt_TeamCount(&state, team, 1); }
int Port_TagTeamSlots(int team) { return Tt_TeamCount(&state, team, 0); }
int Port_TagFreeForAll(void) { return state.free_for_all; }
int Port_TagCanStart(void) { return state.phase == TT_LOBBY && Tt_TeamCount(&state, 0, 1) > 0 && Tt_TeamCount(&state, 1, 1) > 0; }
int Port_TagTime(void) { return state.time; }
int Port_TagSeat(void) { return Tt_Seat(&state, member); }
int Port_TagTurn(void) { int s = Port_TagSeat(); return s >= 0 ? Tt_Turn(&state, Port_TagTeam(s)) : -1; }
int Port_TagHumans(void) { return Tt_Humans(&state); }
int Port_TagHost(void) { return member && member == state.host; }
int Port_TagCPU(int seat) { return seat >= 0 && seat < TT_MAX_PLAYERS ? state.players[seat].cpu : 0; }
int Port_TagDifficulty(int seat) { return seat >= 0 && seat < TT_MAX_PLAYERS ? state.players[seat].difficulty : 0; }
int Port_TagPlayerChara(int seat) { return seat >= 0 && seat < TT_MAX_PLAYERS ? state.players[seat].chara : -1; }
int Port_TagPlayerCostume(int seat) { return seat >= 0 && seat < TT_MAX_PLAYERS ? state.players[seat].costume : 0; }
int Port_TagConfirmed(int seat) { return seat >= 0 && seat < TT_MAX_PLAYERS ? state.players[seat].confirmed : 0; }
const char *Port_TagPlayerName(int seat) { return seat >= 0 && seat < TT_MAX_PLAYERS ? state.players[seat].name : ""; }
int Port_TagResultChoice(void) { return state.result_choice; }
int Port_TagStage(void) { return state.stage; }
int Port_TagBgm(void) { return state.bgm; }
int Port_TagAttacks(void) { return 1; }
unsigned Port_TagSeed(void) { return state.seed; }
int Port_TagDelay(void) { return state.delay; }
const char *Port_TagCode(void) { return room_code; }
const char *Port_TagError(void) { return error; }
int Port_TagLocalBattle(void) { return local_room; }
int Port_TagDraftPending(void) { return 0; }
int Port_TagAllLoaded(void) { int i; for (i = 0; i < Tt_Count(&state); ++i) if (!state.players[i].loaded) return 0; return Tt_Count(&state) > 0; }
int Port_TagAllArmed(void) { int i; for (i = 0; i < Tt_Count(&state); ++i) if (!state.players[i].armed) return 0; return Tt_Count(&state) > 0; }
int Port_TagAllEnded(void) { int i; for (i = 0; i < Tt_Count(&state); ++i) if (!state.players[i].ended) return 0; return Tt_Count(&state) > 0; }
/* the character select's 60 s turn clock */
unsigned Port_TagMenuMillis(void) { return (unsigned)(sceKernelGetProcessTimeWide() / 1000); }

/* ---- the network's, answered as none ---- */
const char *Port_TagChatLine(int i) { (void)i; return ""; }
const char *Port_TagEndpoint(void) { return ""; }
int Port_TagChatCount(void) { return 0; }
int Port_TagCopyCode(void) { return 0; }
int Port_TagDataFingerprint(unsigned long long *out) { (void)out; return 0; }
int Port_TagJoinCode(const char *code) { (void)code; return 0; }
int Port_TagQueueWaiting(int queue) { (void)queue; return 0; }
int Port_TagSendInput(uint32_t frame, const unsigned char pad[TT_PAD_BYTES]) { (void)frame; (void)pad; return 0; }
int Port_TagTakeFrame(unsigned char out[TT_MAX_PLAYERS][TT_PAD_BYTES]) { (void)out; return 0; }
int Port_TagCatchup(void) { return 0; }
int Port_TagPing(void) { return -1; }
void Port_TagArena(int stage, int bgm, int attacks) { (void)stage; (void)bgm; (void)attacks; }
void Port_TagBattleRenderSet(int active) { (void)active; }
void Port_TagCapture(void) {}
void Port_TagChatSend(const char *text) { (void)text; }
void Port_TagHash(unsigned frame, unsigned hash) { (void)frame; (void)hash; }
void Port_TagJoin(const char *code, const char *name, uint64_t content) { (void)code; (void)name; (void)content; }
void Port_TagMenuTestAdvance(unsigned ms) { (void)ms; }
void Port_TagPoll(void) {}
void Port_TagUiCapture(int index) { (void)index; }
void Port_TagWait(int phase, int menu, int cursor, int show) { (void)phase; (void)menu; (void)cursor; (void)show; }
void Port_TagWaitClear(void) {}
void Port_TagWaitResumed(void) {}
void Port_TagYield(void) {}
void Port_TagJointRecordMark(int tick) { (void)tick; }
