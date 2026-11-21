/* The Vita Guide's words (port/vita/vita_guide.c), English and Spanish, in UTF-8. Written for a player who knows
   nothing: one idea a line, short words, the button's picture next to what it does, a heading over every group. The
   contents are the players' questions on Reddit, most asked first (psvita-agent notes/reddit-review-2026-11-19.md),
   and the README's answers. Keep them true to the code: a change of a control, a folder or a setting changes its line
   here too.

   Layout marks (vita_guide.c):
   - a line starting with "# " is a heading (the guide's orange, a little space over it); a page breaks before a
     heading when it can;
   - "\n" starts a line; an empty line is half a line of space;
   - {X} cross, {O} circle, {S} square, {T} triangle, {L1} {R1} {L2} {R2} {L3} {R3}, {U} {D} {LT} {RT} the D-pad's
     arrows: the game's own button pictures (its font's <PAD=> tag); {SEL} and {ST} are written SELECT and START
     (the game's pictures of those two are unlabelled pills).
   - %K:<name>% the player's controls of a Vita action now (port/vita/vita_remap.c: l2 r2 l3 r3 senzu fusion unfuse
     target quit unlock), %KEYLINES% the button pairs that give L2 / R2 / L3 / R3, %TOUCHW% where L2 / R2 / L3 / R3
     are touched now, in words (the Tag legend), %TOUCHLINES% the guide's lines of them: never write those
     buttons here.
   Lines over the page's width wrap by themselves; keep them under ~40 letters so they do not. Avoid the words the
   Vita's text fixes rewrite ("the console", "(PS2)", "PlayStation 2": port/vita/vita_text.c). */
#ifndef VITA_GUIDE_TEXT_H
#define VITA_GUIDE_TEXT_H

/* The guide is grouped (the developer's choice, 2026-11-21): the contents list five sections, each section lists its
   pages, a page opens its text. A topic whose body is NULL is the Vita Buttons editor (vita_guide.c). The topics are
   in the sections' order; kVgSections says where each section's run starts and how long it is. */
typedef struct VgTopic {
    const char *title;
    const char *body;
} VgTopic;

typedef struct VgSection {
    const char *title;
    const char *line; /* the contents' line under the title */
    int first, count;
} VgSection;

#define VG_SECTIONS 5
#define VG_SEC_CONTROLS 0
#define VG_SEC_ONLINE 2
#define VG_T_BUTTONS 2 /* Controls > Vita Buttons (the editor) */
#ifdef BT4_VITA_NETLINK
#define VG_ONLINE_N 10
#else
#define VG_ONLINE_N 1
#endif
#define VG_T_ONLINE 6
#define VG_T_SETTINGS (VG_T_ONLINE + VG_ONLINE_N)
#define VG_T_HELP (VG_T_SETTINGS + 5)
#define VG_TOPICS (VG_T_HELP + 3)
#define VG_SECTION_MAX 10 /* the longest section's pages (Online) */

static const VgSection kVgSectionsEn[VG_SECTIONS] = {
    {"Controls", "Buttons, touch, change the Vita buttons", 0, 3},
    {"Tag Team", "Start a battle, the fight, the screen", 3, 3},
#ifdef BT4_VITA_NETLINK
    {"Online", "Play people on the internet, Vita or PC", VG_T_ONLINE, VG_ONLINE_N},
#else
    {"Online", "Not in this version", VG_T_ONLINE, VG_ONLINE_N},
#endif
    {"Settings & files", "Settings, Unlock All, music, .iso, mods", VG_T_SETTINGS, 5},
    {"Help", "Problems, what is missing, questions", VG_T_HELP, 3},
};
static const VgSection kVgSectionsEs[VG_SECTIONS] = {
    {"Controles", "Botones, táctil, cambiar botones Vita", 0, 3},
    {"Tag Team", "Empezar, el combate, la pantalla", 3, 3},
#ifdef BT4_VITA_NETLINK
    {"En línea", "Juega por internet contra Vita o PC", VG_T_ONLINE, VG_ONLINE_N},
#else
    {"En línea", "No está en esta versión", VG_T_ONLINE, VG_ONLINE_N},
#endif
    {"Ajustes y archivos", "Ajustes, Desbloquear todo, .iso, mods", VG_T_SETTINGS, 5},
    {"Ayuda", "Problemas, lo que falta, preguntas", VG_T_HELP, 3},
};

