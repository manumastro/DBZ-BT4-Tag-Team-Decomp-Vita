/* The Vita Guide's words (port/vita/vita_guide.c), English and Spanish, in UTF-8. Written for every player: short
   sentences, what to press, where to go. The contents are the players' questions on Reddit, most asked first
   (psvita-agent notes/reddit-review-2026-11-19.md), and the README's answers. Keep them true to the code: a change of
   a control, a folder or a setting changes its line here too.

   Marks turned into the game's own button pictures (its font's <PAD=> tag, the pause menu's controls page's way):
   {X} cross, {O} circle, {S} square, {T} triangle, {L1} {R1} {L2} {R2} {L3} {R3}, {SEL} SELECT, {ST} START,
   {U} {D} {LT} {RT} the D-pad's arrows. "\n" starts a line, "\n\n" leaves a line empty. A topic longer than a page goes
   on to the next page by itself. Avoid the words the Vita's text fixes rewrite ("the console", "(PS2)",
   "PlayStation 2": port/vita/vita_text.c). */
#ifndef VITA_GUIDE_TEXT_H
#define VITA_GUIDE_TEXT_H

#define VG_TOPICS 13

typedef struct VgTopic {
    const char *title;
    const char *body;
} VgTopic;

static const VgTopic kVgEn[VG_TOPICS] = {
    {"Controls",
     "Left stick: move.   {S} fight   {T} ki blast   {X} dash   {O} guard\n"
     "{L1} (the Vita's L): ki charge.   {R1} (R): ascend.\n"
     "{L2} blaster wave, {R2} descend, {L3}, {R3}: the Vita has no such buttons. They are on the rear touch pad "
     "(next topic).\n\n"
     "{ST}: pause. In a fight, pause > Vita Controls shows that fight's controls.\n"
     "Close the game: hold {ST} + {SEL} for two seconds.\n"
     "PS TV, or a paired DualShock: its own L2, R2, L3 and R3 work.\n"
     "Other buttons: Options > Controller > Control Type > Custom."},
    {"Touch: L2, R2, L3, R3",
     "The rear touch pad, in four quarters:\n"
     "upper left {L2}   upper right {R2}   lower left {L3}   lower right {R3}\n\n"
     "Options > Controller > Vita Settings > Touch moves each one to a corner of the front screen "
     "({L2} {R2} the bottom corners, {L3} {R3} the top ones) or turns it off.\n"
     "{SEL} + {L1} / {R1} also give {L2} / {R2}.\n\n"
     "In the menus: tap an item to move to it, tap it again to choose it. Swipe to scroll. Tap with two fingers to "
     "go back. A tap skips a film."},
    {"Tag Team: start and setup",
     "Main menu > Tag Team (or Duel > Tag).\n\n"
     "Local Battle: choose the Mode (Singleplayer: you and CPU allies against CPUs), the size of each team, the time "
     "and the CPU level, then Select Fighters. The help at the bottom says what each row does.\n"
     "Recovery settings, Fighter settings and Battle rules hold the PC version's extra rules (revival, Senzu Beans, "
     "fusions, health).\n\n"
     "Character select: the teams pick in turns, then the stage and the music.\n\n"
     "2 vs 2 plays well. 3 vs 3 and bigger teams work, but are still slow on the Vita."},
    {"Tag Team: in the fight",
     "Right stick: choose the enemy you target (a ring and a health bar mark him).\n"
     "Tag Team > Camera mode > Target selection: right stick (the default), {L3} or the D-pad.\n\n"
     "Stay near a downed ally: you revive him. More helpers are faster; moving or being hit stops it.\n"
     "Hold {L2} and press {SEL}: eat a Senzu Bean, if the rules give you any.\n"
     "{R3} + a direction, near a compatible ally: fusion. {L3} + {R3} splits a temporary one.\n\n"
     "Everything else: the game's usual controls.\n"
     "In the fight, pause > Vita Controls shows all of this again."},
    {"Tag Team: the HUD",
     "Your gauges: top left, as in a 1 vs 1 fight.\n"
     "Your allies: the column at the bottom left.\n"
     "The enemy you target: the ring around him and his bar.\n"
     "The radar, top right: where every fighter is.\n\n"
     "Text and HUD sizes: tagui_scale and taghud_scale (percent) in ux0:data/bt4/bt3.txt, for the development build."},
    {"Unlock All",
     "Every character and form, stage, song and item, and 4,850,000 Zenni.\n\n"
     "Open its screen in one of three ways:\n"
     "- hold {SEL} for about a second in the menus;\n"
     "- Options > Controller > Vita Settings, then {S};\n"
     "- press {SEL} as the game starts.\n"
     "Then {X}. Your save is copied first: on the same screen {T} undoes it.\n\n"
     "PS2 cheat codes do nothing here. Do not copy another port's save over yours: use Unlock All.\n"
     "A fighter still locked after it? Send log.txt (topic Problems)."},
    {"Vita Settings",
     "Options > Controller > Vita Settings:\n"
     "Touch: where L2, R2, L3, R3 are (rear pad, front corner, off).\n"
     "Clashes: Square (default) wins beam and blow clashes with {S}; Stick wins them by turning the left stick. "
     "ON / OFF below: the clash assist (hold the button, it presses for you).\n"
     "2nd Pad: a second controller plays as player 2 (PS TV).\n"
     "Mods: use the files in ux0:data/bt4/mods/. Its lower row: the anime soundtrack.\n"
     "Stick (press {D} from the lower row): a dead zone of 0 to 40% and a soft response, if the sticks feel too quick.\n"
     "{S}: Unlock All.\n\n"
     "Controls reminder in fights: pause > Vita Controls, then {S}."},
    {"Anime soundtrack",
     "The English game with the anime music of the Spanish release:\n"
     "1. Copy your own Spanish B14 Rev2 .iso to ux0:data/bt4/es/ (the English one stays where it is).\n"
     "2. Options > Controller > Vita Settings > Mods, lower row: ON.\n\n"
     "The texts and voices stay English; the songs change from the next one.\n"
     "Only the Spanish .iso in ux0:data/bt4/: the whole game in Spanish, with its music."},
    {"Game files (the .iso)",
     "You need your own image of Team BT4's B14 Rev2 for PS2: English (5,914,853,376 bytes) or Spanish "
     "(5,396,742,144 bytes). Get it from Team BT4's official release (an ad blocker helps on download pages). "
     "Other builds (B13, Beta X, Daima ones) do not work.\n\n"
     "Copy the .iso (any name ending in .iso) to ux0:data/bt4/.\n"
     "The .iso, not the .7z, .zip or .rar: unpack those on a computer with 7-Zip, not on a phone.\n"
     "A FAT32 card cannot hold a file over 4 GB: format it as exFAT, or copy SLUS_219.78, BIN and DATA to "
     "ux0:data/bt4/disc/.\n"
     "\"Archive not found\" or \"cut short\": the .iso is too small. Copy it again.\n"
     "An image in ux0:data/bt3/bt4/ (the BT3 port's) is found too.\n"
     "Updating: install the new VPK over the old one. Saves and the .iso stay."},
    {"Mods",
     "Mods go in ux0:data/bt4/mods/, as for the BT3 Vita port: mods/pzs4us1/00042.bin takes the place of file 42 of "
     "PZS4US1.AFS; a pack can have a folder of its own.\n\n"
     "Vita Settings > Mods shows what is in use, and turns it all off from the next start without deleting anything.\n"
     "Mods that change data files work. The PC version's Lua mods do not.\n"
     "Very large mods may not fit in the Vita's memory: if the game stops at the start, turn Mods off."},
    {"Problems? Send this",
     "Open an issue on the port's GitHub page (manumastro/DBZ-BT4-Tag-Team-Decomp-Vita) and attach, from "
     "ux0:data/bt4/:\n"
     "- log.txt (and log-prev.txt);\n"
     "- hang.txt and err.txt, if they are there;\n"
     "- after an error C2-12828-1: the .psp2dmp file in ux0:data/.\n"
     "Say what you were doing, your Vita (1000, 2000 or PS TV), the game's version and your plugins.\n\n"
     "Picture smaller, with borders, in fights only: set Sharpscale's scaling mode to Original.\n"
     "Heavy scenes slow: the CapUnlocker plugin helps."},
    {"Not there yet",
     "Online play, and matches with the PC version: not on the Vita yet.\n"
     "Ad hoc between two Vitas: not yet.\n"
     "Tag Team 3 vs 3 and bigger: they work, but slowly. 2 vs 2 is smooth.\n"
     "The PC version's added songs and stages, its F1 menu (Lua mods, HUD editor) and a language choice in the "
     "game.\n"
     "Other builds than B14 Rev2 (B13, Beta X)."},
    {"More questions",
     "This port or arielxflores's? Both run B14 Rev2. This one is BT4 only, an app of its own with its own "
     "speed-ups. They install side by side.\n"
     "BT3 stays installed: this app does not replace it.\n"
     "Daima characters or missions? Only what Team BT4's B14 Rev2 has.\n"
     "Sticks too quick? Vita Settings > Stick. Clashes hard? Vita Settings > Clashes and its assist.\n"
     "The game in Spanish? Use the Spanish B14 Rev2 image.\n"
     "Updating? The new VPK over the old one."},
};

