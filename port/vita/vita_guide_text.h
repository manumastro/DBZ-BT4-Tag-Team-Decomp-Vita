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
   Lines over the page's width wrap by themselves; keep them under ~40 letters so they do not. Avoid the words the
   Vita's text fixes rewrite ("the console", "(PS2)", "PlayStation 2": port/vita/vita_text.c). */
#ifndef VITA_GUIDE_TEXT_H
#define VITA_GUIDE_TEXT_H

#define VG_TOPICS 13

typedef struct VgTopic {
    const char *title;
    const char *body;
} VgTopic;

static const VgTopic kVgEn[VG_TOPICS] = {
    {"Controls",
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
     "These and {L3} {R3}: rear touch pad (next topic).\n"
     "# Pause and quit\n"
     "{ST}   Pause\n"
     "Hold {ST} + {SEL}   Quit the game"},
    {"Touch: L2, R2, L3, R3",
     "# The back of the Vita\n"
     "Touch the rear pad in one of four places:\n"
     "{L2}   Top left\n"
     "{R2}   Top right\n"
     "{L3}   Bottom left\n"
     "{R3}   Bottom right\n"
     "# Another way\n"
     "{SEL} + {L1}   Same as {L2}\n"
     "{SEL} + {R1}   Same as {R2}\n"
     "# Move them\n"
     "Options > Controller > Vita Settings > Touch\n"
     "Put them in the screen's corners, or off.\n"
     "# In the menus\n"
     "Tap an item, tap it again to choose it.\n"
     "Swipe to scroll. Two fingers: go back."},
    {"Tag Team: start",
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
    {"Tag Team: in the fight",
     "# Your target\n"
     "Right stick   Change the enemy you aim at\n"
     "A ring and a health bar mark him.\n"
     "# Help your team\n"
     "Near a fallen ally   Revive him\n"
     "Hold {L2} + press {SEL}   Eat a Senzu Bean\n"
     "{R3} + a direction   Fuse with a near ally\n"
     "{L3} + {R3}   Undo the fusion\n"
     "# Forgot something?\n"
     "In the fight: {ST} > Vita Controls"},
    {"Tag Team: the screen",
     "# What you see\n"
     "Top left   Your health and ki\n"
     "Bottom left   Your team\n"
     "Ring and bar   The enemy you aim at\n"
     "Top right   The radar: every fighter"},
    {"Unlock All",
     "# What you get\n"
     "Every fighter, form, stage and song,\n"
     "every item, and 4,850,000 Zenni.\n"
     "# How to do it\n"
     "In a menu, hold {SEL} for one second.\n"
     "Or: Options > Controller > Vita Settings, {S}\n"
     "Then press {X}.\n"
     "# Changed your mind?\n"
     "Your save is copied first.\n"
     "On the same screen, {T} puts it back.\n"
     "# Careful\n"
     "PS2 cheat codes do nothing here.\n"
     "Never copy another port's save over yours."},
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
     "{S}   Unlock All"},
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
     "Online play\n"
     "Vita to Vita (ad hoc)\n"
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
     "See the topic Vita Settings."},
};