static const VgTopic kVgEn[VG_TOPICS] = {
    /* ---- Controls ---- */
    {"Buttons",
     "# Fighting\n"
     "Left stick   Move\n"
     "{S}   Attack\n"
     "{T}   Ki blast\n"
     "{X}   Dash\n"
     "{O}   Guard\n"
     "{L1}   Charge ki\n"
     "{R1}   Fly up\n"
     "# Buttons the Vita does not have\n"
     "{L2}   Blast wave\n"
     "{R2}   Fly down\n"
     "These and {L3} {R3}: touch, see the next page.\n"
     "# Pause and quit\n"
     "{ST}   Pause\n"
     "Hold %K:quit%   Quit the game"},
    {"Touch: L2, R2, L3, R3",
     "# Where you touch them now\n"
     "%TOUCHLINES%\n"
     "# Another way\n"
     "%KEYLINES%\n"
     "# Move them\n"
     "Options > Controller > Vita Settings > Touch\n"
     "Put them in the screen's corners, or off.\n"
     "Button pairs: Controls > Vita Buttons\n"
     "# In the menus\n"
     "Tap an item, tap it again to choose it.\n"
     "Swipe to scroll. Two fingers: go back."},
    {"Vita Buttons: change them", NULL},
    /* ---- Tag Team ---- */
    {"Start a battle",
     "# How to start\n"
     "Main menu > Tag Team > Local Battle\n"
     "# Set up the battle\n"
     "Mode: you and CPU friends against the CPU.\n"
     "Choose the team size, time and CPU level.\n"
     "Then choose Select Fighters.\n"
     "# Choose the fighters\n"
     "The teams pick in turns.\n"
     "Then pick the stage and the music.\n"
     "# Good to know\n"
     "2 vs 2 runs smoothly.\n"
     "3 vs 3 and bigger teams are still slow."},
    {"In the fight",
     "# Your target\n"
     "%K:target%   Change the enemy you aim at\n"
     "A ring and a health bar mark him.\n"
     "# Help your team\n"
     "Near a fallen ally   Revive him\n"
     "%K:senzu%   Eat a Senzu Bean\n"
     "%K:fusion% + a direction   Fuse with a near ally\n"
     "%K:unfuse%   Undo the fusion\n"
     "# Forgot something?\n"
     "In the fight: {ST} > Vita Controls"},
    {"The screen",
     "# What you see\n"
     "Top left   Your health and ki\n"
     "Bottom left   Your team\n"
     "Ring and bar   The enemy you aim at\n"
     "Top right   The radar: every fighter"},
    /* ---- Online ---- */
#ifdef BT4_VITA_NETLINK
    {"Getting started",
     "# What you need\n"
     "Wi-Fi with internet.\n"
     "# Where\n"
     "Main menu > Tenkaichi Budokai Online\n"
     "# Your name\n"
     "Profile > Type Name: the name others see.\n"
     "Set it before your first match.\n"
     "# Moving around\n"
     "{U}{D}   Choose a row\n"
     "{X}   Open it\n"
     "{T}   Go back"},
    {"Quick Match",
     "# Fight someone right now\n"
     "Tenkaichi Budokai Online > Quick Match\n"
     "Battle   Single or Team Battle\n"
     "Region   Auto: the nearest players first\n"
     "Search   Start looking\n"
     "# While it searches\n"
     "{S}   Train While Waiting\n"
     "{T}   Stop searching\n"
     "# Good to know\n"
     "Anyone searching can be your rival,\n"
     "on a Vita or on a PC."},
    {"Create Match",
     "# Your own room\n"
     "Tenkaichi Budokai Online > Create Match\n"
     "Choose the rules, then Create Room.\n"
     "# Connection\n"
     "Room Code   Friends join with a code\n"
     "By Address   Friends on your Wi-Fi\n"
     "# Who can join (Room Code)\n"
     "By Code   Only who has the code\n"
     "Open Room   Anyone, from Public Rooms\n"
     "# In the room\n"
     "The code is on the screen: tell your friends.\n"
     "Two players fight, the others watch."},
    {"Join a room",
     "# Join a friend's room\n"
     "Tenkaichi Budokai Online > Public Rooms\n"
     "Public Rooms   Open rooms: pick one\n"
     "Private Rooms   Type the room's code\n"
     "# The room code\n"
     "Six letters and numbers.\n"
     "Ask the room's owner for it.\n"
     "# By address (the same Wi-Fi)\n"
     "Private Rooms, then BY ADDRESS.\n"
     "Type the address the owner's room shows,\n"
     "for example 192.168.1.20:7000"},
    {"Ranked",
     "# Fights that count\n"
     "Tenkaichi Budokai Online > Ranked\n"
     "Find Ranked Match   Search for a rival\n"
     "Global Top   The best players\n"
     "How Ranked Works   Points and streaks\n"
     "# Good to know\n"
     "Win to earn points. A streak earns more.\n"
     "The ranking starts again every week\n"
     "or month (Argentina time).\n"
     "Set your name in Profile first."},
    {"Tournament",
     "# A tournament with friends\n"
     "Tenkaichi Budokai Online > Tournament\n"
     "Up to 32 players.\n"
     "By Code   Only who has the code\n"
     "Open Room   Anyone, from Public Rooms\n"
     "# How it goes\n"
     "Players join, then the owner starts it.\n"
     "Two fight at a time, the others watch.\n"
     "The bracket shows who fights next."},
    {"Train While Waiting",
     "# Fight while you wait\n"
     "While Quick Match searches:\n"
     "{S}   Train While Waiting\n"
     "Pick your fighter and a sparring partner,\n"
     "then Start Training.\n"
     "# The search goes on\n"
     "A sign shows how long it has searched.\n"
     "When a rival is found, the match starts."},
    {"Training",
     "# Practice, no rival needed\n"
     "Tenkaichi Budokai Online > Training\n"
     "1 vs 1   The game's Training mode\n"
     "2 vs 2   Tag Team against the CPU\n"
     "Tag Team Tutorial   Learn the Tag moves"},
    {"Playing PC players",
     "# Vita and PC together\n"
     "The Vita uses the PC version's service.\n"
     "In Quick Match, Ranked, rooms and\n"
     "tournaments you can meet PC players.\n"
     "# The same version\n"
     "Both games must be the same release.\n"
     "When the PC version updates, wait for\n"
     "a new release of this port to play online.\n"
     "# Smooth fights\n"
     "Play close to your Wi-Fi router.\n"
     "The Vita has no rollback: with a slow\n"
     "connection the fight waits a little."},
    {"Not on the Vita yet",
     "# Not online on the Vita yet\n"
     "Tag Team online (2 vs 2 and more)\n"
     "Voice chat\n"
     "Vita to Vita without internet (ad hoc)\n"
     "# Tag Team now\n"
     "Main menu > Tag Team: against the CPU.\n"
     "Online > Training > 2 vs 2 works too."},
#else
    {"Online play",
     "# Not in this version\n"
     "This version of the port has no online play."},
#endif
    /* ---- Settings & files ---- */
    {"Vita Settings",
     "# Where\n"
     "Options > Controller > Vita Settings\n"
     "# The rows\n"
     "Touch   Where {L2} {R2} {L3} {R3} are\n"
     "Clashes   Win clashes with {S} or the stick\n"
     "2nd Pad   A second player (PS TV)\n"
     "Mods   Your mods on or off\n"
     "Mods, lower row   The anime music\n"
     "Stick   Slower stick, if it feels too quick\n"
     "{S}   Unlock All\n"
     "{ST}   Vita Buttons: change them"},
    {"Unlock All",
     "# What you get\n"
     "Every fighter, form, stage and song,\n"
     "every item, and 4,850,000 Zenni.\n"
     "# How to do it\n"
     "In a menu, hold %K:unlock% for one second.\n"
     "Or: Options > Controller > Vita Settings, {S}\n"
     "Then press {X}.\n"
     "# Changed your mind?\n"
     "Your save is copied first.\n"
     "On the same screen, {T} puts it back.\n"
     "# Careful\n"
     "PS2 cheat codes do nothing here.\n"
     "Never copy another port's save over yours."},
    {"Anime soundtrack",
     "# English game, anime music\n"
     "1. Copy your Spanish B14 Rev2 .iso\n"
     "to ux0:data/bt4/es/\n"
     "2. Options > Controller > Vita Settings:\n"
     "Mods, lower row: ON\n"
     "Texts and voices stay in English.\n"
     "# The whole game in Spanish\n"
     "Put only the Spanish .iso in ux0:data/bt4/"},
    {"Game files (the .iso)",
     "# What you need\n"
     "Your own B14 Rev2 .iso by Team BT4,\n"
     "English or Spanish. Get it from Team BT4.\n"
     "Other versions (B13, Beta X) do not work.\n"
     "# Where it goes\n"
     "ux0:data/bt4/  (any name ending in .iso)\n"
     "# Common mistakes\n"
     "Copy the .iso, not the .7z, .rar or .zip.\n"
     "Unpack it on a computer, not a phone.\n"
     "A FAT32 card cannot hold over 4 GB:\n"
     "use exFAT.\n"
     "\"Archive not found\": copy the .iso again.\n"
     "# Updating\n"
     "Install the new VPK. Your saves stay."},
    {"Mods",
     "# Where they go\n"
     "ux0:data/bt4/mods/\n"
     "mods/pzs4us1/00042.bin replaces\n"
     "file 42 of PZS4US1.AFS.\n"
     "# On or off\n"
     "Options > Controller > Vita Settings > Mods\n"
     "# Good to know\n"
     "The PC version's Lua mods do not work.\n"
     "Game stops at the start? Turn Mods off."},
    /* ---- Help ---- */
    {"Problems? Send this",
     "# Tell us on GitHub\n"
     "manumastro/DBZ-BT4-Tag-Team-Decomp-Vita\n"
     "Open an issue there.\n"
     "# Attach these files\n"
     "From ux0:data/bt4/:\n"
     "log.txt and log-prev.txt\n"
     "hang.txt and err.txt, if you have them\n"
     "Error C2-12828-1: the .psp2dmp in ux0:data/\n"
     "# And say\n"
     "What you were doing, your Vita model\n"
     "and your plugins.\n"
     "# Quick fixes\n"
     "Small picture with borders: Sharpscale\n"
     "scaling mode Original.\n"
     "Slow scenes: the CapUnlocker plugin."},
    {"Not there yet",
     "# Coming later, maybe\n"
#ifdef BT4_VITA_NETLINK
     "Online Tag Team (see Online)\n"
#else
     "Online play\n"
#endif
     "The PC version's new songs and stages\n"
     "The PC version's F1 menu\n"
     "# Works, but slowly\n"
     "Tag Team 3 vs 3 and bigger\n"
     "# Not supported\n"
     "Other builds: B13, Beta X"},
    {"More questions",
     "# This port or arielxflores's?\n"
     "Both play B14 Rev2. You can have both.\n"
     "# Does it remove BT3?\n"
     "No. Both stay installed.\n"
     "# Daima fighters?\n"
     "Only what B14 Rev2 has.\n"
     "# The game in Spanish?\n"
     "Use the Spanish B14 Rev2 .iso.\n"
     "# Stick too quick? Clashes too hard?\n"
     "See Settings & files > Vita Settings."},
};

