/* PS Vita: the dozen SDL3 audio functions the PC port's sound code uses (port/src/gs/snd_adx.c, snd_se.c), on the
   Vita's own audio output (port/vita/vita_audio.c). The two sound files build unchanged against this. */
#ifndef BT3_VITA_SDL_SHIM_H
#define BT3_VITA_SDL_SHIM_H
#include <stdbool.h>
#include <stdint.h>
#define SDLCALL
#define SDL_INIT_AUDIO 0x10u
#define SDL_AUDIO_S16 0x8010
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK 0xFFFFFFFFu
typedef uint32_t SDL_AudioDeviceID;
typedef struct SDL_AudioSpec { int format, channels, freq; } SDL_AudioSpec;
typedef struct SDL_AudioStream SDL_AudioStream;
typedef void (SDLCALL *SDL_AudioStreamCallback)(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount);
bool SDL_InitSubSystem(uint32_t flags);
SDL_AudioDeviceID SDL_OpenAudioDevice(SDL_AudioDeviceID id, const SDL_AudioSpec *spec);
bool SDL_ResumeAudioDevice(SDL_AudioDeviceID dev);
const char *SDL_GetCurrentAudioDriver(void);
const char *SDL_GetError(void);
SDL_AudioStream *SDL_CreateAudioStream(const SDL_AudioSpec *src, const SDL_AudioSpec *dst);
bool SDL_SetAudioStreamGetCallback(SDL_AudioStream *stream, SDL_AudioStreamCallback cb, void *userdata);
bool SDL_BindAudioStream(SDL_AudioDeviceID dev, SDL_AudioStream *stream);
bool SDL_PutAudioStreamData(SDL_AudioStream *stream, const void *buf, int len);
bool SDL_SetAudioStreamGain(SDL_AudioStream *stream, float gain);
bool SDL_LockAudioStream(SDL_AudioStream *stream);
bool SDL_UnlockAudioStream(SDL_AudioStream *stream);
bool SDL_ClearAudioStream(SDL_AudioStream *stream);
void SDL_DestroyAudioStream(SDL_AudioStream *stream);
/* (BT4decomp's sound code: milliseconds since the start, for its logs) */
#include <psp2/kernel/processmgr.h>
static inline uint64_t SDL_GetTicks(void) { return sceKernelGetProcessTimeWide() / 1000u; }
/* (BT4decomp's fighters' sounds by distance, gs/snd_spatial.c) */
typedef uint64_t Uint64;
typedef int SDL_SpinLock;
static inline void SDL_LockSpinlock(SDL_SpinLock *l) { while (__atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE)) {} }
static inline void SDL_UnlockSpinlock(SDL_SpinLock *l) { __atomic_store_n(l, 0, __ATOMIC_RELEASE); }
/* (BT4decomp's recording of its own mix for tests: not on the Vita) */
typedef void (SDLCALL *SDL_AudioPostmixCallback)(void *userdata, const SDL_AudioSpec *spec, float *buffer, int buflen);
static inline bool SDL_SetAudioPostmixCallback(SDL_AudioDeviceID dev, SDL_AudioPostmixCallback cb, void *userdata) {
    (void)dev; (void)cb; (void)userdata;
    return false;
}
#endif
