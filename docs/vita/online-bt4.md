# Online play in BT4decomp, and what porting it to the Vita would take

An analysis (2026-11-13), nothing built yet. It reads the online code of BT4decomp (NauTaro's BT4 adaptation of
z3xox's Tenkaichi3Decomp PC port) as merged into this tree, and says what a Vita port needs for two goals: **Vita
against Vita** (ad-hoc, local network, internet) and **Vita against PC** (against BT4decomp's PC release). The BT3
PC port's online play, which BT4decomp builds on, is in [online.md](online.md); this document says what BT4decomp
does differently.

## Where this tree stands

- The BT4decomp side is NauTaro's `5b2c4f3` (2026-11-12, `0.1.39.5-R5.7` in `port/include/bt4_version.h`), merged
  with the Vita port at `2846bf3`. Unlike the BT3 Vita port, which was 644 commits behind the PC port, the game code
  here is the PC release's own.
- On the Vita all of the online code is left out. `port/vita/build.py` (`port_sources`) compiles none of
  `port/src/gs/net*.c`, `match_supabase.cpp`, `supabase_signal.cpp`, `tag_team_*.c/.cpp`, `voice.cpp`, nor
  `port/src/gs/state.c` (the rollback's saved states). `port/vita/vita_bt4.c` answers for them: `Port_NetActive`,
  `Port_NetSession` 0, `Port_NetSpeedMilli` 1000, `Port_OnlineBlocked` "Online play is not available on the PS
  Vita.", the `Port_Lobby*`, `Port_Ranked*`, `PortVoice_*` stubs, `Port_TagNetwork` 0, and the data fingerprint
  (`plat_fprint.c`) and verification (`plat_verify.c`) stubs.
- **Offline Tag Team is off too**: the game reaches the Tag Team rooms through `tag_team_net.c` even for a local
  room with CPUs (`Port_TagConnect("")`, `local_room`), and that file is stubbed (`vita_bt4.c`'s header).
- Code size of the online part: `net*.c` about 11,200 lines (C), signaling about 1,400 (C++), Tag Team about 2,800,
  voice about 1,000.

## What a player has (PC)

| Mode | What it is | Players |
|---|---|---|
| 1 vs 1 | Room code, Quick Match, Host / Join by Address; rollback and input delay chosen by the host | 2, plus spectators |
| Lobby / tournament | A room of up to 8 (`net_lobby.c`): two play, the others watch; brackets of 2-8 entrants | 2 fighting at a time |
| Ranked | 1 vs 1 or native Team Battle through Quick Match; Elo (K=32) kept by the server (`docs/ONLINE_RANKED_DESIGN.md`) | 2 |
| Tag Team | Simultaneous team battles: 2 vs 2 to 5 vs 5, six-player Free for All, CPU seats; Quick Match 2 vs 2 | up to 10 seats |
| Spectators | 1 vs 1 and lobbies only (the server refuses spectators in a Tag room: `cloudflare/online_core.mjs`, `join`) | |
| Voice | Proximity voice (volume by fighter distance), push-to-talk or voice activation | room members |
| Chat | Lines of text in 1 vs 1 (`net_chat.c`, `T_CHAT`) and Tag rooms (`TTW_CHAT`) | |

## The components

### Signaling: three generations, one in use

| Backend | Where | State in R5.7 |
|---|---|---|
| **Cloudflare "v5"** (Worker + Durable Object with SQLite) | client `port/src/gs/cloudflare_signal.inc` (`cloudEntry`); server `cloudflare/online_worker.mjs`, `online_core.mjs`, `cloudflare/ONLINE_V5.md` | **The default.** `cloudBackend()` is true unless `BT4_ONLINE_BACKEND=supabase` |
| Supabase (two projects, "primary" and "secondary") | client `supabase_signal.cpp` (`entry`), endpoints in `port/src/gs/online_route.h`; server `supabase/migrations/` | Selectable by the variable; still used once per installation to migrate an existing identity (`cloudAuthenticate`: `/auth/v1/token` then `/v5/auth/exchange`) |
| The BT3 port's Worker | `port/matchmaking/worker.js`, client `net_match.c` | Only with `BT3_MATCH_URL` set (`MATCH_URL_DEFAULT` is empty) |

The changelog's R5 1.5 ("Move online accounts ... to the Cloudflare service") matches the code;
`docs/port/README-BT4decomp.md`'s online section ("Rooms and Quick Match use authenticated private Supabase
signaling", "no longer uses Cloudflare coordination") and `BT4_PROFILE.md` ("sim2") are older than the code.

**Cloudflare v5, what the client does** (`cloudflare_signal.inc`):

- Host `bt4-online-v5.mellamannau22.workers.dev` (compile-time `BT4_CLOUDFLARE_HOST`), HTTPS on 443.
- Accounts: `POST /v5/auth/start` makes an anonymous account; `/v5/auth/refresh` renews it (every 14 minutes);
  tokens are kept in the user's preference folder (`network/cloudflare-v5-<slot>.json`). No key is compiled in:
  any client can make an account. A nickname is required to create or join a room (`NICKNAME_REQUIRED`).
- One WebSocket, `wss://.../v5/socket`: `{"op":"authenticate","access_token":...}`, then commands
  `{op, args, serial, consumer}`; events `bt4.ready`, `reply`, `bt4.signal` (channel `duel`, `tag` or `voice`),
  `bt4.room`, `bt4.closed`; `ping` every 20 s. If the upgrade is refused, the same commands go to `POST /v5/api`
  and a waiting search is polled every 2 s (`httpFallback`).
- Commands (`Bt4Signal_Command`): `create`, `join`, `get`, `get_signals`, `turn_credentials`, `lease`, `leave`,
  `list`, `queue`, `cancel`, `signal`, `counts`, `voice`, `presence`, `ranked_*`, `nickname_*`.
- The server matches rooms and queues on two strings (`online_core.mjs`, `join`, `queue`): `wire` and `content`
  (below, "Versions"). Quick Match sizes: duel 2, Tag `2v2-114` 4, `5v5-114` 10, `ffa6-114` 6.
- What the server does not carry: combat input and voice ("Combat inputs and voice are not sent through the
  coordinator", `ONLINE_V5.md`). It hands out short-lived Cloudflare TURN credentials (`turn_credentials`).

**Supabase** (legacy): Auth anonymous sign-up, PostgREST RPCs `bt4_api`, `bt4_ranked_api`,
`bt4_turn_credentials`, and a private Realtime channel `realtime:bt4:inbox:<user>` for events. The two projects'
URLs and keys are in `online_route.h`; the keys are Supabase **publishable** keys (`sb_publishable_...`), meant to be
public (`supabase/README.md`: "never a service-role key"). Which project admits new rooms is chosen by a policy file
fetched from GitHub (`net_update.c`, `Update_FetchOnlineRoute`: `NauTaro/.../online-routing/policy.json`), signed
with Ed25519 (`online_route.h`, `decode`: prefix `BT4-ONLINE-ROUTE-V1`, the update key `PORT_UPDATE_KEY`).

### Transport: what goes between the players

| Mode | Transport | NAT traversal | Files |
|---|---|---|---|
| 1 vs 1, lobby, tournament, spectators, chat | **Plain UDP**, BSD sockets, the BT3 port's packets (`MAGIC` "BT3N") | Own STUN client (RFC 5389) asking `stun.l.google.com:19302`, `stun1...`, and `global.stun.twilio.com:3478` (the second address shows a router that changes ports), keep-alive every 15 s; both sides greet each other's addresses at once (hole punching) | `net.c`, `net_lobby.c`, `net_match.c` (`stun_ask`), `match_supabase.cpp` (addresses through `signal` kind `udp`) |
| 1 vs 1 relayed | UDP through **TURN** (RFC 5766), only the host speaks TURN, Cloudflare's credentials (three hours) | after a direct attempt fails | `net_relay.c` (own client: MD5, SHA-1, HMAC written out) |
| Lobby match, players who cannot reach each other | UDP through the lobby's owner (`T_L_ROUTE`, 20-byte head) after 2 s without a direct connection | | `net_lobby_route.inc` |
| **Tag Team** | **WebRTC data channels** (libdatachannel): `bt4-room-control-v1` reliable ordered, `bt4-tag-input-v1` unordered, unreliable, 0 retransmits; 528-byte messages | ICE with the same three STUN servers, plus a TURN server from `turn_credentials` | `tag_team_p2p.cpp`, `p2p_rtc_api.inc` |
| Voice | WebRTC media tracks, Opus | ICE, STUN `stun.l.google.com`, TURN if given | `voice.cpp` |

The Tag Team topology is a **star**: every guest opens one peer connection, to peer 1, the room's owner
(`openPeers`: a guest only connects to id 1). The owner runs the room's state machine and the input coordinator
(below): the "Tag Team room authority" on the owner's computer. If the owner leaves, the round ends (no migration).

Two older Tag Team transports are still in `tag_team_net.c`, reached only with `BT4_TAG_SERVER` set: a TLS
WebSocket to a Cloudflare Durable Object that runs the same kernel compiled to WebAssembly (`cloudflare/worker.mjs`,
`kernel.wasm`, with an optional WebRTC side channel `tag_team_direct.cpp`), and a plain **TCP** connection to a
"development room server" (`tag_team_net.c` built with `TT_SERVER_MAIN`, port 26010).

## The game's sync model

### 1 vs 1: the BT3 port's lockstep with optional rollback

Unchanged from the BT3 port (`net.c`'s header): both machines run the whole game; each sends its player's input for
every vertical blank (60 a second at 100%), and the game reads both pads from there.

- **The input packet** (`send_inputs`): a 44-byte head (`MAGIC`, `T_INPUT`, first blank, count, "how far ahead",
  three times for the ping, the newest sure check blank and its 64-bit value) and the last 16 blanks' input, 18
  bytes each (the pad's data as `Port_PadRead` gives it, after the key configuration): at most 332 bytes. A lost
  packet is covered by the next ones.
- **Input delay**: 2 blanks by default; "automatic" picks 1-6 from the measured round trip (`auto_delay`); a relayed
  match gets a floor of up to 30 (`relay_delay_floor`). The host's delay is used on both sides.
- **Rollback** (`roll_tick`, `state.c`): the game runs ahead on a guess and goes back when the other's real input
  differs. The limit is **each side's own**: "the two ways of playing work against each other; the host's is only
  the other side's starting value" (the comment on `sOptRoll`). A build that cannot roll back plays with 0
  (`sRollMax = !Port_RollCan() ? 0 : ...`), as the 32-bit Linux build did. Quick Match uses 4.
- **Clock balance**: each packet says how far its sender is beyond the other's input; the copy that is ahead slows its
  pacing by 1 ms steps (`sBalance`, `gPortPaceShiftNs`).
- **Desync check**: every 32 blanks (`SYNC_EVERY`) each side notes `Port_NetDigest()` (`headless.c`: where the game
  is; in a fight both fighters' health and position and the clock); the newest "sure" note rides in every input
  packet and is compared. By default a difference is reported and the match goes on; `net_desync_end=1` ends it.
- **A session is a fresh program**: `relaunch` restarts the executable (`execv` / `CreateProcess`) with the
  session's settings in environment variables and an **empty save folder**, so both sides start from the game's
  defaults; the start-up runs with nothing pressed and the two meet at the character select (`T_ARRIVED`). The
  host's seed is fed to the game's generators when the versus mode starts.
- **Speed**: the host chooses 100-200%; Quick Match and the Host tab use 114% (`NET_SPEED_STANDARD`).

### Tag Team: a coordinator that commits frames, no rollback

Tag Team is a different model (`shared/tag_kernel.c`, `port/src/tag_team.c`, `port/include/tag_team.h`):

- **One clock, on the owner.** The owner's coordinator commits one frame every 1/34.2 s (30 steps a second at
  114%, `TT_TAG_STEPS_PER_SECOND`; `tag_team_p2p.cpp`: `step=1e9*10/342`). At each deadline it takes every seat's
  input for that frame; a seat whose input has not arrived gets its last input again for up to 3 frames (with the
  one-shot target and START bytes cleared), then neutral (`Kernel_DeadlineFrame`). **A late packet never rewrites a
  committed frame**: nothing is predicted and nothing rolls back. CPU seats get neutral input (their AI runs in the
  game on every machine).
- **Each client sends its input ahead**: frame `now + Port_TagDelay()`, where the lead is the one-way trip in frames
  (`ceil(ping * 34.2 / 2000)`) plus a margin raised only when the coordinator reports this seat late, plus the
  client's backlog. The owner has no network hop for its own input.
- **Clients wait for committed frames** (`src/battle/tag_team_online.inc`, `Port_TagTakeFrame`): a client without
  the next frame does not advance. A client that falls too far behind is dropped ("fell too far behind the TAG TEAM
  coordinator").
- **Packets** (`port/include/tag_team_wire.h`): 512 bytes, inside a 16-byte RTC head (magic `0x32505442`, room id,
  sender). `TTW_INPUT`: frame number and the 18-byte record (bytes 0-3 the buttons after the owner's key mapping,
  4-5 the stick, 6 target / L3, 7 pad status, 8 START, 9-17 shared test rules). `TTW_FRAME`: the frame number and
  the 10 seats' records, plus the "late" mask. The owner sends each frame with the two before it on the unreliable
  channel; a guest with a gap asks for a replay (command 110) on the reliable channel; the owner keeps 512 frames.
  `TTW_STATE` (the room: seats, characters, stage, seed, phase) and the lobby commands go on the reliable channel.
- **Desync check**: every 30 frames each client sends `TTW_HASH` with ten words per fighter (position at 1/16 world
  unit, health, ki, action, target, team, character, costume; `TagOnline_Trace`). The coordinator compares complete
  samples; positions within 2/16 count as "rounding" (`TT_SYNC_POSITION_TOLERANCE`, "harmless sub-pixel
  floating-point rounding differences between PCs"); six differing samples in a row abort the room
  (`TT_SYNC_FAILURE_LIMIT`).
- **Start**: all seats confirm, load, and report a load digest of the roster (`TTW_LOAD`, `TTW_LOADED`), arm and
  acknowledge (`TTW_ARM`, `TTW_ACK`), then `TTW_START`; the room's seed seeds `Port_Srand` and the twister
  (`tag_team_online.inc`).

| | BT3 port 1 vs 1 | BT4decomp Tag Team |
|---|---|---|
| Players | 2 (+ spectators) | 2-10 seats, humans and CPUs |
| Who decides a frame's input | each side, from the other's packets | the owner's coordinator, at a deadline |
| Late input | wait (lockstep) or guess and roll back | held 3 frames, then neutral; never corrected |
| Unit | vertical blank (60/s x speed) | simulation step (34.2/s, fixed) |
| Transport | UDP | WebRTC data channels |
| Check | 64-bit digest every 32 blanks, exact | 10 words per fighter every 30 frames, positions with a tolerance |

## Determinism

- The PC build runs all of the game's code on the port's soft-float (`port/tools/undefined.py`: `-msoft-float
  -mno-sse`, `softfloat_ps2.c`), the PS2's results on any processor, as the BT3 port did. Online play relies on it:
  1 vs 1's exact digest, and Tag Team's identical simulation on every machine.
- The Vita runs the game's floats on the hardware with the PS2's rules (`port/vita/vita_float.c`: round toward zero,
  denormals as zero, the adder's cut done on the bits, `__port_fdiv`; "It gives the same bits as the exact model"),
  `-ffp-contract=off`, and the same PS2 maths library (`port/third_party/newlib_libm`). The development build's
  self-test compares it with the PC's sums on a sample. A sample is not a proof.
- Tag Team's tolerance on positions is evidence that BT4decomp has seen small float differences between PCs (the
  comment in `tag_team_online.inc`); only positions are tolerated, and a persistent difference ends the room.
- Vita-only changes inside the game's code (`#ifdef PORT_VITA` in `src/`) must not change the simulation or the
  number of draws from shared generators. Most are drawing only; to be checked one by one before playing a PC:
  the NaN / infinity guards in `btl_char_move.c` (`BtlMove_CalcVerticalSpeed`), the cross-fade left to the renderer
  in `stg_b.c`, the surfaces' clipping short cuts in `eft_c.c` / `eft_d.c`, the control type read in `btl_input.c`,
  and the `--wrap` replacements of `port/src/port_fast.c` (`BtlTaskList_Update` is simulation). `netplay_notes.md`
  lists where draw code consumes `rand()` (the depth haze, particles): any culling difference there desyncs.
- The 1 vs 1 session depends on tick-driven sound (`BT3_SOUND_TICKS`, `snd_adx.c`, `by_ticks`), so that voice waits
  in the battle sequence do not depend on the audio device. `snd_adx.c` is already compiled on the Vita; it turns
  this on when `Port_NetSession()` or `Port_TagNetwork()` is 1.

## Versions and compatibility

What a client must match to play a PC:

| What | Value (R5.7) | Where checked |
|---|---|---|
| 1 vs 1 net version | `NET_VERSION = 0x42543414` | the greeting (`T_HELLO`); also the room's `wire` = `bt4-duel-<that number in decimal>` |
| Lobby version | `LOBBY_VERSION = 4` | `net_lobby.c` greeting |
| Tag Team protocol | `TT_PROTOCOL = 0x54540009`; room `wire` = `bt4-p2p-2` | every Tag packet; the server's admission |
| Game data family | `BT4-B14-Rev2-sim6`, ID `0xb33e7efcb2c66312` (`port/include/bt4_online_compat.h`): sizes and CRCs of `SLUS_219.78`, `BIN/DBZ4.BIN`, `BIN/DBZP.BIN` | `T_FPRINT` (required: `FP_PEER_REQUIRED = 1`), the room's `content` |
| Client version | `_client: {version: "0.1.39.5-R5.7", capability: 2}` on every admission command | the server's `minimum_client` (`online_worker.mjs`): older clients get `UPDATE_REQUIRED` |

- The data identity is a constant claimed when the three base files match; the program itself is not checked. The
  simulation family (`sim2` to `sim6` in the R3-R5 releases) changes when gameplay changes, and every such change
  locks older clients out.
- Signatures: Ed25519 (monocypher, `update_sign.c`) signs the update packages and the Supabase route policy only.
  Game packets are not signed; 1 vs 1's UDP is not encrypted (WebRTC's is, by DTLS). `net_sign.c`'s HMAC build key
  belongs to the old Worker and is not used by the default backend. The server's two-party Ranked report "is not
  an independent referee" (`ONLINE_RANKED_DESIGN.md`).

## Dependencies and servers

| Library | Used for | Built how on PC | On the Vita |
|---|---|---|---|
| WinHTTP (Windows), libcurl (Linux, macOS; `curl_ws_io.h` for WebSockets) | HTTPS, the signaling WebSocket, the update check | system / packaged | to bring: curl with OpenSSL or Mbed TLS, or SceHttp + SceSsl (no WebSocket) |
| libdatachannel 0.24.6 (libjuice, usrsctp, libSRTP, OpenSSL) | Tag Team P2P, voice | loaded at run time (`voiceLibrary`); `port/release/RTC_SDK.md`, `build_rtc.py` | not known to have been built for the Vita: C++17, threads, BSD sockets, a DTLS library |
| Opus | voice | packaged | plain C |
| nlohmann json (header), monocypher, SDL3 | JSON; Ed25519; threads, timers, preference folder | in `port/third_party` / system | json and monocypher compile anywhere; SDL3 through the Vita's shim (`port/vita/sdlshim`) |

Who hosts what:

- **NauTaro's**: the Cloudflare v5 Worker and its Durable Object (accounts, nicknames, rooms, queues, signaling,
  presence counts, Ranked), the Cloudflare TURN credentials issuer, the two Supabase projects (Free plan: "provider
  quotas, including concurrent Realtime connection limits", README-BT4decomp), and the GitHub route policy. Every
  online feature except "by address" goes through them. `ONLINE_V5.md`: "Review paid-plan request, SQLite and TURN
  usage before increasing capacity": Vita clients would add to his costs. **Using his service from a Vita needs his
  agreement**, and the Vita build would have to claim his version strings.
- **Public third parties**: Google's and Twilio's STUN servers (free, no account).
- **Self-hostable**: the server code is in the repository (`cloudflare/` for v5 with `wrangler.online.json`, its
  secrets `AUTH_SECRET`, `ADMIN_SECRET`, `TURN_GATEWAY_SECRET` to set; `supabase/migrations/`; the old
  `port/matchmaking/worker.js`). A server of our own serves only Vitas and PCs pointed at it: PC players would need a
  build with another `BT4_CLOUDFLARE_HOST`.
- **No server at all**: 1 vs 1 "Host by Address" / "Join by Address" (`Port_LobbyStart`, `Port_NetLanAddress`) and
  lobbies by address. A TURN relay is then unavailable.

## What the Vita needs

### (a) Vita against Vita

- **The same program on both sides** makes determinism a non-issue (the hardware float model is the same on both).
- **Local network and internet by address**: `net.c`, `net_lobby.c`, `net_chat.c` over the Vita's BSD sockets
  (SceNet through VitaSDK's newlib), without `state.c`: `Port_RollCan()` 0, delay-based lockstep. The BT3 analysis's
  budget applies: the game thread uses about 10-12 ms of a 33 ms frame there, and a silently re-run frame is
  estimated at ten times the PC's 1-4 ms, so rollback is out; lockstep with 2-4 frames of delay fits.
- **Ad-hoc**: SceNetAdhoc's PDP sockets are addressed by MAC address and port, not `sockaddr_in`. A small adapter
  under `net.c`'s `net_send` / `net_recv` (and the lobby's) and a group join (SceNetAdhocctl) in place of the
  address entry; no STUN, no TURN.
- **Tag Team between Vitas** can skip WebRTC: carry the same 512-byte packets over UDP (or the TCP of the
  development server), with the owner Vita running `shared/tag_kernel.c` as `tag_team_p2p.cpp` does. That is a
  transport of our own, not the PC's.
- **Matchmaking on the internet** needs a server: ours (the old Worker or v5 on our own Cloudflare account), or
  NauTaro's with his agreement.

### (b) Vita against PC

- **1 vs 1 needs no WebRTC.** Its transport is plain UDP, its STUN and TURN clients are plain C, and rollback is
  per side, so a Vita without rollback can meet a PC with rollback 4. **By address it needs no server and nobody's
  agreement.** With room codes and Quick Match it needs the Cloudflare v5 signaling: TLS, a WebSocket (or the
  HTTPS fallback, which works for 1 vs 1: the UDP addresses are kept in the room's descriptor), JSON, an anonymous
  account, a nickname, and the PC's version claim.
- **Speed**: Quick Match runs at 114%, so the Vita must keep 34.2 steps a second (29.2 ms each), presenting fewer
  pictures than steps; a room's host can choose 100%. Not measured on the Vita.
- **Determinism against soft-float** has to be shown, not assumed: the same scripted input (`BT3_NET_SCRIPT`) on a
  PC and a Vita, `Port_NetDigest` compared over whole fights on every stage; then real matches with the digest
  check on. If it ever differs, the fallback is the soft-float code on the Vita (twice the time per operation,
  measured in the BT3 analysis).
- **Tag Team against PC needs WebRTC on the Vita** (libdatachannel and its stack, or a compatible implementation of
  ICE + DTLS + SCTP), the Tag Team simulation itself running on the Vita at 34.2 steps a second with up to 10
  fighters, and the host's upload: each frame goes to each guest three times, 528 bytes, about 54 KB/s per guest, so
  about 490 KB/s for a 5 vs 5 owner (an estimate from the code). The TCP development server would avoid WebRTC on a
  local network, but PC players would have to start it and set `BT4_TAG_SERVER`; whether it still works with R5.7
  clients is not checked.
- **Voice** needs WebRTC media, Opus and the microphone (SceAudioIn): last, if ever.

### The plan, in order

Sizes are estimates (small: days; medium: one to three weeks; large: more, or unknown).

| # | Step | For | Size |
|---|---|---|---|
| 1 | Offline Tag Team on the Vita: build `tag_team_net.c`'s local room, `tag_team.c`, `tag_team_wire.c`; measure 2 vs 2 and 5 vs 5 on the console | every Tag Team goal | Medium |
| 2 | Network bring-up: SceNet / SceNetCtl start, the sockets through newlib, the SDL shim's threads, timers and random bytes, a session start without `execv` (`sceAppMgrLoadExec` with arguments, or an in-process reset) and with an empty save folder | all | Medium |
| 3 | 1 vs 1 by address, lockstep only (`Port_RollCan` 0), the real `plat_fprint.c`, tick-driven sound; Vita against Vita on a local network | (a) | Medium |
| 4 | The determinism proof against a PC (scripted input, digests), and the `PORT_VITA` audit above | (b) | Small to medium |
| 5 | 1 vs 1 by address, Vita against PC; speed 100% first, then 114% | (b) | Small after 3-4 |
| 6 | Ad-hoc 1 vs 1 (PDP adapter, group join) | (a) | Medium |
| 7 | Tag Team between Vitas over our own UDP transport, owner Vita running the kernel | (a) | Medium to large |
| 8 | TLS + WebSocket + JSON on the Vita (curl with OpenSSL or Mbed TLS; Mbed TLS 3.6 already builds for the Vita for vita-agent-use), then the v5 signaling client: room codes, Quick Match, TURN for 1 vs 1 | (b), (a) on the internet | Medium to large, and NauTaro's agreement |
| 9 | WebRTC data channels on the Vita, Tag Team against PC | (b) | Large, unproven |
| 10 | Voice | both | Large |

Steps 1-5 give Vita against Vita on a local network and Vita against PC by address without touching anyone's server.

### Main risks

- **The processor.** The Vita's game thread at 114% speed, without rollback, and Tag Team's up to 10 fighters: none
  measured. Step 1 answers the Tag Team part before any network work.
- **Determinism against a PC.** The hardware float model is checked on samples only; Vita-only changes in `src/`
  and the `--wrap` replacements must leave the simulation and the generators' draws untouched.
- **A moving target.** BT4decomp changed its signaling backend twice in recent releases (the Worker to Supabase in
  0.1.39.2 R6, Supabase to Cloudflare v5 in R5 1.5), its simulation family went from sim2 to sim6 across R1-R5, and
  the server can raise the minimum client at any time. Playing PCs means following every release that changes the simulation, the protocol or the minimum.
- **TLS on the Vita.** The firmware's root certificates are old; a TLS library with our own certificate list avoids
  depending on them.
- **The network.** Wi-Fi only (2.4 GHz on the PCH-1000), so more jitter than a wired PC; a larger input delay than
  PC players use.
- **Other people's servers.** NauTaro's service has quotas and costs; adding Vita clients to it, and claiming his
  version strings, needs his agreement. The by-address and ad-hoc ways, and a server of our own, need none.
