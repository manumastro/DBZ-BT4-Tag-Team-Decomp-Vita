/*
 * PS Vita: the SDL3 calls of BT4decomp's network files (port/vita/sdlshim/SDL3/SDL.h, the part after "network
 * files"), on the Vita's kernel and C library. Built only with the online part (build.py, BT4_VITA_ONLINE).
 *
 * Threads are kernel threads with a 256 KB stack (a TLS handshake in OpenSSL is deep), on the second and third cores
 * (the first is the game's), at a priority below the game's, the renderer's and the log's. Locks are recursive, as
 * SDL's. Nothing here touches the game's state or its random generators.
 */
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#ifndef PORT_VITA_HOME
#define PORT_VITA_HOME "ux0:data/bt4"
#endif

/* ---- random numbers: xorshift128+, seeded from the clock on first use ---- */
static Uint64 sRand[2];

void SDL_srand(Uint64 seed) {
    if (seed == 0) {
        seed = sceKernelGetProcessTimeWide() ^ ((Uint64)(uintptr_t)&seed << 17);
    }
    sRand[0] = seed ^ 0x9E3779B97F4A7C15ull;
    sRand[1] = (seed << 21) ^ 0xBF58476D1CE4E5B9ull ^ (seed >> 7);
    if ((sRand[0] | sRand[1]) == 0) {
        sRand[1] = 1;
    }
}

Uint32 SDL_rand_bits(void) {
    Uint64 a, b;
    if ((sRand[0] | sRand[1]) == 0) {
        SDL_srand(0);
    }
    a = sRand[0];
    b = sRand[1];
    sRand[0] = b;
    a ^= a << 23;
    sRand[1] = a ^ b ^ (a >> 17) ^ (b >> 26);
    return (Uint32)((sRand[1] + b) >> 32);
}

Sint32 SDL_rand(Sint32 n) {
    if (n <= 0) {
        return 0;
    }
    return (Sint32)(((Uint64)SDL_rand_bits() * (Uint64)n) >> 32);
}

/* ---- threads ---- */
struct SDL_Thread {
    SceUID uid;
    SDL_ThreadFunction fn;
    void *data;
    int status;
    int state; /* 0 running, 1 detached, 2 finished */
};

static int thread_entry(SceSize args, void *argp) {
    SDL_Thread *t = *(SDL_Thread **)argp;
    int expected = 0;
    (void)args;
    t->status = t->fn(t->data);
    if (!__atomic_compare_exchange_n(&t->state, &expected, 2, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        free(t); /* detached: no one waits for it */
        sceKernelExitDeleteThread(0);
    }
    return 0;
}

SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data) {
    SDL_Thread *t = calloc(1, sizeof(*t));
    char kname[32];
    if (t == NULL) {
        return NULL;
    }
    t->fn = fn;
    t->data = data;
    snprintf(kname, sizeof(kname), "%s", name != NULL ? name : "sdl thread");
    /* priority 0x10000100 + 20 relative: below the game (0x10000100) and the renderer; cores 2 and 3 (masks 0x20000, 0x40000) */
    t->uid = sceKernelCreateThread(kname, thread_entry, 0x10000100 + 20, 256 * 1024, 0, 0x00020000 | 0x00040000, NULL);
    if (t->uid < 0) {
        free(t);
        return NULL;
    }
    if (sceKernelStartThread(t->uid, sizeof(t), &t) < 0) {
        sceKernelDeleteThread(t->uid);
        free(t);
        return NULL;
    }
    return t;
}

void SDL_WaitThread(SDL_Thread *t, int *status) {
    if (t == NULL) {
        return;
    }
    sceKernelWaitThreadEnd(t->uid, NULL, NULL);
    sceKernelDeleteThread(t->uid);
    if (status != NULL) {
        *status = t->status;
    }
    free(t);
}