static const VgTopic kVgEs[VG_TOPICS] = {
    {"Controles",
     "Stick izquierdo: moverse.   {S} golpe   {T} ráfaga de ki   {X} impulso   {O} defensa\n"
     "{L1} (la L de la Vita): cargar ki.   {R1} (R): subir.\n"
     "{L2} onda, {R2} bajar, {L3}, {R3}: la Vita no tiene esos botones. Están en el panel táctil trasero "
     "(tema siguiente).\n\n"
     "{ST}: pausa. En combate, pausa > Controles Vita muestra los controles de ese combate.\n"
     "Cerrar el juego: mantén {ST} + {SEL} dos segundos.\n"
     "PS TV o un DualShock emparejado: sus propios L2, R2, L3 y R3 funcionan.\n"
     "Otros botones: Opciones > Mando > Tipo de control > Personalizado."},
    {"Táctil: L2, R2, L3, R3",
     "El panel táctil trasero, en cuatro cuartos:\n"
     "arriba a la izquierda {L2}   arriba a la derecha {R2}   abajo a la izquierda {L3}   abajo a la derecha {R3}\n\n"
     "Opciones > Mando > Vita Settings > Touch lleva cada uno a una esquina de la pantalla "
     "({L2} {R2} las esquinas de abajo, {L3} {R3} las de arriba) o lo apaga.\n"
     "{SEL} + {L1} / {R1} también dan {L2} / {R2}.\n\n"
     "En los menús: toca una opción para ir a ella y tócala otra vez para elegirla. Desliza para desplazarte. "
     "Toca con dos dedos para volver. Un toque salta un vídeo."},
    {"Tag Team: empezar y preparar",
     "Menú principal > Tag Team (o Duelo > Tag).\n\n"
     "Local Battle: elige el modo (Singleplayer: tú y aliados CPU contra la CPU), el tamaño de cada equipo, el tiempo "
     "y el nivel de la CPU, y luego Select Fighters. La ayuda de abajo dice qué hace cada fila.\n"
     "Recovery settings, Fighter settings y Battle rules tienen las reglas extra de la versión de PC (revivir, "
     "semillas del ermitaño, fusiones, vida).\n\n"
     "Selección de personajes: los equipos eligen por turnos, luego el escenario y la música.\n\n"
     "2 contra 2 va bien. 3 contra 3 y equipos mayores funcionan, pero aún van lentos en la Vita."},
    {"Tag Team: en combate",
     "Stick derecho: elige el enemigo al que apuntas (lo marcan un anillo y una barra de vida).\n"
     "Tag Team > Camera mode > Target selection: stick derecho (por defecto), {L3} o la cruceta.\n\n"
     "Quédate cerca de un aliado caído: lo reanimas. Con más ayudantes es más rápido; moverte o recibir un golpe "
     "lo corta.\n"
     "Mantén {L2} y pulsa {SEL}: comes una semilla del ermitaño, si las reglas te dan alguna.\n"
     "{R3} + una dirección, cerca de un aliado compatible: fusión. {L3} + {R3} separa una temporal.\n\n"
     "Todo lo demás: los controles de siempre.\n"
     "En combate, pausa > Controles Vita lo muestra otra vez."},
    {"Tag Team: el HUD",
     "Tus barras: arriba a la izquierda, como en un combate 1 contra 1.\n"
     "Tus aliados: la columna de abajo a la izquierda.\n"
     "El enemigo al que apuntas: el anillo a su alrededor y su barra.\n"
     "El radar, arriba a la derecha: dónde está cada luchador.\n\n"
     "Tamaños del texto y del HUD: tagui_scale y taghud_scale (por ciento) en ux0:data/bt4/bt3.txt, en la versión "
     "de desarrollo."},
    {"Desbloquear todo",
     "Todos los personajes y formas, escenarios, canciones y objetos, y 4.850.000 Zenis.\n\n"
     "Abre su pantalla de una de estas tres formas:\n"
     "- mantén {SEL} un segundo en los menús;\n"
     "- Opciones > Mando > Vita Settings, y luego {S};\n"
     "- pulsa {SEL} al arrancar el juego.\n"
     "Luego {X}. Antes se copia tu partida: en la misma pantalla {T} lo deshace.\n\n"
     "Los trucos de PS2 no hacen nada aquí. No copies la partida de otro port sobre la tuya: usa Desbloquear todo.\n"
     "¿Sigue bloqueado algún luchador? Envía log.txt (tema Problemas)."},
    {"Vita Settings",
     "Opciones > Mando > Vita Settings:\n"
     "Touch: dónde están L2, R2, L3, R3 (panel trasero, esquina de la pantalla, apagado).\n"
     "Clashes: Square (por defecto) gana los choques de rayos y de golpes con {S}; Stick los gana girando el stick "
     "izquierdo. ON / OFF debajo: la ayuda en los choques (mantén el botón y pulsa por ti).\n"
     "2nd Pad: un segundo mando juega como jugador 2 (PS TV).\n"
     "Mods: usa los archivos de ux0:data/bt4/mods/. Su fila de abajo: la banda sonora del anime.\n"
     "Stick (pulsa {D} desde la fila de abajo): una zona muerta del 0 al 40% y una respuesta suave, si los sticks van "
     "demasiado rápidos.\n"
     "{S}: Desbloquear todo.\n\n"
     "Recordatorio de controles en combate: pausa > Controles Vita, y luego {S}."},
    {"Banda sonora del anime",
     "El juego en inglés con la música del anime de la versión en español:\n"
     "1. Copia tu .iso de B14 Rev2 en español a ux0:data/bt4/es/ (la inglesa se queda donde está).\n"
     "2. Opciones > Mando > Vita Settings > Mods, fila de abajo: ON.\n\n"
     "Textos y voces siguen en inglés; las canciones cambian desde la siguiente.\n"
     "Solo la .iso en español en ux0:data/bt4/: todo el juego en español, con su música."},
    {"Archivos del juego (la .iso)",
     "Necesitas tu propia imagen de B14 Rev2 de Team BT4 para PS2: inglés (5.914.853.376 bytes) o español "
     "(5.396.742.144 bytes). Consíguela de la versión oficial de Team BT4 (un bloqueador de anuncios ayuda en las "
     "páginas de descarga). Otras versiones (B13, Beta X, las de Daima) no funcionan.\n\n"
     "Copia la .iso (cualquier nombre que acabe en .iso) a ux0:data/bt4/.\n"
     "La .iso, no el .7z, .zip o .rar: descomprímelos en un ordenador con 7-Zip, no en el móvil.\n"
     "Una tarjeta FAT32 no admite archivos de más de 4 GB: formátala en exFAT, o copia SLUS_219.78, BIN y DATA a "
     "ux0:data/bt4/disc/.\n"
     "\"Archive not found\" o \"cut short\": la .iso es demasiado pequeña. Cópiala otra vez.\n"
     "Una imagen en ux0:data/bt3/bt4/ (la del port de BT3) también se encuentra.\n"
     "Actualizar: instala el VPK nuevo sobre el viejo. Las partidas y la .iso se quedan."},
    {"Mods",
     "Los mods van en ux0:data/bt4/mods/, como en el port de BT3 para Vita: mods/pzs4us1/00042.bin sustituye al "
     "archivo 42 de PZS4US1.AFS; un pack puede tener su propia carpeta.\n\n"
     "Vita Settings > Mods muestra lo que se usa, y lo apaga todo desde el siguiente arranque sin borrar nada.\n"
     "Los mods que cambian archivos de datos funcionan. Los mods Lua de la versión de PC no.\n"
     "Los mods muy grandes pueden no caber en la memoria de la Vita: si el juego se para al arrancar, apaga Mods."},
    {"¿Problemas? Envía esto",
     "Abre un issue en la página de GitHub del port (manumastro/DBZ-BT4-Tag-Team-Decomp-Vita) y adjunta, de "
     "ux0:data/bt4/:\n"
     "- log.txt (y log-prev.txt);\n"
     "- hang.txt y err.txt, si están;\n"
     "- tras un error C2-12828-1: el archivo .psp2dmp de ux0:data/.\n"
     "Di qué estabas haciendo, tu Vita (1000, 2000 o PS TV), la versión del juego y tus plugins.\n\n"
     "Imagen más pequeña, con bordes, solo en combate: pon el modo de escala de Sharpscale en Original.\n"
     "Escenas pesadas lentas: el plugin CapUnlocker ayuda."},
    {"Lo que aún no hay",
     "Juego en línea, y partidas con la versión de PC: aún no en la Vita.\n"
     "Ad hoc entre dos Vitas: aún no.\n"
     "Tag Team 3 contra 3 y mayores: funcionan, pero lentos. 2 contra 2 va fluido.\n"
     "Las canciones y escenarios añadidos de la versión de PC, su menú F1 (mods Lua, editor del HUD) y elegir idioma "
     "en el juego.\n"
     "Versiones distintas de B14 Rev2 (B13, Beta X)."},
    {"Más preguntas",
     "¿Este port o el de arielxflores? Los dos usan B14 Rev2. Este es solo BT4, una app propia con sus propias "
     "mejoras de velocidad. Se instalan uno al lado del otro.\n"
     "BT3 se queda instalado: esta app no lo sustituye.\n"
     "¿Personajes o misiones de Daima? Solo lo que tiene B14 Rev2 de Team BT4.\n"
     "¿Sticks muy rápidos? Vita Settings > Stick. ¿Choques difíciles? Vita Settings > Clashes y su ayuda.\n"
     "¿El juego en español? Usa la imagen de B14 Rev2 en español.\n"
     "¿Actualizar? El VPK nuevo sobre el viejo."},
};

