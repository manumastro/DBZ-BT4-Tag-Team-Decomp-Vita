/* PS Vita: the dozen SDL3 audio functions the PC port's sound code uses (port/src/gs/snd_adx.c, snd_se.c), on the
   Vita's own audio output (port/vita/vita_audio.c). The two sound files build unchanged against this. */
#ifndef BT3_VITA_SDL_SHIM_H
#define BT3_VITA_SDL_SHIM_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus /* (BT4decomp's online signaling, gs/supabase_signal.cpp, is C++) */
extern "C" {
#endif
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
/* (BT4decomp's free camera, gs/spectator_camera.c through port/vita/vita_spectator.c: the time between two steps) */
static inline Uint64 SDL_GetTicksNS(void) { return (Uint64)sceKernelGetProcessTimeWide() * 1000u; }
typedef int SDL_SpinLock;
/* Not a bare spin: the sound thread (higher priority) taking a lock the game thread holds on the same core spun for
   ever, the game thread never running again to let it go (a hang in Tag Team, whose extra fighters' voices take it
   more often). As SDL's own, it gives the processor away after a while. */
#include <psp2/kernel/threadmgr.h>
static inline void SDL_LockSpinlock(SDL_SpinLock *l) {
    int n = 0;
    while (__atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE)) {
        if (++n >= 100) {
            sceKernelDelayThread(20);
        }
    }
}
static inline void SDL_UnlockSpinlock(SDL_SpinLock *l) { __atomic_store_n(l, 0, __ATOMIC_RELEASE); }
/* (BT4decomp's Tag Team radar cue, gs/snd_se.c: a request handed to the sound thread; SDL's Set returns the old value) */
typedef struct SDL_AtomicInt { int value; } SDL_AtomicInt;
static inline int SDL_SetAtomicInt(SDL_AtomicInt *a, int v) { return __atomic_exchange_n(&a->value, v, __ATOMIC_SEQ_CST); }
static inline int SDL_GetAtomicInt(SDL_AtomicInt *a) { return __atomic_load_n(&a->value, __ATOMIC_SEQ_CST); }
/* (BT4decomp's TrueType lines of its own screens, gs/tbo_text.c: the copy of a line's text) */
#include <stddef.h>
static inline size_t SDL_strlcpy(char *dst, const char *src, size_t size) {
    size_t n = 0;
    while (src[n] != '\0') {
        n++;
    }
    if (size > 0) {
        size_t k = n < size - 1 ? n : size - 1, i;
        for (i = 0; i < k; i++) {
            dst[i] = src[i];
        }
        dst[k] = '\0';
    }
    return n;
}
/* (BT4decomp's recording of its own mix for tests: not on the Vita) */
typedef void (SDLCALL *SDL_AudioPostmixCallback)(void *userdata, const SDL_AudioSpec *spec, float *buffer, int buflen);
static inline bool SDL_SetAudioPostmixCallback(SDL_AudioDeviceID dev, SDL_AudioPostmixCallback cb, void *userdata) {
    (void)dev; (void)cb; (void)userdata;
    return false;
}
/* ---- BT4decomp's network files (gs/net*.c, gs/supabase_signal.cpp + cloudflare_signal.inc, curl_ws_io.h,
   voice_transport_curl.inc, build_identity.c): threads, locks, semaphores, files, folders, random numbers, counters.
   On the Vita's kernel and C library: port/vita/vita_sdl.c (built only with the online part, BT4_VITA_ONLINE) ---- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef int8_t Sint8;
typedef uint8_t Uint8;
typedef int16_t Sint16;
typedef uint16_t Uint16;
typedef int32_t Sint32;
typedef uint32_t Uint32;
typedef int64_t Sint64;
static inline void SDL_Delay(Uint32 ms) { sceKernelDelayThread(ms * 1000u); }
static inline void SDL_DelayNS(Uint64 ns) { sceKernelDelayThread((SceUInt)(ns / 1000u)); }
static inline Uint64 SDL_GetPerformanceCounter(void) { return sceKernelGetProcessTimeWide(); }
static inline Uint64 SDL_GetPerformanceFrequency(void) { return 1000000u; }
static inline char *SDL_getenv(const char *name) { return getenv(name); }
static inline void *SDL_malloc(size_t n) { return malloc(n); }
static inline void SDL_free(void *p) { free(p); }
static inline double SDL_sin(double x) { return sin(x); }
static inline double SDL_fabs(double x) { return fabs(x); }
#define SDL_snprintf snprintf
#define SDL_vsnprintf vsnprintf
static inline int SDL_AddAtomicInt(SDL_AtomicInt *a, int v) { return __atomic_fetch_add(&a->value, v, __ATOMIC_SEQ_CST); }
static inline bool SDL_CompareAndSwapAtomicInt(SDL_AtomicInt *a, int oldval, int newval) {
    return __atomic_compare_exchange_n(&a->value, &oldval, newval, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
/* random numbers of their own (never the game's generators) */
Sint32 SDL_rand(Sint32 n);
Uint32 SDL_rand_bits(void);
void SDL_srand(Uint64 seed);
/* threads: a kernel thread each, off the game's core, below the game's and the renderer's priorities */
typedef struct SDL_Thread SDL_Thread;
typedef int (SDLCALL *SDL_ThreadFunction)(void *data);
typedef Uint64 SDL_ThreadID;
SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data);
void SDL_WaitThread(SDL_Thread *thread, int *status);
void SDL_DetachThread(SDL_Thread *thread);
SDL_ThreadID SDL_GetCurrentThreadID(void);
typedef enum SDL_ThreadPriority { SDL_THREAD_PRIORITY_LOW, SDL_THREAD_PRIORITY_NORMAL, SDL_THREAD_PRIORITY_HIGH, SDL_THREAD_PRIORITY_TIME_CRITICAL } SDL_ThreadPriority;
static inline bool SDL_SetCurrentThreadPriority(SDL_ThreadPriority p) { (void)p; return true; }
/* recursive locks, counting semaphores (SDL_WaitSemaphoreTimeout: ms, -1 for ever; true when taken) */
typedef struct SDL_Mutex SDL_Mutex;
SDL_Mutex *SDL_CreateMutex(void);
void SDL_LockMutex(SDL_Mutex *m);
bool SDL_TryLockMutex(SDL_Mutex *m);
void SDL_UnlockMutex(SDL_Mutex *m);
void SDL_DestroyMutex(SDL_Mutex *m);
typedef struct SDL_Semaphore SDL_Semaphore;
SDL_Semaphore *SDL_CreateSemaphore(Uint32 initial);
void SDL_DestroySemaphore(SDL_Semaphore *s);
void SDL_SignalSemaphore(SDL_Semaphore *s);
void SDL_WaitSemaphore(SDL_Semaphore *s);
bool SDL_TryWaitSemaphore(SDL_Semaphore *s);
bool SDL_WaitSemaphoreTimeout(SDL_Semaphore *s, Sint32 ms);
/* once-only start-up (curl_ws_io.h's curl_global_init) */
typedef struct SDL_InitState { SDL_AtomicInt status; SDL_ThreadID thread; void *reserved; } SDL_InitState;
bool SDL_ShouldInit(SDL_InitState *state);
void SDL_SetInitialized(SDL_InitState *state, bool initialized);
/* files and folders: the C library's (ux0: paths). The preference folder is ux0:data/bt4/, the base folder (where
   lib/cacert.pem is looked for) app0: or, with bt3.txt online_ca, the folder that holds the CA list made with it */