void SDL_DetachThread(SDL_Thread *t) {
    int expected = 0;
    if (t == NULL) {
        return;
    }
    if (!__atomic_compare_exchange_n(&t->state, &expected, 1, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        /* it has finished already: let it go here */
        sceKernelWaitThreadEnd(t->uid, NULL, NULL);
        sceKernelDeleteThread(t->uid);
        free(t);
    }
}

SDL_ThreadID SDL_GetCurrentThreadID(void) { return (SDL_ThreadID)(unsigned)sceKernelGetThreadId(); }

/* ---- locks ---- */
struct SDL_Mutex {
    pthread_mutex_t m;
};

SDL_Mutex *SDL_CreateMutex(void) {
    SDL_Mutex *m = malloc(sizeof(*m));
    pthread_mutexattr_t a;
    if (m == NULL) {
        return NULL;
    }
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    if (pthread_mutex_init(&m->m, &a) != 0) {
        free(m);
        m = NULL;
    }
    pthread_mutexattr_destroy(&a);
    return m;
}

void SDL_LockMutex(SDL_Mutex *m) {
    if (m != NULL) {
        pthread_mutex_lock(&m->m);
    }
}

bool SDL_TryLockMutex(SDL_Mutex *m) { return m == NULL || pthread_mutex_trylock(&m->m) == 0; }

void SDL_UnlockMutex(SDL_Mutex *m) {
    if (m != NULL) {
        pthread_mutex_unlock(&m->m);
    }
}

void SDL_DestroyMutex(SDL_Mutex *m) {
    if (m != NULL) {
        pthread_mutex_destroy(&m->m);
        free(m);
    }
}

/* ---- semaphores ---- */
struct SDL_Semaphore {
    SceUID uid;
};

SDL_Semaphore *SDL_CreateSemaphore(Uint32 initial) {
    SDL_Semaphore *s = malloc(sizeof(*s));
    if (s == NULL) {
        return NULL;
    }
    s->uid = sceKernelCreateSema("sdl sema", 0, (int)initial, 0x7FFFFFFF, NULL);
    if (s->uid < 0) {
        free(s);
        return NULL;
    }
    return s;
}

void SDL_DestroySemaphore(SDL_Semaphore *s) {
    if (s != NULL) {
        sceKernelDeleteSema(s->uid);
        free(s);
    }
}

void SDL_SignalSemaphore(SDL_Semaphore *s) {
    if (s != NULL) {
        sceKernelSignalSema(s->uid, 1);
    }
}

bool SDL_WaitSemaphoreTimeout(SDL_Semaphore *s, Sint32 ms) {
    SceUInt us;
    if (s == NULL) {
        return false;
    }
    if (ms < 0) {
        return sceKernelWaitSema(s->uid, 1, NULL) >= 0;
    }
    if (ms == 0) {
        return sceKernelPollSema(s->uid, 1) >= 0;
    }
    us = (SceUInt)ms * 1000u;
    return sceKernelWaitSema(s->uid, 1, &us) >= 0;
}

void SDL_WaitSemaphore(SDL_Semaphore *s) { SDL_WaitSemaphoreTimeout(s, -1); }

bool SDL_TryWaitSemaphore(SDL_Semaphore *s) { return SDL_WaitSemaphoreTimeout(s, 0); }

/* ---- conditions (pthread's, with SDL_Mutex's lock) ---- */
#include <sys/time.h>
struct SDL_Condition {
    pthread_cond_t c;
};

SDL_Condition *SDL_CreateCondition(void) {
    SDL_Condition *c = malloc(sizeof(*c));
    if (c != NULL && pthread_cond_init(&c->c, NULL) != 0) {
        free(c);
        c = NULL;
    }
    return c;
}

void SDL_DestroyCondition(SDL_Condition *c) {
    if (c != NULL) {
        pthread_cond_destroy(&c->c);
        free(c);
    }
}

void SDL_SignalCondition(SDL_Condition *c) {
    if (c != NULL) {
        pthread_cond_signal(&c->c);
    }
}

void SDL_BroadcastCondition(SDL_Condition *c) {
    if (c != NULL) {
        pthread_cond_broadcast(&c->c);
    }
}

bool SDL_WaitConditionTimeout(SDL_Condition *c, SDL_Mutex *m, Sint32 ms) {
    struct timeval now;
    struct timespec until;
    if (c == NULL || m == NULL) {
        return false;
    }
    if (ms < 0) {
        return pthread_cond_wait(&c->c, &m->m) == 0;
    }
    gettimeofday(&now, NULL);
    until.tv_sec = now.tv_sec + ms / 1000;
    until.tv_nsec = (long)now.tv_usec * 1000 + (long)(ms % 1000) * 1000000;
    if (until.tv_nsec >= 1000000000) {
        until.tv_sec++;
        until.tv_nsec -= 1000000000;
    }
    return pthread_cond_timedwait(&c->c, &m->m, &until) == 0;
}

/* ---- a value per thread: a small table by kernel thread ID (the network's threads are kernel threads, not pthreads)
   with eight IDs; a thread's row is taken on its first SetTLS and never given back (a handful of threads a run) ---- */
#define TLS_THREADS 64
#define TLS_IDS 8
static struct {
    int thread;
    void *v[TLS_IDS];
} sTls[TLS_THREADS];
static SDL_SpinLock sTlsLock;
static SDL_AtomicInt sTlsNext;

static int tls_slot(SDL_TLSID *id, int make) {
    int k = SDL_GetAtomicInt(id);
    if (k == 0 && make) {
        int n = SDL_AddAtomicInt(&sTlsNext, 1) + 1;
        if (n > TLS_IDS || !SDL_CompareAndSwapAtomicInt(id, 0, n)) {
            k = SDL_GetAtomicInt(id); /* (another thread gave it one first, or none left) */
        } else {
            k = n;
        }
    }
    return k >= 1 && k <= TLS_IDS ? k - 1 : -1;
}

void *SDL_GetTLS(SDL_TLSID *id) {
    int k = tls_slot(id, 0), me = sceKernelGetThreadId(), i;
    void *v = NULL;
    if (k < 0) {
        return NULL;
    }
    SDL_LockSpinlock(&sTlsLock);
    for (i = 0; i < TLS_THREADS; i++) {
        if (sTls[i].thread == me) {
            v = sTls[i].v[k];
            break;
        }
    }
    SDL_UnlockSpinlock(&sTlsLock);
    return v;
}

bool SDL_SetTLS(SDL_TLSID *id, const void *value, SDL_TLSDestructorCallback destructor) {
    int k = tls_slot(id, 1), me = sceKernelGetThreadId(), i, free_row = -1;
    bool ok = false;
    (void)destructor;
    if (k < 0) {
        return false;
    }
    SDL_LockSpinlock(&sTlsLock);
    for (i = 0; i < TLS_THREADS; i++) {
        if (sTls[i].thread == me) {
            break;
        }
        if (sTls[i].thread == 0 && free_row < 0) {
            free_row = i;
        }
    }
    if (i == TLS_THREADS && free_row >= 0) {
        i = free_row;
        sTls[i].thread = me;
    }
    if (i < TLS_THREADS) {
        sTls[i].v[k] = (void *)value;
        ok = true;
    }
    SDL_UnlockSpinlock(&sTlsLock);
    return ok;
}

/* ---- once-only start-up: 0 not done, 1 being done (by state->thread), 2 done ---- */
bool SDL_ShouldInit(SDL_InitState *state) {
    for (;;) {
        if (SDL_CompareAndSwapAtomicInt(&state->status, 0, 1)) {
            state->thread = SDL_GetCurrentThreadID();
            return true;
        }
        if (SDL_GetAtomicInt(&state->status) == 2) {
            return false;
        }
        if (state->thread == SDL_GetCurrentThreadID()) {
            return false; /* (asked again by the thread doing it) */
        }
        sceKernelDelayThread(1000);
    }
}

void SDL_SetInitialized(SDL_InitState *state, bool initialized) {
    state->thread = 0;
    SDL_SetAtomicInt(&state->status, initialized ? 2 : 0);
}

/* ---- files and folders ---- */
struct SDL_IOStream {
    FILE *f;
};

SDL_IOStream *SDL_IOFromFile(const char *file, const char *mode) {
    SDL_IOStream *io;
    FILE *f = fopen(file, mode);
    if (f == NULL) {
        return NULL;
    }
    io = malloc(sizeof(*io));
    if (io == NULL) {
        fclose(f);
        return NULL;
    }
    io->f = f;
    return io;
}

size_t SDL_ReadIO(SDL_IOStream *io, void *ptr, size_t size) { return io != NULL ? fread(ptr, 1, size, io->f) : 0; }

size_t SDL_WriteIO(SDL_IOStream *io, const void *ptr, size_t size) { return io != NULL ? fwrite(ptr, 1, size, io->f) : 0; }

SDL_IOStatus SDL_GetIOStatus(SDL_IOStream *io) {
    return io == NULL || ferror(io->f) ? SDL_IO_STATUS_ERROR : feof(io->f) ? SDL_IO_STATUS_EOF : SDL_IO_STATUS_READY;
}

Sint64 SDL_GetIOSize(SDL_IOStream *io) {
    long at, end;
    if (io == NULL) {
        return -1;
    }
    at = ftell(io->f);
    if (fseek(io->f, 0, SEEK_END) != 0) {
        return -1;
    }
    end = ftell(io->f);
    fseek(io->f, at, SEEK_SET);
    return (Sint64)end;
}

bool SDL_CloseIO(SDL_IOStream *io) {
    int r;
    if (io == NULL) {
        return false;
    }
    r = fclose(io->f);
    free(io);
    return r == 0;
}

/* gs/supabase_signal.cpp locks its identity file against a second copy of the game (newlib declares no flock on the
   Vita): one program runs at a time here, so the lock is always had */
int flock(int fd, int operation) {
    (void)fd;
    (void)operation;
    return 0;
}

/* ux0:data/bt4/ for every program and organization name: the Vita has one game and one data folder */
char *SDL_GetPrefPath(const char *org, const char *app) {
    (void)org;
    (void)app;
    return strdup(PORT_VITA_HOME "/");
}

/* (port/vita/vita_net.c sets it: "app0:" or the folder of the CA list made with bt3.txt online_ca) */
char gVitaSdlBase[160] = "app0:";

const char *SDL_GetBasePath(void) { return gVitaSdlBase; }

bool SDL_CreateDirectory(const char *path) {
    char p[512];
    size_t i, n = strlen(path);
    SceIoStat st;
    if (n == 0 || n >= sizeof(p)) {
        return false;
    }
    memcpy(p, path, n + 1);
    while (n > 0 && p[n - 1] == '/') {
        p[--n] = '\0';
    }
    for (i = 0; i <= n; i++) { /* each parent in turn ("ux0:" itself is left alone) */
        if (p[i] == '/' || p[i] == '\0') {
            char c = p[i];
            p[i] = '\0';
            if (i > 0 && p[i - 1] != ':' && sceIoGetstat(p, &st) < 0) {
                sceIoMkdir(p, 0777);
            }
            p[i] = c;
        }
    }
    return sceIoGetstat(p, &st) >= 0 && SCE_S_ISDIR(st.st_mode);
}