/* The pause menu's page (a fight's controls): the 1 vs 1 page, then the Tag Team page in a Tag fight. %TOUCH% is the
   line that says where L2 / R2 / L3 / R3 are now (Vita Settings > Touch). */
static const char *const kVgFightEn[2] = {
    "{S} fight   {T} ki blast   {X} dash   {O} guard\n"
    "{L1} ki charge   {R1} ascend   {L2} blaster wave   {R2} descend\n"
    "%TOUCH%\n"
    "{SEL} + {L1} / {R1} also give {L2} / {R2}.\n"
    "Clashes: press {S} again and again (or {T}).\n"
    "Close the game: hold {ST} + {SEL} for two seconds.\n"
    "Everything else: main menu > Vita Guide.",
    "Right stick: change the enemy you target.\n"
    "Stay near a downed ally: revive him.\n"
    "Hold {L2} + press {SEL}: Senzu Bean.\n"
    "{R3} + direction near an ally: fusion.\n"
    "{L3} + {R3}: split a temporary fusion.\n"
    "Your allies: bottom left. Radar: top right.",
};
static const char *const kVgFightEs[2] = {
    "{S} golpe   {T} ráfaga de ki   {X} impulso   {O} defensa\n"
    "{L1} cargar ki   {R1} subir   {L2} onda   {R2} bajar\n"
    "%TOUCH%\n"
    "{SEL} + {L1} / {R1} también dan {L2} / {R2}.\n"
    "Choques: pulsa {S} una y otra vez (o {T}).\n"
    "Cerrar el juego: mantén {ST} + {SEL} dos segundos.\n"
    "Todo lo demás: menú principal > Guía Vita.",
    "Stick derecho: cambia el enemigo al que apuntas.\n"
    "Quédate cerca de un aliado caído: lo reanimas.\n"
    "Mantén {L2} + pulsa {SEL}: semilla del ermitaño.\n"
    "{R3} + dirección cerca de un aliado: fusión.\n"
    "{L3} + {R3}: separa una fusión temporal.\n"
    "Tus aliados: abajo a la izquierda. Radar: arriba a la derecha.",
};

