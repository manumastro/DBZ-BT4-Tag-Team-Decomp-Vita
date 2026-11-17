/*
 * BT4decomp's free / spectator camera on the Vita: its own port/src/gs/spectator_camera.c (R1 1.3, "Tag Team free
 * view"), built unchanged. The Tag Team screens' Camera mode sets it (port_tbo.c: Camera mode Game camera / Free camera
 * / Follow fighter / Fight overview, Fighter, Camera speed, Camera zoom, Reset camera; saved as the PC saves them,
 * spectator_mode, spectator_target, spectator_speed, spectator_zoom); the fight's camera asks it every step
 * (src/battle/btl_cam.c BtlCam_PortFreeCamera) and draws from where it says, the game's own camera left as it was.
 * While it is on, player 1's pad drives it instead of the fighter (START and SELECT still reach the game), as on the
 * PC: the pad's report is filtered by port/vita/vita_gfx.c Port_PadRead, and its sticks and shoulders are read by
 * PortInput_Spectator there.
 *
 * bt3.txt tagcam: 1 (default) upstream's camera; 0 the game's own camera always, as before (the Camera mode rows
 * shown, doing nothing: PortSpectator_Mode answers 0, the pad not filtered).
 */
#define PortSpectator_Mode PortSpectator_ModeUp
#include "spectator_camera.c"
#undef PortSpectator_Mode

int gVitaTagCam = 1;

int PortSpectator_Mode(void) {
    return gVitaTagCam ? PortSpectator_ModeUp() : 0;
}