static const VgTopic kVgEs[VG_TOPICS] = {
    {"Controles",
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
     "Estos y {L3} {R3}: panel trasero (tema siguiente).\n"
     "# Pausa y salir\n"
     "{ST}   Pausa\n"
     "Mantén {ST} + {SEL}   Cerrar el juego"},
    {"Táctil: L2, R2, L3, R3",
     "# La parte de atrás de la Vita\n"
     "Toca el panel trasero en uno de cuatro sitios:\n"
     "{L2}   Arriba a la izquierda\n"
     "{R2}   Arriba a la derecha\n"
     "{L3}   Abajo a la izquierda\n"
     "{R3}   Abajo a la derecha\n"
     "# Otra forma\n"
     "{SEL} + {L1}   Igual que {L2}\n"
     "{SEL} + {R1}   Igual que {R2}\n"
     "# Cambiarlos de sitio\n"
     "Opciones > Mando > Vita Settings > Touch\n"
     "Ponlos en las esquinas de la pantalla, o apágalos.\n"
     "# En los menús\n"
     "Toca una opción y tócala otra vez para elegirla.\n"
     "Desliza para bajar. Dos dedos: volver."},
    {"Tag Team: empezar",
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
    {"Tag Team: en combate",
     "# Tu objetivo\n"
     "Stick derecho   Cambia el enemigo al que apuntas\n"
     "Lo marcan un anillo y una barra de vida.\n"
     "# Ayuda a tu equipo\n"
     "Junto a un aliado caído   Lo reanimas\n"
     "Mantén {L2} + pulsa {SEL}   Semilla del ermitaño\n"
     "{R3} + una dirección   Fusión con un aliado\n"
     "{L3} + {R3}   Deshacer la fusión\n"
     "# ¿Se te olvidó algo?\n"
     "En combate: {ST} > Controles Vita"},
    {"Tag Team: la pantalla",
     "# Lo que ves\n"
     "Arriba a la izquierda   Tu vida y tu ki\n"
     "Abajo a la izquierda   Tu equipo\n"
     "Anillo y barra   El enemigo al que apuntas\n"
     "Arriba a la derecha   El radar: todos"},
    {"Desbloquear todo",
     "# Lo que consigues\n"
     "Todos los luchadores, formas, escenarios,\n"
     "canciones y objetos, y 4.850.000 Zenis.\n"
     "# Cómo hacerlo\n"
     "En un menú, mantén {SEL} un segundo.\n"
     "O: Opciones > Mando > Vita Settings, {S}\n"
     "Luego pulsa {X}.\n"
     "# ¿Te arrepientes?\n"
     "Antes se copia tu partida.\n"
     "En la misma pantalla, {T} la recupera.\n"
     "# Cuidado\n"
     "Los trucos de PS2 no hacen nada aquí.\n"
     "No copies la partida de otro port."},
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
     "{S}   Desbloquear todo"},
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
     "Juego en línea\n"
     "Vita con Vita (ad hoc)\n"
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
     "Mira el tema Vita Settings."},
};

/* The pause menu's page (a fight's controls): the 1 vs 1 page, then the Tag Team page in a Tag fight. %TOUCH% is the
   line that says where L2 / R2 / L3 / R3 are now (Vita Settings > Touch). Same marks as the guide. */
static const char *const kVgFightEn[2] = {
    "{S}   Attack            {T}   Ki blast\n"
    "{X}   Dash              {O}   Guard\n"
    "{L1}   Charge ki       {R1}   Fly up\n"
    "{L2}   Blast wave     {R2}   Fly down\n"
    "Hold {ST} + {SEL}   Quit the game\n"
    "%TOUCH%",
    "Right stick   Change your target\n"
    "Next to a fallen ally   Revive him\n"
    "Hold {L2} + press {SEL}   Senzu Bean\n"
    "{R3} + direction   Fusion\n"
    "{L3} + {R3}   Undo the fusion",
};
static const char *const kVgFightEs[2] = {
    "{S}   Golpe              {T}   Ráfaga de ki\n"
    "{X}   Impulso           {O}   Defensa\n"
    "{L1}   Cargar ki       {R1}   Subir\n"
    "{L2}   Onda              {R2}   Bajar\n"
    "Mantén {ST} + {SEL}   Cerrar el juego\n"
    "%TOUCH%",
    "Stick derecho   Cambia de objetivo\n"
    "Junto a un aliado caído   Lo reanimas\n"
    "Mantén {L2} + pulsa {SEL}   Semilla\n"
    "{R3} + dirección   Fusión\n"
    "{L3} + {R3}   Deshacer la fusión",
};

/* The fight's one-line reminder (hudhelp), 1 vs 1 and Tag Team */
static const char *const kVgHudEn[2] = {
    "{L2} {R2} {L3} {R3}: %WHERE%      {ST} > Vita Controls",
    "Right stick: change target      {ST} > Vita Controls",
};
static const char *const kVgHudEs[2] = {
    "{L2} {R2} {L3} {R3}: %WHERE%      {ST} > Controles Vita",
    "Stick derecho: cambiar objetivo      {ST} > Controles Vita",
};

/* The Tag Team setup page's legend (port_tbo.c, the Tag screens' own kit and lines) */
static const char *const kVgTagEn[8] = {
    "IN THE FIGHT", "Right stick: target", "Near a fallen ally: revive", "L2 + SELECT: Senzu Bean",
    "R3 + direction: fusion", "L3 + R3: undo a fusion", "L2 R2 L3 R3: rear touch pad", "START > Vita Controls",
};
static const char *const kVgTagEs[8] = {
    "EN COMBATE", "Stick derecho: objetivo", "Junto a un aliado caído: reanimar", "L2 + SELECT: semilla",
    "R3 + dirección: fusión", "L3 + R3: deshacer fusión", "L2 R2 L3 R3: panel trasero", "START > Controles Vita",
};

#endif