static const VgTopic kVgEs[VG_TOPICS] = {
    /* ---- Controles ---- */
    {"Botones",
     "# Combate\n"
     "Stick izquierdo   Moverte\n"
     "{S}   Golpe\n"
     "{T}   Ráfaga de ki\n"
     "{X}   Impulso\n"
     "{O}   Defensa\n"
     "{L1}   Cargar ki\n"
     "{R1}   Subir\n"
     "# Botones que la Vita no tiene\n"
     "{L2}   Onda\n"
     "{R2}   Bajar\n"
     "Estos y {L3} {R3}: táctiles, mira la página siguiente.\n"
     "# Pausa y salir\n"
     "{ST}   Pausa\n"
     "Mantén %K:quit%   Cerrar el juego"},
    {"Táctil: L2, R2, L3, R3",
     "# Dónde los tocas ahora\n"
     "%TOUCHLINES%\n"
     "# Otra forma\n"
     "%KEYLINES%\n"
     "# Cambiarlos de sitio\n"
     "Opciones > Mando > Vita Settings > Touch\n"
     "Ponlos en las esquinas de la pantalla, o apágalos.\n"
     "Parejas de botones: Controles > Botones Vita\n"
     "# En los menús\n"
     "Toca una opción y tócala otra vez para elegirla.\n"
     "Desliza para bajar. Dos dedos: volver."},
    {"Botones Vita: cámbialos", NULL},
    /* ---- Tag Team ---- */
    {"Empezar un combate",
     "# Cómo empezar\n"
     "Menú principal > Tag Team > Local Battle\n"
     "# Prepara el combate\n"
     "Modo: tú y amigos CPU contra la CPU.\n"
     "Elige el tamaño del equipo, el tiempo y la CPU.\n"
     "Luego elige Select Fighters.\n"
     "# Elige los luchadores\n"
     "Los equipos eligen por turnos.\n"
     "Luego el escenario y la música.\n"
     "# Bueno saber\n"
     "2 contra 2 va fluido.\n"
     "3 contra 3 y equipos mayores aún van lentos."},
    {"En combate",
     "# Tu objetivo\n"
     "%K:target%   Cambia el enemigo al que apuntas\n"
     "Lo marcan un anillo y una barra de vida.\n"
     "# Ayuda a tu equipo\n"
     "Junto a un aliado caído   Lo reanimas\n"
     "%K:senzu%   Semilla del ermitaño\n"
     "%K:fusion% + una dirección   Fusión con un aliado\n"
     "%K:unfuse%   Deshacer la fusión\n"
     "# ¿Se te olvidó algo?\n"
     "En combate: {ST} > Controles Vita"},
    {"La pantalla",
     "# Lo que ves\n"
     "Arriba a la izquierda   Tu vida y tu ki\n"
     "Abajo a la izquierda   Tu equipo\n"
     "Anillo y barra   El enemigo al que apuntas\n"
     "Arriba a la derecha   El radar: todos"},
    /* ---- En línea ---- */
#ifdef BT4_VITA_NETLINK
    {"Para empezar",
     "# Lo que necesitas\n"
     "Wi-Fi con internet.\n"
     "# Dónde\n"
     "Menú principal > Tenkaichi Budokai Online\n"
     "# Tu nombre\n"
     "Perfil > Cambiar nombre: el que ven los demás.\n"
     "Ponlo antes de tu primera partida.\n"
     "# Moverte\n"
     "{U}{D}   Elegir una fila\n"
     "{X}   Abrirla\n"
     "{T}   Volver"},
    {"Partida rápida",
     "# Lucha contra alguien ya\n"
     "Tenkaichi Budokai Online > Partida rápida\n"
     "Combate   Individual o por equipos\n"
     "Región   Auto: primero los más cercanos\n"
     "Buscar   Empieza a buscar\n"
     "# Mientras busca\n"
     "{S}   Entrenar mientras esperas\n"
     "{T}   Dejar de buscar\n"
     "# Bueno saber\n"
     "Cualquiera que busque puede ser tu rival,\n"
     "en una Vita o en un PC."},
    {"Crear partida",
     "# Tu propia sala\n"
     "Tenkaichi Budokai Online > Crear partida\n"
     "Elige las reglas y luego Crear sala.\n"
     "# Conexión\n"
     "Código de sala   Entran con un código\n"
     "Por dirección   Amigos en tu misma Wi-Fi\n"
     "# Quién entra (Código de sala)\n"
     "Por código   Solo quien tiene el código\n"
     "Sala abierta   Todos, desde Salas públicas\n"
     "# En la sala\n"
     "El código sale en pantalla: díselo a tus amigos.\n"
     "Dos luchan y los demás miran."},
    {"Entrar en una sala",
     "# Entra en la sala de un amigo\n"
     "Tenkaichi Budokai Online > Salas públicas\n"
     "Salas públicas   Salas abiertas: elige una\n"
     "Salas privadas   Escribe el código\n"
     "# El código de sala\n"
     "Seis letras y números.\n"
     "Pídeselo al dueño de la sala.\n"
     "# Por dirección (la misma Wi-Fi)\n"
     "Salas privadas y luego POR DIRECCIÓN.\n"
     "Escribe la dirección que muestra la sala,\n"
     "por ejemplo 192.168.1.20:7000"},
    {"Clasificatorias",
     "# Combates que cuentan\n"
     "Tenkaichi Budokai Online > Clasificatorias\n"
     "Buscar clasificatoria   Busca un rival\n"
     "Top global   Los mejores jugadores\n"
     "Cómo funcionan   Puntos y rachas\n"
     "# Bueno saber\n"
     "Gana para sumar puntos. Una racha suma más.\n"
     "La clasificación empieza de nuevo cada semana\n"
     "o cada mes (hora de Argentina).\n"
     "Antes pon tu nombre en Perfil."},
    {"Torneo",
     "# Un torneo con amigos\n"
     "Tenkaichi Budokai Online > Torneo\n"
     "Hasta 32 jugadores.\n"
     "Por código   Solo quien tiene el código\n"
     "Sala abierta   Todos, desde Salas públicas\n"
     "# Cómo va\n"
     "Los jugadores entran y el dueño lo empieza.\n"
     "Luchan dos cada vez y los demás miran.\n"
     "El cuadro dice quién lucha después."},
    {"Entrenar mientras esperas",
     "# Lucha mientras esperas\n"
     "Mientras Partida rápida busca:\n"
     "{S}   Entrenar mientras esperas\n"
     "Elige tu luchador y un compañero de práctica,\n"
     "luego Iniciar entrenamiento.\n"
     "# La búsqueda sigue\n"
     "Un letrero dice cuánto lleva buscando.\n"
     "Cuando llega un rival, empieza la partida."},
    {"Entrenamiento",
     "# Practica, sin rival\n"
     "Tenkaichi Budokai Online > Entrenamiento\n"
     "1 vs 1   El modo Entrenamiento del juego\n"
     "2 vs 2   Tag Team contra la CPU\n"
     "Tutorial de Tag Team   Aprende lo de Tag"},
    {"Jugar contra el PC",
     "# Vita y PC juntos\n"
     "La Vita usa el servicio de la versión de PC.\n"
     "En Partida rápida, Clasificatorias, salas\n"
     "y torneos puedes encontrar jugadores de PC.\n"
     "# La misma versión\n"
     "Los dos juegos deben ser la misma versión.\n"
     "Cuando la versión de PC se actualiza, espera\n"
     "una versión nueva de este port para jugar.\n"
     "# Combates fluidos\n"
     "Juega cerca de tu router Wi-Fi.\n"
     "La Vita no tiene rollback: con una conexión\n"
     "lenta el combate espera un poco."},
    {"Aún no en la Vita",
     "# Aún no en línea en la Vita\n"
     "Tag Team en línea (2 contra 2 y más)\n"
     "Chat de voz\n"
     "Vita con Vita sin internet (ad hoc)\n"
     "# Tag Team ahora\n"
     "Menú principal > Tag Team: contra la CPU.\n"
     "En línea > Entrenamiento > 2 vs 2 también."},
#else
    {"Juego en línea",
     "# No está en esta versión\n"
     "Esta versión del port no tiene juego en línea."},
#endif
    /* ---- Ajustes y archivos ---- */
    {"Vita Settings",
     "# Dónde\n"
     "Opciones > Mando > Vita Settings\n"
     "# Las filas\n"
     "Touch   Dónde están {L2} {R2} {L3} {R3}\n"
     "Clashes   Gana los choques con {S} o el stick\n"
     "2nd Pad   Un segundo jugador (PS TV)\n"
     "Mods   Tus mods, sí o no\n"
     "Mods, fila de abajo   La música del anime\n"
     "Stick   Stick más lento, si va rápido\n"
     "{S}   Desbloquear todo\n"
     "{ST}   Botones Vita: cámbialos"},
    {"Desbloquear todo",
     "# Lo que consigues\n"
     "Todos los luchadores, formas, escenarios,\n"
     "canciones y objetos, y 4.850.000 Zenis.\n"
     "# Cómo hacerlo\n"
     "En un menú, mantén %K:unlock% un segundo.\n"
     "O: Opciones > Mando > Vita Settings, {S}\n"
     "Luego pulsa {X}.\n"
     "# ¿Te arrepientes?\n"
     "Antes se copia tu partida.\n"
     "En la misma pantalla, {T} la recupera.\n"
     "# Cuidado\n"
     "Los trucos de PS2 no hacen nada aquí.\n"
     "No copies la partida de otro port."},
    {"Música del anime",
     "# Juego en inglés, música del anime\n"
     "1. Copia tu .iso de B14 Rev2 en español\n"
     "a ux0:data/bt4/es/\n"
     "2. Opciones > Mando > Vita Settings:\n"
     "Mods, fila de abajo: ON\n"
     "Textos y voces siguen en inglés.\n"
     "# Todo el juego en español\n"
     "Deja solo la .iso en español en ux0:data/bt4/"},
    {"Archivos del juego (.iso)",
     "# Lo que necesitas\n"
     "Tu propia .iso de B14 Rev2 de Team BT4,\n"
     "en inglés o español. Consíguela de Team BT4.\n"
     "Otras versiones (B13, Beta X) no funcionan.\n"
     "# Dónde va\n"
     "ux0:data/bt4/  (cualquier nombre .iso)\n"
     "# Errores frecuentes\n"
     "Copia la .iso, no el .7z, .rar o .zip.\n"
     "Descomprímela en un ordenador, no en el móvil.\n"
     "Una tarjeta FAT32 no admite más de 4 GB:\n"
     "usa exFAT.\n"
     "\"Archive not found\": copia la .iso otra vez.\n"
     "# Actualizar\n"
     "Instala el VPK nuevo. Tus partidas se quedan."},
    {"Mods",
     "# Dónde van\n"
     "ux0:data/bt4/mods/\n"
     "mods/pzs4us1/00042.bin sustituye\n"
     "al archivo 42 de PZS4US1.AFS.\n"
     "# Sí o no\n"
     "Opciones > Mando > Vita Settings > Mods\n"
     "# Bueno saber\n"
     "Los mods Lua de la versión de PC no van.\n"
     "¿Se para al arrancar? Apaga los Mods."},
    /* ---- Ayuda ---- */
    {"¿Problemas? Envía esto",
     "# Cuéntanoslo en GitHub\n"
     "manumastro/DBZ-BT4-Tag-Team-Decomp-Vita\n"
     "Abre un issue allí.\n"
     "# Adjunta estos archivos\n"
     "De ux0:data/bt4/:\n"
     "log.txt y log-prev.txt\n"
     "hang.txt y err.txt, si los tienes\n"
     "Error C2-12828-1: el .psp2dmp de ux0:data/\n"
     "# Y di\n"
     "Qué estabas haciendo, tu modelo de Vita\n"
     "y tus plugins.\n"
     "# Arreglos rápidos\n"
     "Imagen pequeña con bordes: Sharpscale,\n"
     "modo de escala Original.\n"
     "Escenas lentas: el plugin CapUnlocker."},
    {"Lo que aún no hay",
     "# Quizá más adelante\n"
#ifdef BT4_VITA_NETLINK
     "Tag Team en línea (mira En línea)\n"
#else
     "Juego en línea\n"
#endif
     "Canciones y escenarios nuevos del PC\n"
     "El menú F1 de la versión de PC\n"
     "# Funciona, pero lento\n"
     "Tag Team 3 contra 3 y mayores\n"
     "# No soportado\n"
     "Otras versiones: B13, Beta X"},
    {"Más preguntas",
     "# ¿Este port o el de arielxflores?\n"
     "Los dos usan B14 Rev2. Puedes tener los dos.\n"
     "# ¿Quita BT3?\n"
     "No. Los dos se quedan instalados.\n"
     "# ¿Luchadores de Daima?\n"
     "Solo lo que tiene B14 Rev2.\n"
     "# ¿El juego en español?\n"
     "Usa la .iso de B14 Rev2 en español.\n"
     "# ¿Stick rápido? ¿Choques difíciles?\n"
     "Mira Ajustes y archivos > Vita Settings."},
};