typedef struct SDL_IOStream SDL_IOStream;
SDL_IOStream *SDL_IOFromFile(const char *file, const char *mode);
size_t SDL_ReadIO(SDL_IOStream *io, void *ptr, size_t size);
size_t SDL_WriteIO(SDL_IOStream *io, const void *ptr, size_t size);
Sint64 SDL_GetIOSize(SDL_IOStream *io);
bool SDL_CloseIO(SDL_IOStream *io);
typedef enum SDL_IOStatus { SDL_IO_STATUS_READY, SDL_IO_STATUS_ERROR, SDL_IO_STATUS_EOF, SDL_IO_STATUS_NOT_READY, SDL_IO_STATUS_READONLY, SDL_IO_STATUS_WRITEONLY } SDL_IOStatus;
SDL_IOStatus SDL_GetIOStatus(SDL_IOStream *io);
char *SDL_GetPrefPath(const char *org, const char *app);
const char *SDL_GetBasePath(void);
bool SDL_CreateDirectory(const char *path);
/* conditions (Tag Team's transports, gs/tag_team_ws.h) */
typedef struct SDL_Condition SDL_Condition;
SDL_Condition *SDL_CreateCondition(void);
void SDL_DestroyCondition(SDL_Condition *c);
void SDL_SignalCondition(SDL_Condition *c);
void SDL_BroadcastCondition(SDL_Condition *c);
bool SDL_WaitConditionTimeout(SDL_Condition *c, SDL_Mutex *m, Sint32 ms);
/* a value per thread (gs/net_match.c's search number) */
typedef SDL_AtomicInt SDL_TLSID;
typedef void (SDLCALL *SDL_TLSDestructorCallback)(void *value);
void *SDL_GetTLS(SDL_TLSID *id);
bool SDL_SetTLS(SDL_TLSID *id, const void *value, SDL_TLSDestructorCallback destructor);
/* the C library's string and memory calls under SDL's names */
#define SDL_atoi atoi
#define SDL_memcmp memcmp
#define SDL_memcpy memcpy
#define SDL_memset memset
#define SDL_strchr strchr
#define SDL_strcmp strcmp
#define SDL_strlen strlen
#define SDL_strncmp strncmp
#define SDL_strstr strstr
#define SDL_strtoll strtoll
static inline size_t SDL_strlcat(char *dst, const char *src, size_t size) {
    size_t n = strlen(dst);
    return n < size ? n + SDL_strlcpy(dst + n, src, size - n) : n + strlen(src);
}
/* events: only the type's name (gs/voice.h's declarations; the Vita has no SDL events) */
typedef union SDL_Event { Uint32 type; Uint8 padding[128]; } SDL_Event;
/* processes: none on the Vita (gs/net_match.c's old Worker path runs the curl program: left out, it finds none) */
typedef struct SDL_Process SDL_Process;
static inline SDL_Process *SDL_CreateProcess(const char *const *args, bool pipe_stdio) { (void)args; (void)pipe_stdio; return NULL; }
static inline void *SDL_ReadProcess(SDL_Process *p, size_t *datasize, int *exitcode) {
    (void)p;
    if (datasize) *datasize = 0;
    if (exitcode) *exitcode = -1;
    return NULL;
}
static inline void SDL_DestroyProcess(SDL_Process *p) { (void)p; }
/* shared libraries: none on the Vita (the PC's run-time libcurl / libdatachannel paths are left out) */
typedef struct SDL_SharedObject SDL_SharedObject;
typedef void (*SDL_FunctionPointer)(void);
static inline SDL_SharedObject *SDL_LoadObject(const char *file) { (void)file; return NULL; }
static inline SDL_FunctionPointer SDL_LoadFunction(SDL_SharedObject *h, const char *name) { (void)h; (void)name; return NULL; }
static inline void SDL_UnloadObject(SDL_SharedObject *h) { (void)h; }
#ifdef __cplusplus
}
#endif
#endif