/* The fight's one-line reminder (hudhelp), 1 vs 1 and Tag Team */
static const char *const kVgHudEn[2] = {
    "{L2} {R2} {L3} {R3}: %WHERE%   {ST}: Vita Controls",
    "Right stick: target   {L2}+{SEL}: Senzu   {R3}+dir: fusion   {ST}: Vita Controls",
};
static const char *const kVgHudEs[2] = {
    "{L2} {R2} {L3} {R3}: %WHERE%   {ST}: Controles Vita",
    "Stick dcho.: objetivo   {L2}+{SEL}: semilla   {R3}+dir.: fusión   {ST}: Controles Vita",
};

/* The Tag Team setup page's legend (port_tbo.c, the Tag screens' own kit and lines) */
static const char *const kVgTagEn[8] = {
    "IN THE FIGHT", "Right stick: target", "Near a downed ally: revive", "L2 + SELECT: Senzu Bean",
    "R3 + direction: fusion", "L3 + R3: split a fusion", "L2 R2 L3 R3: rear touch pad", "START > Vita Controls",
};
static const char *const kVgTagEs[8] = {
    "EN COMBATE", "Stick derecho: objetivo", "Cerca de un aliado caído: reanimar", "L2 + SELECT: semilla",
    "R3 + dirección: fusión", "L3 + R3: separar fusión", "L2 R2 L3 R3: panel trasero", "START > Controles Vita",
};

#endif
