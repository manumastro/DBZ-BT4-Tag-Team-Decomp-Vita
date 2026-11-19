/* The Vita's own controls and the player's bindings of them (port/vita/vita_remap.c). */
#ifndef VITA_REMAP_H
#define VITA_REMAP_H

enum { VR_L2, VR_R2, VR_L3, VR_R3, VR_SENZU, VR_FUSION, VR_UNFUSE, VR_TARGET, VR_QUIT, VR_UNLOCK, VR_COUNT };
enum { VR_CAP_WAIT, VR_CAP_GOT, VR_CAP_NONE, VR_CAP_CANCEL, VR_CAP_ALONE };

extern int gVitaRemap;
int Vita_RemapOn(void);
unsigned Vita_RemapGet(int a);
unsigned Vita_RemapDefault(int a);
const char *Vita_RemapName(int a);
int Vita_RemapConflict(int a, unsigned m);
int Vita_RemapValid(int a, unsigned m);
void Vita_RemapSet(int a, unsigned m);
void Vita_RemapDefaults(void);
int Vita_RemapOverride(const char *name, const char *value);
unsigned Vita_RemapPad(unsigned *raw);
int Vita_RemapHeld(int a, unsigned b);
void Vita_RemapCaptureBegin(void);
void Vita_RemapCaptureEnd(void);
int Vita_RemapCapturing(void);
int Vita_RemapCapture(unsigned *m);
int Vita_RemapAllUp(void);
int Vita_RemapMaskText(unsigned m, char *out, int size, int words);
void Vita_RemapActionText(int a, char *out, int size, int words);
char *Vita_RemapExpand(const char *in, char *out, int size, int words);

#endif
