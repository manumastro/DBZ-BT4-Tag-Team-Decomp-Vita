# Online play in BT4decomp, and what porting it to the Vita would take

An analysis (2026-11-13), then the first milestone's tools (2026-11-18: the section "M0" below, branch
`wip/online-m0`). The fuller study of 2026-11-17 (the PC client step by step, the Vita's equivalents, the plan M0-M6,
the questions for NauTaro) is in the agent's notes (psvita-agent `notes/online/vita-online-study.md`); where this
document and that one differ, the study is the newer (its section 9 lists what is out of date here: the tree is now
upstream's `a8c140d`, 0.1.39.6 R1 1.3, client `0.1.39.6-R1.4`, family `BT4-B14-Rev2-sim8` `0xa93bcad79ac47a51`;
offline Tag Team is in the build). It reads the online code of BT4decomp (NauTaro's BT4 adaptation of
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
- **Offline Tag Team is off too**: `tag_team_net.c` is stubbed (`vita_bt4.c`'s header). On the PC a room of one
  player and CPUs is still an online room; the local room (`Port_TagConnect(NULL)`, `local_room`) is reached only by
  the render tests. The plan for an offline Tag Team on the Vita: [tagteam.md](tagteam.md).
- Code size of the online part: `net*.c` about 11,200 lines (C), signaling about 1,400 (C++), Tag Team about 2,800,
  voice about 1,000.
- 2026-11-18 (branch `wip/online-m1`): the network's bring-up is built (next section). The game's online entries are
  still `vita_bt4.c`'s stand-ins. The plan from here, the determinism study and the questions for NauTaro:
  `psvita-agent/notes/online/vita-online-study.md` (sections 4, 7 and 8).

## The network on the Vita (milestone M1, 2026-11-18)

Built on `wip/online-m1` (from `tagteam` `5ebd70d`), development build; **not run on the console yet**. Nothing of it
runs unless `bt3.txt` asks for it (`online=1` or `online_test=1/2`, [development.md](development.md)); off, no module is
loaded, nothing is allocated, no thread is started and the log has no `net:` line.

What is in the program (only with `BT4_VITA_ONLINE=1`, the development build's default; the release build leaves all of
it out, `build.py`):

| Part | Where | What |
|---|---|---|
| Start-up, shut-down, settings, self-test | `port/vita/vita_net.c` | `sceSysmoduleLoadModule(NET)`, `sceNetInit` with a pool from the C heap (`online_pool`, 1 MB by default, its lowest free amount logged through `sceNetGetStatisticsInfo`), `sceNetCtlInit`, the console's address and Wi-Fi signal logged; `curl_global_init`; the CA list; `Vita_NetStop` undoes it all |
| HTTPS, WebSocket, TLS | the build image's libcurl 8.17.0 (WS/WSS) on OpenSSL 1.1.1t, zstd, zlib | as the PC's Linux and macOS builds: the POSIX path of `curl_ws_io.h` and `voice_transport_curl.inc`, unchanged |
| CA list | `lib/cacert.pem` in the package (the build image's `/etc/ssl/certs/ca-certificates.crt`, 180 KB, as `port/tools/package_native.py` does for the PC) | the firmware's own certificates are not used. `online_ca=<pem>` adds one (the test server's): both are joined into `ux0:data/bt4/net/lib/cacert.pem` and the SDL base folder points there |
| A log line per request | `vita_net.c`, `-Wl,--wrap=curl_easy_init,curl_easy_perform,curl_multi_add_handle` | `net: POST https://host/v5/auth/start -> 200 in N ms (TLS ready at N ms), curl 0`; `net: opening ... (a connection held open: WebSocket)`. The query string, the bodies and the tokens are never logged. The `curl_easy_init` wrapper also sets every handle's CA list (curl's built-in path does not exist on the Vita; upstream's `http()` sets none) |
| JSON | nlohmann json (`port/third_party/voice/json.hpp`), compiled in `supabase_signal.cpp` | the client's own |
| SDL calls | `port/vita/sdlshim/SDL3/SDL.h` (declarations, small inline ones), `port/vita/vita_sdl.c` | threads (kernel threads, 256 KB stacks, cores 2-3, below the game's priority), recursive locks, semaphores, conditions, a per-thread value table, once-only start-up, files and folders (`SDL_GetPrefPath` = `ux0:data/bt4/`), random numbers of their own (never the game's), counters, string helpers, `flock` as a no-op (one program runs at a time); `SDL_LoadObject` and `SDL_CreateProcess` answer "none" (the PC's run-time libraries and its old `curl` process path are left out) |
| BT4decomp's signaling client | `port/src/gs/supabase_signal.cpp` + `cloudflare_signal.inc` + `update_sign.c`, `update_crypto.c`, `update_monocypher.c` (its route checks) | unchanged but for two `PORT_VITA` blocks in `cloudflare_signal.inc`: the host is `bt3.txt`'s `online_server` at run time (none: `no-server.invalid`, so a test build never reaches the default service), and the v5 backend only (no Supabase migration). Its identity goes to `ux0:data/bt4/network/<server>/` (`BT4_SIGNAL_SESSION_DIR`). `gs/net_update.c`'s online gate is answered by `vita_net.c` (no GitHub policy fetch). `BT4_SIGNAL_HTTPS_ONLY=1`, `BT4_SIGNAL_DIAGNOSTICS=1` work as `bt3.txt` lines |
| `Port_RollCan()` | `vita_bt4.c` | 0: no rollback on the Vita (delay-based lockstep, the study's section 5) |

### The self-test

`online_test=1` (then the game) or `2` (then the run ends with a `vita: exitafter:` line, which the console runners wait
for), on the start-up screen and in the log, on a thread of the kind the network's own run on:

1. DNS: `getaddrinfo(online_test_dns)` (default `stun.l.google.com`, the PC's first STUN server).
2. UDP: the console's LAN address found as `gs/net.c`'s `lan_address` does (connect + getsockname), then ten
   datagrams to `online_udp` (default the server's host, port 26011) on a non-blocking socket polled as `net.c`
   polls, each echoed by the test server: count back and round trips.
3. HTTPS: two GETs of `<online_server>/m1/get` on one handle (the second without a new handshake): status, name /
   TCP / TLS / total times, the C heap's growth.
4. WebSocket: upstream's `curl_ws_io.h` (`Bt4CurlWs_Open`, `_Connect`, `curl_ws_send` / `curl_ws_recv`) to
   `<online_server>/m1/echo`, five texts echoed.
5. Signaling: upstream's client asked `get` (consumer 0) and `nickname_get` (consumer 4): `/v5/auth/start`, the socket
   `/v5/socket`, `{"op":"authenticate"}`, `bt4.ready`, two replies; its status and times.
6. With `online_test_public=<url>`: an HTTPS GET of that address with the package's CA list only (no `online_ca`):
   whether firmware 3.65's clock and our list verify a public chain (`https://www.cloudflare.com/cdn-cgi/trace`: the
   kind of chain BT4decomp's `workers.dev` service has, without touching that service).

After each step, `net: memory ...`: the C heap in use and its growth since before the network (its peak too), and
SceNet's pool free now and at its lowest. Then `net: self-test done in N ms: N passed, N failed`.

The test server: `psvita-agent/agent-tools/online_testserver.py` (standard library Python; on the developer's Windows
PC with Windows' own Python, since a server inside WSL 2 sits behind its NAT). `--gen-cert` once (a test CA and a
certificate for the PC's LAN address, with any `openssl` program, Git for Windows' included), then run it: HTTPS and
WebSocket on 8443, UDP echo on 26011. It answers `/m1/get`, `/m1/echo`, `/v5/health`, `/v5/auth/start`,
`/v5/auth/refresh`, `/v5/api` and `/v5/socket` the way BT4decomp's v5 Worker does for a user without a room
(`authenticate` -> `bt4.ready`, `{serial, consumer, op}` -> `reply`, `ping` -> `pong`), and prints every request. It
was checked from WSL (Python client: the echo, `auth/start`, the socket's `bt4.ready` and a reply, the UDP echo, an
`/v5/api` call) and from Windows (`curl.exe` over the LAN address). BT4decomp's own `cloudflare/` Worker could be run
with `wrangler dev` instead (it needs Node and its `AUTH_SECRET`, `ADMIN_SECRET`, `TURN_GATEWAY_SECRET`); the Python
server is enough for M1. The console's settings: `psvita-agent/sets/bt4/online_m1_*.txt` (`online_m1_test`: the whole
test; `_https_only`: the signaling's HTTPS fallback; `_public`: the public chain, no server; `_ingame` / `_off`: the
u0 demo fight with the network up and without), each with what its log must show.

### What it costs

Measured off the console (the development build, `arm-vita-eabi-size` of `bt3.unstripped.elf`, against `tagteam`
`5ebd70d` built the same way):

| | `tagteam` | with the network part | added |
|---|---|---|---|
| program code (`text`) | 5,523,646 | 7,941,944 | +2,418,298 bytes (2.31 MB: OpenSSL's libcrypto and libssl, libcurl, zstd, the client and its JSON) |
| initialised data | 245,704 | 261,420 | +15.7 KB |
| zeroed data (`bss`; the game's 80 MB C heap is not in it) | 82,865,616 | 82,948,312 | +80.8 KB |
| `eboot.bin` | 3,525,112 | 5,366,009 | +1.76 MB |
| `.vpk` | 8,207,595 | 10,143,830 | +1.85 MB (with the 180 KB CA list) |

The code is loaded whether `online` is on or not: about 2.4 MB of the program's memory in a development build. The
release build has none of it (`BT4_VITA_ONLINE=0`; built 2026-11-18: 425 objects, `eboot.bin` 3,520,478 bytes, no CA
list); a release with online play would carry it.

At run time, only with `online=1` or a self-test: SceNet's pool (1 MB from the C library's heap, which the game's heap
shares: 80 MB, 56.7 MB at most used in the 25-stage soak, 74.4 MB in a 5 vs 5), and curl's and OpenSSL's own
allocations (a TLS connection's buffers, the CA list parsed into memory: the 180 KB file becomes a few hundred KB of
OpenSSL's structures, once per handle that loads it). Not measured yet: the self-test's `net: memory` lines give both
on the console. The threads: one per open WebSocket (`VoiceWs`) and one for the signaling client, 256 KB of stack each,
only while open.

### What builds, what does not

`python3 port/vita/build.py netcheck` (in the image: `BT3_VITA_DEV=1 port/vita/tools/docker_build.sh netcheck`) compiles
BT4decomp's other network files for the Vita into `port/build/obj_vita_dev/netcheck/`, without linking them, and lists
what each needs that nothing defines and what it would define twice. 2026-11-18, all ten compile:

| File | Compiles | Missing to link (besides the C library) | Defined already (`vita_bt4.c`'s stand-ins, to drop when it goes in) |
|---|---|---|---|
| `net.c` (1 vs 1 session, with `net_timing.h`) | yes | `Gs_HoldFrame`, `Gs_NetworkWait` (the PC renderer's hold while waiting), `MenuTranslation_PortText`, `Port_UiBanner`, `Port_UiNotice` (the PC's UI), `PortVoice_DuelKey` / `_NewKey` / `_Leave` (voice), `Update_BlocksOnline`, `Update_OnlineNote` (`net_update.c`), `execv` (`relaunch`: no `execv` on the Vita; a session start by `sceAppMgrLoadExec`, the study's 3.5) | 58: the `Port_Net*`, `Port_Lobby*` set, `Port_OnlineBlocked` |
| `net_chat.c` | yes | nothing | none |
| `net_relay.c` (TURN) | yes | nothing | none |
| `net_match.c` (STUN, the old Worker) | yes | `Sign_HasKey`, `Sign_Request`, `Sign_SelfTest` (`net_sign.c`, the old Worker's HMAC: leave that path out) | none |
| `net_lobby.c` | yes | nothing once `net.c` and `net_match.c` are in | 7 `Port_Lobby*` |
| `build_identity.c` | yes | `Port_FileOpenHost` (the three base files through `vita_file.c` / `vita_iso.c`) | `Port_DevRulesEncode` |
| `plat_fprint.c` | yes | nothing | the four `Port_DataFingerprint*` |
| `match_supabase.cpp` (room discovery) | yes | `PortVoice_P2PSession`, `Port_UiDragonBallFound`, `pthread_cancel` | 11: nickname, presence, `Port_Ranked*` |
| `tag_team_net.c` | yes | `TagPeer_*` (WebRTC, `tag_team_p2p.cpp`: libdatachannel, not on the Vita), `TagDirect_*` (`tag_team_direct.cpp`), `PortVoice_*`, `Gs_NetworkWait`, `Port_UiBanner` | 135 `Port_Tag*` (`vita_tag.c`, the offline room) |
| `state.c` | yes | `Port_HeapRegion`, `Port_UiNotice` (its rollback is compiled only for x86-64 and Apple: the Vita gets the fallback) | `Port_RollCan`, `Port_StateLog`, `Port_StateTouch`, `gPortResim` |

So M2 (1 vs 1 by address) needs: the PC UI's few calls answered on the Vita (`Port_UiBanner`, `Port_UiNotice`,
`Gs_HoldFrame`, `Gs_NetworkWait`), voice's answered "none", `relaunch` replaced, `net_sign.c`'s three left out of
`net_match.c`'s old path, `Port_FileOpenHost`, and `vita_bt4.c`'s stand-ins of these files removed as each goes in.
None of it is the simulation: that is M0's (the float model, `wip/online-m0`).

## Ad Hoc: Vita to Vita without internet (2026-11-19, branch `wip/online`)

Our addition to BT4decomp's online play; everything else stays the PC's. Ad Hoc is a transport, not a game mode: the
menus are the PC's hub (Create Match gets a Connection value "Ad Hoc", Join an "AD HOC" list of nearby rooms beside
"BY ADDRESS"), and a match runs upstream's own protocol (`gs/net.c`: hello, timing, pad records, digests) over Ad Hoc
datagrams instead of internet UDP. The plan, milestones A0-A2 and the reasons: psvita-agent
`notes/online/vita-multiplayer-plan.md`.

- The calls (`port/vita/vita_adhoc.c`): the PSP ad hoc layer the Vita keeps, as Rinnegatamante's vitaQuake uses it on
  HENkaku: `SCE_SYSMODULE_PSPNET_ADHOC`, `sceNetAdhocInit`, `sceNetAdhocctlInit` (ID `BT4VITA01`, reserved type), the
  system's own dialog for the group (`sceNetCheckDialog`, mode `PSP_ADHOC_CONN`; no PSN), then `sceNetAdhocPdp*`
  sockets addressed by MAC and port (payload up to 1444 bytes: the 1 vs 1 input packet and Tag Team's 512-byte
  packets fit). No programmatic group join exists in the SDK's stubs: the dialog it is. A Vita is either in Ad Hoc or
  on its access point, never both.
- A0 (`adhoc_test=1`, built, not run): start-up, the dialog, a socket, beacons and pings, a summary per Vita heard
  (round trips). With one console it shows the start-up, the dialog and the group; peers need a second Vita.
- A1 (next): `gs/net.c` unchanged on Ad Hoc: its one send (`net_send`) and one receive (`recvfrom`) on a pseudo socket
  (a `--wrap` of the socket calls for that one socket), the peer's MAC carried as a pseudo `sockaddr_in` (4 + 2 bytes);
  the room found from the beacons; the session as an address match.
- Vita against Vita does not wait for M0: two Vitas compute the same floats; the session-like start of `m0=1`
  (synchronous reads, neutral Vita Settings, the PC's key table) is what they need.

## M2a: upstream's 1 vs 1 network code linked, the online hub (2026-11-19, branch `wip/online`)

`BT4_VITA_NETLINK=1 BT3_VITA_DEV=1 port/vita/tools/docker_build.sh` (objects and package in `*_nl` folders) links
BT4decomp's `net.c`, `net_chat.c`, `net_relay.c`, `net_match.c`, `net_lobby.c`, `build_identity.c`, `plat_fprint.c` and
`match_supabase.cpp`, unchanged. `vita_bt4.c`'s 81 stand-ins of them are renamed away in that build
(`port/vita/vita_netlink_rename.h`, from `build.py netcheck`); `port/vita/vita_netglue.c` answers what they call from
PC-only files: the renderer's hold (`Gs_HoldFrame`, `Gs_NetworkWait`: nothing), the F1 window's banner and notices (a
log line for now; the Vita-made look comes with the menus), voice (none), the update gate (`UPDATE_REQUIRED` from the
server: "install the latest release"), the old Worker's signature (unused path), `execv` and `state.c`'s session and
rollback (none: no session can start yet), `pthread_cancel` (none), and `Port_FileOpenHost` for the identity's three
base files (the extracted disc folder; a disc image is not read yet, so the hub then says the data is missing).

`bt3.txt online_menu=1` shows the PC's hub: the main menu's "Tenkaichi Budokai Online" after Tag Team, and
`PortTbo_Run` (`src/menu/port_screen.c`, `menu_a.c`, `PORT_VITA` blocks). With `online=1` and `online_server=` the hub's
nickname and room calls go to the server named there (never BT4decomp's own in these builds).

Still to do for M2a: the session start (`sceAppMgrLoadExec` with a session file instead of `relaunch`'s `execv`, or a
start in the same process from the menus, as `state.c` does on 64-bit PCs); `Port_VBlank`'s online tick
(`port/src/plat_stub.c`); the banner and notice drawn Vita-native; the identity from a disc image; then a 1 vs 1 by
address (Vita vs Vita on the same build first: not blocked by M0).

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
- The Vita runs the game's floats on the float unit, round toward zero, denormals as zero (`Vita_FloatMode`),
  `-ffp-contract=off`, with the same PS2 maths library (`port/third_party/newlib_libm`) built for the float unit.
  **This is not the PC's arithmetic** (corrected 2026-11-17, psvita-agent `notes/online/vita-online-study.md`
  section 4): `vita_float.c`'s helpers (the adder's cut, the nearest division) are called by nothing in the game
  (`nm`), and the start-up self-test checks only them. Additions and subtractions miss the adder's cut, divisions
  round toward zero where the PC's round to nearest, a negative float converted to unsigned saturates where the PC's
  wraps, doubles run toward zero. The build variant of the M0 section below (`BT4_PS2FLOAT=1`, `ps2float=1`) gives
  the game's code the PC's arithmetic.
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

## M0: the same simulation as the PC, offline

The first milestone of the plan (study section 7): the Vita's game must compute the PC's fight blank for blank before
any network work matters. Branch `wip/online-m0` (2026-11-18) has the tools; nothing of it has run on a console yet.

### How the PC gets the PS2's arithmetic

- Every game source, the PS2's maths library (`port/third_party/newlib_libm`) and the port's game-side files are
  compiled with `-msoft-float -mno-sse -mno-mmx -ffp-contract=off` (`port/tools/undefined.py`, `CC`; clang on the 64-bit
  builds, and on arm64 Macs IR through `llc -mattr=-fp-armv8,-neon`). The compiler emits no float instruction: every
  float operation is a call (`__addsf3`, `__subsf3`, `__mulsf3`, `__divsf3`, `__fixsfsi`, `__fixunssfsi`, `__floatsisf`,
  `__floatunsisf`, the comparisons) into `port/src/softfloat_ps2.c`, the PCSX2 model by default (no build script sets
  `PORT_FLOAT_MODEL`): the adder's cut of the smaller operand then truncation, truncating multiply, division rounded
  to nearest, overflow and x / 0 the largest number, float to unsigned the PS2 compiler's sequence (a negative value
  wraps).
- Doubles (rare: the port's `headless.c`, the vector library's `vu0_b.c`): `port/src/plat_libm.c`, the host's IEEE
  arithmetic, round to nearest, with denormals.
- The vector library's references (`src/port/vu0_a.c`, `vu0_b.c`) take the vector unit's add, multiply and divide from
  `softfloat_ps2.c` (`RefVu0_*Bits`: no cut, truncating).

### How the Vita gets it now: the `BT4_PS2FLOAT` build and `ps2float`

Building the game with `-mfloat-abi=soft` is not the Vita's way: it changes the calling convention at every boundary
with the hard-float code (the vector library, the C library, the port's own files, function pointers), with no
error where a boundary is missed. So the closest thing that builds for ARM is done after the compiler:

- `BT4_PS2FLOAT=1 BT3_VITA_DEV=1 port/vita/tools/docker_build.sh` builds a variant (objects `port/build/obj_vita_dev_pf`,
  package `port/build/vita_dev_pf`, `BT4-Vita-dev-pf.vpk`). The game's objects are compiled to assembly as usual (the
  same flags and register allocation, plus `-mrestrict-it`, so that no float instruction sits in an IT block), and
  `port/vita/ps2float.py` replaces each float instruction whose result can differ from the PC's with a `b.w` to a
  piece of code at the end of its section (the same 4 bytes: nothing the compiler laid out moves). The piece saves
  r0-r5, ip and lr, passes the operands' bits to the model's entry point (`port/vita/vita_ps2float_a.S`, which keeps
  the flags, every float register and the float unit's status), puts the result in the destination and jumps back.
- Replaced (the variant of 2026-11-18: 8,057 instructions in 312 objects, the check's own 14 among them): `vadd.f32` 1,911, `vsub.f32` 1,983,
  `vdiv.f32` 1,320, `vmla.f32` 1,712, `vmls.f32` 249, `vnmls.f32` 144, `vnmla.f32` 5 (the product by the float unit,
  the sum by the model), `vcvt.u32.f32` 352, `vcvt.f32.u32` 201; doubles (`vadd`/`vmul`/`vdiv`/`vmla`/`vnmls.f64`,
  `vcvt` between float and double: 180) run with the float unit at round to nearest without flush to zero, the PC's
  doubles.
- Left alone: `vmul.f32` (truncating on both: the PC's `__mulsf3` and the float unit at round toward zero; the variant
  `BT4_PS2FLOAT=mul` replaces the multiplies too, as a check of that), conversions to and from signed integers
  (truncating on both), comparisons, negation, absolute value; the vector library's references (`pc_vu0_a.o`,
  `pc_vu0_b.o`) and `port_vufast.c` get their doubles replaced only, since their float operations are the vector
  unit's (no cut, truncating: the float unit's own); `port_fast.c`'s stand-ins for game functions are treated as game
  code.
- `bt3.txt` `ps2float=0` (default): the pieces do the float unit's own operation (the same results as the default
  build: this measures the detour's cost); `ps2float=1`: the PS2's results. The model is `port/vita/vita_ps2float.c`
  (the PC's add and multiply cores, `softfloat_ps2_inl.h`, and its division and unsigned conversions written out, built
  with `-mgeneral-regs-only`); the entry points take short ways where they give its bits
  (`port/vita/vita_ps2float_a.S`): an add or subtract is the adder's cut done on the bits and then the float unit's
  add (round toward zero, the game thread's mode); a division the float unit's at round to nearest; the C model for
  operands with the largest exponent, a zero or denormal divisor, a quotient that overflowed or is tiny.
  `ps2float=3`: the C model always (a check of the short ways). The default build is unchanged and ignores `ps2float`
  (one log line says so).
- Checked, without a console:
  - the build runs `port/vita/tools/pfcheck_host.c`: the Vita's model against `port/src/softfloat_ps2.c`, both built for
    the build machine, 4,000,000 operand pairs, six operations each: 0 differ (a difference stops the build);
  - `port/vita/tools/ps2float_emu.py` (an ARM emulator, Unicorn, in WSL): the start-up check below with `ps2float` 0,
    1 and 3: 0 differ; the add, subtract and divide entry points' short ways against the C model on 60,000 operand
    pairs (special patterns, tiny, huge, close exponents, any): 0 differ; each kind of replaced instruction (14) from
    40 random states each, in the three modes: every register but the destination, the flags and the float unit's
    status kept, the result the float unit's (0) or the model's (1, 3);
  - at start, the variant runs `Vita_PfSelfTest` (`port/vita/vita_pftest.c`, compiled once as game code and once as
    the float unit's): one log line `ps2float: check (ps2float=N): 2112 operations, 0 differ`.
- Open points: `src/battle/btl_cam.c:452` `__builtin_sqrtf` is a `vsqrt.f32` on the Vita (round toward zero) and a call
  of `sqrtf` on the PC (which one the PC's link picks is to be checked); the PC's own conversion of a float to unsigned
  on x86-64 and arm64 (`__fixunssfsi`, or a 64-bit conversion then truncation: the same for values below 2^32);
  operands with the largest exponent in comparisons (the PS2 has no infinity; the Vita's compare sees one).

### The cost

The model is chosen at run time, so the console measures three things on the same fight: the default build, the
variant at `ps2float=0` (the detour) and at `ps2float=1` (the model). The variant logs, every 600 blanks, the model's
operations per blank (`ps2float=N: per blank add .. sub .. mul .. div .. f2u .. u2f ..`), the other factor.

Estimate per replaced operation, from the code (Cortex-A9 at 444 MHz, not measured): the detour (`ps2float=0`) is
about 25 instructions (8-register push and pop, four moves between the float unit and the core registers, three
branches, the counter), some 30-50 cycles against 1-14 for the instruction itself (a division 14). `ps2float=1` adds
the short way: about 20 more instructions for an add or subtract (the flags kept, the exponents compared, the mask),
some 15 for a division (the float unit's mode set and put back twice): about 50-80 cycles in all, 0.11-0.18 us per
operation; the C model (rare operands, and the unsigned conversions) about 150-300 cycles. The game thread takes
19 ms a step in the heavy 1 vs 1 and 27-31 ms in 2 vs 2, against 29.2 ms a step at 114%. So at 10,000 replaced
operations a step the model costs some 1.1-1.8 ms (+6-9% in 1 vs 1), at 30,000 3.3-5.4 ms. The number of operations
a step is the unknown: the variant logs it. If it is too much: the pieces of code could do the short ways themselves
(no call, no 8-register save: about half the detour), and the counters could go.

### The state log, the inputs and a session-like start

- `BT3_HASH=<file>` (as on the PC; `port/vita/vita_m0.c`, which replaces the stub of `vita_bt4.c`): one line per
  vertical blank in the PC's format (`port/src/gs/state.c`): the blank, a memory checksum (0 on the Vita: the two
  programs' memory differs anyway), the XXH3 of the fight's values and, during a fight, both fighters' health and
  position bits, the battle clock, the C library generator's state (`port/src/headless.c` `Port_Trace` builds them,
  the same file on both).
- `port/vita/tools/m0_compare.py A B`: the fight lines of two logs lined up (from each file's first fight line, or
  `--offset`), the first blank and value where they part, with the blanks around it.
- The inputs: `BT3_PAD_REC` / `BT3_PAD_PLAY` (18 bytes per pad read, `port/src/plat_stub.c`, the same code on both)
  already worked on the Vita; `BT3_PAD_TABLE=<file>` (36 bytes per blank: both pads as read during that blank) is now
  written on the Vita too, and `BT3_PAD_TABLE_PLAY=<file>` plays such a table back by blanks (Vita only, read whole at
  start).
- `m0=1`: a start like an online session's, as far as the offline test needs: `asyncread=0` (a load takes the same
  blanks every run), an empty save folder of its own (`ux0:data/bt4/m0save`, emptied at each start, only when it holds
  the mode's mark file: never the player's saves), sound lengths by blanks (`BT3_SOUND_TICKS=1`, as a session), the
  PC's key configuration (the console's default in `Save_PortKeys` / `Save_PortDefaultKeys`, `src/sys/save.c` under
  `PORT_VITA`: the Vita's Normal swaps L1 and L2's actions, so the same pad record gave other actions), the attract
  demo's own pairing with `mode=demo` (`Demo_SetupBattle`, as the PC's `BT3_DEMO=1`, not the Vita's list),
  `BT3_HASH=ux0:data/bt4/m0hash.txt` unless another is named. The seed is the PC's fixed one (`BT3_SEED` changes it).

### The `PORT_VITA` audit

Every `PORT_VITA` block of the game's text (`src/`, `include/`; 2026-11-18, 70 files differ from upstream), read for
what it can change in the simulation: timing (blanks), the random generators' draws, skipped work in update or draw,
state. Most blocks are drawing only. The ones that matter:

| Where | What the Vita does | Default | Effect | Verdict | Fix for a session |
|---|---|---|---|---|---|
| `src/battle/btl_input.c:521` `BtlInput_GetKeyMask`, `src/sys/save.c` `Save_PortKeys`, `Save_PortDefaultKeys` | The control type "Normal" (Ki Charge on L): actions 4 and 5 (L1, L2) swapped against the PS2's and PC's default; custom sets from the Vita's controller page | on | the same pad gives other actions | breaks | **done**: `m0=1` (`gVitaSimExact`) gives the console's set and ignores custom sets; online needs the same (or the actions sent, not the pads) |
| `src/battle/eft_m.c:1412` `EftAura_StepFlames` | A flame that cannot respawn (pool full) is removed; the PC retries it forever | always | state, only where the PC hangs | breaks only where the PC hangs | keep; report upstream |
| `src/battle/btl_char_move.c:1414`, `1484` | NaN / infinite / huge height or jump speed set to 0 | always | state, only for such values | risk | none while values stay finite (the model gives no infinity: the PS2's largest number) |
| `src/battle/btl_char_member.c:467` | `demoult` keeps health at 1 or more | off | health | breaks if on | never in a session |
| `src/battle/battle_work.c:631` | `demochara3..10` replace the extra fighters | off | roster | breaks if on | never in a session |
| `src/sys/heap.c:282` `Heap_Alloc` | A block that does not fit comes from the C library (logged) | when the heap is full | pointers, possibly state | risk | in a session: a desync (end the match) |
| `src/sys/gfxm_e_b.c:764` `ObjShadow_BeginRender` | The silhouette pass skipped (no ground triangles, faded, or kept from the picture before) | on | drawing; `ObjShadow_LoadProg` is skipped with it (writes the shadow view's colour, draws the parts flat) | risk (to check that nothing of the game reads what it writes) | if M0's logs part near shadows: take the PC's path under `gVitaSimExact` |
| `src/sys/movie.c:276`, `333` | A touch on the screen skips a film | on | blanks | risk in a session with a film | off in a session |
| `src/menu/menu_a_b.c:72`, `menu_i_c.c:197`, `212` | `startmode=` skips the start-up menus, logos and opening film | off | start-up blanks (the arrival blank) | breaks if set | 0 in a session; `Port_NetFixedStart` is a stub (0) on the Vita: it must be 1 in a session (logos, intro, copyright always shown, whatever the player's skip settings) |
| `src/menu/menu_x_c.c` Vita Settings (clash assist, stick dead zone, 2nd pad, mods, touch spots, Unlock All, the reset of defaults) | Device settings that change the fight's input or the save | off / neutral | input, save, data | breaks if they differ between the sides | forced to the neutral values in a session (assist off, dead zone 0, 2nd pad off, no mods), Unlock All blocked |
| `src/menu/port_screen.c:823`, `890`, `port_tbo.c:1583` | Tag Team: no online section; the online hub replaced by the local setup | on | menu flow | (offline: right) | the PC's paths when online play exists |
| `port/src/headless.c` (`PORT_VITA`, the test fight) | The third archive opened before a test fight (the menus do it otherwise); the Vita's list of pairings | test fights | blanks before the fight; the pairing | (test only) | `m0=1` takes the PC's pairing; the archive's open is kept (blanks before the fight are not compared) |
| `port/vita/build.py` `FAST_WRAPS`, `VUFAST_WRAPS` (`--wrap`, not `src/`) | `BtlTaskList_Update`, the stage's culling, the effects' list and 39 vector functions replaced by faster stand-ins, checked at start against the library on samples | on | simulation where they compute (`BtlTaskList_Update`, the vector ones) | risk | in the variant `port_fast.c` gets the model as game code; the vector stand-ins are the vector unit's arithmetic; if M0's logs part, a link without them is the check |
| `include/sys/timer.h`, `dma.h`, `src/sys/dma.c`, `gfx*.c`, `vu1_packet.c`, `col_c*.c`, `view_*.c`, `stg_*.c`, `stgm_a.c`, `eft_c.c`, `eft_d.c`, `hud_c_b.c`, `tag_team_*.inc`, `btl_seq.c`, `btl_obj.c`, `bobj_a.c`, `btl_cam.c:851`, `mcflow_a.c`, `adx.c`, `file.c`, `menu_*` art and text | Drawing, markers for the renderer, logs, counters, register arrays, text and art | | none: no generator drawn or skipped, no game state written (the surfaces' culling in `eft_c.c` / `eft_d.c` skips only triangles the PC clips to nothing; the cross-fade's capture in `stg_b.c` keeps its buffers) | safe | none |

Outside `src/`: the Vita's `Port_VBlank` (`port/src/plat_stub.c`) has no online tick, rollback or state save (no
session yet: M2); `snd_adx.c`'s `disc_blanks` gives 0 for a voice file missing from its table (the length then comes
from the local file: online voice timing needs the table complete).

### The PC's reference

The PC port builds and runs on the developer's Windows PC (2026-11-18): upstream `a8c140d` (the commit merged here),
the 64-bit Linux variant (`BT3_CC=clang64`, software float, `BT3_SKELETON=1`), in WSL's Docker (image
`bt4-pc-build:local`), with the game data extracted from the developer's Spanish B14 Rev2 image (the Vita sets' disc)
into the clone's git-ignored `port/build/gamedata`. Headless (`BT3_GS=none BT3_DEMO=1`), a demo fight takes about 30
seconds. psvita-agent `agent-tools/bt4_pcref.sh` (`setup`, `build`, `run <name> [VAR=value ...]`) repeats it; the logs
are in `port/build/m0ref/` (git-ignored, with a README of each run's command line; a gzipped copy in psvita-agent
`notes/online/m0ref/`):

| Run | Environment | Blanks | Fight lines |
|---|---|---|---|
| `R1_demo_seed_default` | `BT3_DEMO=1` (the default seed, 0x12345678) | 3196 | 3010 (from blank 187) |
| `R2_demo_seed_0x1234`, `R2_demo_seed_0x5678` | `BT3_SEED=...` | 3198, 3662 | 3012, 3476 |
| `R3_demo_seed_default_soundticks` | `BT3_SOUND_TICKS=1` | 3196 | 3010 |
| `R4_demo_seed_0x1234_soundticks` | `BT3_SEED=0x1234 BT3_SOUND_TICKS=1` | 3198 | 3012 |

R1 run twice is byte for byte the same. R3's fight values are R1's in every blank; its memory checksum parts from
blank 865 on (the sound players' state, counted in blanks): in this fight the sound's timing did not reach the
fighters, in others it can, so the Vita's `m0=1` (sound by blanks) is compared with R3 and R4. A fight recorded on the console
(`online_m0_rec`) is played on the PC with `bt4_pcref.sh run <name> BT3_PAD_PLAY=<the .rec> BT3_SOUND_TICKS=1` (from the
title, the real menus, no window).

### What the console runs must show

The sets are in psvita-agent `sets/bt4/online_m0_*.txt` (each with a `.fetch` list: `run_batch.sh` fetches the state
logs, `vita_agent_run.py --fetch`, both on this branch). Two batches:

1. The variant (`port/build/vita_dev_pf`): `online_m0_demo_pf0`, `online_m0_demo_pf1`, `online_m0_demo_pf1b`,
   `online_m0_seed1234_pf1`, `online_m0_cost_h_pf0`, `online_m0_cost_h_pf1`, `online_m0_cost_t_pf0`,
   `online_m0_cost_t_pf1`.
2. The default build of this branch (`port/build/vita_dev`): `online_m0_demo_def`, `online_m0_cost_h_def`,
   `online_m0_cost_t_def`.

What each must show:
- the log's `ps2float: check (ps2float=0|1): 2112 operations, 0 differ` line in every run of the variant (a
  difference there is a bug of the variant: stop);
- `m0_demo_pf0` = `m0_demo_def` line for line (`m0_compare.py`: "the same in every fight blank"): the variant with the
  model off changes nothing;
- `m0_demo_pf1` = `m0_demo_pf1b`: two runs of the same start give the same fight (asyncread=0, the empty save);
- `m0_demo_pf1` against `m0_demo_pf0`: where the model first changes the fight (expected to part: that is the point);
- `m0_demo_pf1` against the PC's log of the same seed, disc and commit (below: `R3_demo_seed_default_soundticks.txt`,
  since `m0=1` counts sound in blanks; `m0_seed1234_pf1` against `R4_demo_seed_0x1234_soundticks.txt`): the same, or the first value and blank where they part;
- the cost: the game thread's ms per step (`perfsum.py --parts`) of `_def`, `_pf0`, `_pf1` in the heavy 1 vs 1 and in
  2 vs 2, and the operations per blank of the `ps2float=1:` lines: the detour's and the model's cost;
- a fight played by the developer (`online_m0_rec`, by hand, then `online_m0_play_pf1` and `online_m0_playtab_pf1`
  played back): the three state logs the same; the recording also drives the PC's `BT3_PAD_PLAY` run.

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
| WinHTTP (Windows), libcurl (Linux, macOS; `curl_ws_io.h` for WebSockets) | HTTPS, the signaling WebSocket, the update check | system / packaged | the build image's libcurl 8.17.0 (WS/WSS) on OpenSSL 1.1.1t, linked since M1 (above), with `lib/cacert.pem` in the package |
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