/* The pause menu's page (a fight's controls): the 1 vs 1 page, then the Tag Team page in a Tag fight. %TOUCH% is the
   line that says where L2 / R2 / L3 / R3 are now (Vita Settings > Touch). Same marks as the guide. */
static const char *const kVgFightEn[2] = {
    "{S}   Attack            {T}   Ki blast\n"
    "{X}   Dash              {O}   Guard\n"
    "{L1}   Charge ki       {R1}   Fly up\n"
    "{L2}   Blast wave     {R2}   Fly down\n"
    "Hold %K:quit%   Quit the game\n"
    "%TOUCH%",
    "%K:target%   Change your target\n"
    "Next to a fallen ally   Revive him\n"
    "%K:senzu%   Senzu Bean\n"
    "%K:fusion% + direction   Fusion\n"
    "%K:unfuse%   Undo the fusion",
};
static const char *const kVgFightEs[2] = {
    "{S}   Golpe              {T}   Ráfaga de ki\n"
    "{X}   Impulso           {O}   Defensa\n"
    "{L1}   Cargar ki       {R1}   Subir\n"
    "{L2}   Onda              {R2}   Bajar\n"
    "Mantén %K:quit%   Cerrar el juego\n"
    "%TOUCH%",
    "%K:target%   Cambia de objetivo\n"
    "Junto a un aliado caído   Lo reanimas\n"
    "%K:senzu%   Semilla\n"
    "%K:fusion% + dirección   Fusión\n"
    "%K:unfuse%   Deshacer la fusión",
};

/* The fight's one-line reminder (hudhelp), 1 vs 1 and Tag Team */
static const char *const kVgHudEn[2] = {
    "{L2} {R2} {L3} {R3}: %WHERE%      {ST} > Vita Controls",
    "%K:target%: change target      {ST} > Vita Controls",
};
static const char *const kVgHudEs[2] = {
    "{L2} {R2} {L3} {R3}: %WHERE%      {ST} > Controles Vita",
    "%K:target%: cambiar objetivo      {ST} > Controles Vita",
};

/* The Tag Team setup page's legend (port_tbo.c, the Tag screens' own kit and lines) */
static const char *const kVgTagEn[8] = {
    "IN THE FIGHT", "%K:target%: target", "Near a fallen ally: revive", "%K:senzu%: Senzu Bean",
    "%K:fusion% + direction: fusion", "%K:unfuse%: undo a fusion", "%TOUCHW%", "START > Vita Controls",
};
static const char *const kVgTagEs[8] = {
    "EN COMBATE", "%K:target%: objetivo", "Junto a un aliado caído: reanimar", "%K:senzu%: semilla",
    "%K:fusion% + dirección: fusión", "%K:unfuse%: deshacer fusión", "%TOUCHW%", "START > Controles Vita",
};

#endif
