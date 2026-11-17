/*
 * The film behind BT4decomp's Tag Team screens on the PS Vita (bt3.txt tbomovie; default 0: the poster, vita_tbo.c).
 *
 * On PC the film is tbo/ui/tag_ui_intro.pss, then tag_ui_loop.pss (port/src/gs/tag_menu_movie.c, decoded in software).
 * On the Vita the same two films are H.264 files, port/vita/assets/tag_ui_intro.mp4 and tag_ui_loop.mp4 (960 x 544, made
 * from upstream's .pss by port/vita/tools/make_tagmovie.sh), packaged as tbo/ui/tag_ui_intro.mp4 and tag_ui_loop.mp4, and
 * decoded by the console's hardware decoder through SceAvPlayer. The software MPEG-2 path of an earlier commit (724dda0)
 * is not here: 180 ms a picture on one core, 4 pictures a second.
 *
 *   tbomovie=0  the poster (vita_tbo.c, TBO_TAG_ART), as before; any value other than 1 is the poster too
 *   tbomovie=1  the film: the intro once, then the loop repeated (the loop's source is set to loop, so the player
 *               keeps its own time)
 *
 * Frames: the player's allocator hook (allocateTexture) gives it physically contiguous memory (the hardware decoder writes
 * its pictures there), mapped for the GPU (sceGxmMapMemory), as vitaGL's samples/video_playback does. The drawing thread
 * points the film's texture at the newest frame (Vita_VglTexYuv: a linear YVU 4:2:0 texture on the frame itself,
 * SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC0) and the GPU converts the colour when it samples: no copy, no conversion on the CPU.
 * vitaGL takes all the free physically contiguous memory at its start (26 MB, all the console has); with tbomovie=1 and tbomoviemem=0
 * the drawing thread gives that pool back to the system when the film starts (the port's vitaGL patch, vglPhycontMemRelease: only
 * when nothing is allocated in it) and takes it again 1 s after the player closed, so fights keep it; tbomoviemem=N leaves N MB of it
 * unused from vitaGL's start instead (vita_vgl.c). The frames' memory is given back TM_FREE_DELAY_US after the player
 * lets it go (the chip may still be reading the last pictures), and the frame the drawing thread holds is dropped then.
 *
 * When it runs: Port_TagMenuMovieBegin (port_tbo.c, the Tag Team screens opened) restarts the film. The drawing thread
 * starts the playing thread when the screens ask for the film's picture; the thread stops, its player closed, 0.5 s
 * after the last picture asked for. The texture is given back when the film is not playing, or 600 pictures after it was
 * last drawn (vita_tbo.c KEEP_FRAMES). A film that cannot be played is retried after 2 s; the poster is drawn meanwhile.
 *
 * Threads: the playing thread (it polls the player) on the fourth core when CapUnlocker's plugin is in (vita_main.c
 * Vita_ThreadOnCore3), else the second core. The player's own threads are its business (basePriority). vitaGL's calls are
 * made on the drawing thread only.
 *
 * The log, every 2 s: "tagmovie:" with the frames the player gave, the frames shown (the texture pointed at them), the
 * frames replaced before they were shown, the frames late (more than 85 ms after their time), the time to point the texture
 * (average, worst), the memory held (the player's frames and its other blocks, from the allocator hooks), the film's source
 * (intro or loop), and the core.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <psp2/avplayer.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/gxm.h>
#include <vitaGL.h>

extern void Vita_Log(const char *fmt, ...);
extern unsigned gGsFrame;
extern unsigned Vita_VglPhycontRelease(void);
extern unsigned Vita_VglPhycontReacquire(unsigned size);
extern void Vita_VglPhycontInfo(unsigned *total, unsigned *freeb);
extern unsigned Vita_VglTexYuv(unsigned t, const void *planes, int w, int h);
extern void Vita_VglTexFree(unsigned t);
extern SceUID Vita_ThreadOnCore3(const char *name, SceKernelThreadEntry entry, int prio, int stack, int fallbackMask);
extern int gVitaCore3;

int gVitaTagMovie = 0; /* bt3.txt tbomovie: 0 the poster (default), 1 the film; other values the poster */
int gVitaTagMovieCopy = 1; /* bt3.txt tbomoviecopy: 1 each picture copied into the film's own 1 MB-aligned buffers (default), 0 the player's frame used in place */
int gVitaTagMovieCsc = 0; /* bt3.txt tbomoviecsc: 0 CSC0, 1 CSC1 (vita_vgl.c) */
int gVitaTagMovieReserve = 6; /* bt3.txt tbomoviereserve: MB of physically contiguous memory the film always leaves free in the system (batch 33: the console went off with 22 of 26 MB taken by the film); an allocation that would leave less is refused (the poster, or no copy) */
int gVitaTagMovieStage = 0; /* bt3.txt tbomoviestage: 1 only gives vitaGL's pool back and takes it again (no player), once, to test that on its own */
int gVitaTagMovieMem = 0; /* bt3.txt tbomoviemem: MB of physically contiguous memory vitaGL leaves to the player at its start (vita_vgl.c); 0 (default):
                             none left, the player's memory is carved from a block of vitaGL's own physically contiguous pool while the film plays */

#define TM_OUT_FRAMES 5      /* the player's output frame buffers (vitaGL's samples/video_playback: 5; the GPU reads one for a few pictures after it was given) */
#define TM_FREE_DELAY_US 150000u /* frames given back this long after the player lets them go */
#define TM_PHYCONT_ALIGN (1024 * 1024) /* physically contiguous blocks: 1 MB sizes */
#define TM_IDLE_US 3000000u  /* the thread stops this long after the last picture asked for (batch 31: 0.5 s made the player 26 times in 70 s, 0.65 s each time to open) */
#define TM_KEEP_FRAMES 600   /* the texture given back this many pictures after it was last drawn (vita_tbo.c KEEP_FRAMES) */
#define TM_RETRY_US 2000000u /* a film that could not be played is not tried again for this long */
#define TM_NOFRAME_US 3000000u /* the intro gives no picture for this long: the film is given up (the poster) */
#define TM_LATE_US 85000u    /* a picture more than this late is counted as late */
#define TM_POLL_US 2000u     /* the player polled this often when it has no picture */
#define TM_LOG_US 2000000u
#define TM_DIAG_US 250000u   /* the first TM_NOFRAME_US of a source: the player's state logged this often */
/* the player's handle is an address (0x8xxxxxxx: negative as an int), its errors 0x80xxxxxx codes: not a sign test */
#define TM_NONE ((SceAvPlayerHandle)0)
#define TM_OK(h) ((h) != 0 && ((unsigned)(h) & 0xFF000000u) != 0x80000000u)
#define TM_PRIORITY 0xA0 /* the player's own threads (a plain priority, as vitaGL's samples/video_playback: no core bits) */
#define TM_STACK 0x10000
#define TM_INTRO "app0:tbo/ui/tag_ui_intro.mp4"
#define TM_LOOP "app0:tbo/ui/tag_ui_loop.mp4"
#define TM_COPIES 3 /* the copies' ring: the GPU reads one for a few pictures after it was shown */
#define TM_POOL_BACK_US 1000000u /* vitaGL's pool taken again this long after the player closed (the chip is done with its memory) */
#define TM_TRACK 64 /* blocks whose sizes are kept for the memory count (allocator hooks give no size on free) */

static SceUID sLock = -1;
static struct {
    /* the newest frame the player gave (its planes; valid until the player reuses the buffer), under sLock */
    const uint8_t *frame;
    uint32_t w, h;
    uint32_t serial;       /* frames given by the player */
    uint32_t uploaded;     /* the serial of the frame in the texture (drawing thread) */
    int running, stopping; /* the playing thread: running, or closing its player */
    uint64_t touched;      /* us: the last picture asked for */
    uint64_t failedAt;     /* us: the last film that could not be played (0: none) */
    unsigned tex;          /* the texture (drawing thread) */
    unsigned texFrame;     /* gGsFrame when it was last drawn */
    int restart;           /* Port_TagMenuMovieBegin: the film from its start */
    int inLoop;            /* the playing thread: the loop's source is playing (for the log) */
    /* the counters of the log (since the last line) */
    unsigned decoded, late, shown, replaced, uploads;
    uint64_t uploadUs, uploadMax;
    /* memory: the allocator hooks' blocks (the player's frames and its other blocks) */
    struct { void *p; size_t n; int frame; } blk[TM_TRACK];
    size_t liveFrame, liveOther, peak;
    int core;
    int allocFailed;       /* a frame's memory the player asked for could not be given (it will give no picture) */
    uint64_t freedAt;      /* us: the last wait before frames were given back */
} sM;

/* the copies (tbomoviecopy=1): 1 MB-aligned physically contiguous blocks mapped for the GPU, allocated by the playing thread when the
   first picture comes, given back at its end */
/* tbomoviemem=0: vitaGL's physically contiguous pool is given back to the system by the drawing thread (Vita_VglPhycontRelease, only when
   nothing is allocated in it) before the player starts, and taken again by it (Vita_TagMovieIdle) TM_POOL_BACK_US after the player closed:
   the player's blocks are the plain sceKernelAllocMemBlock ones. sPoolSize is the size to take again (0: the pool is not given back). */
static unsigned sPoolSize;
static uint64_t sPoolEndAt; /* us: the playing thread's end */
static struct { SceUID blk; void *p; uint32_t size; } sCopy[TM_COPIES];
static int sCopyAt;
static uint64_t sCopyUs, sCopyMax;
static unsigned sCopyN;
static int sCopyRing = TM_COPIES; /* the copies in use (a copy buffer refused for want of memory shortens it) */
static int sCopyOff;              /* no copy buffer could be made: the frames are used in place */
static int sNever;                /* the pool could not be given back (or the system did not show its memory): no film this session */
static int sStageDone;
static int sFails;

static uint64_t tm_now(void) { return sceKernelGetProcessTimeWide(); }
static void tm_lock(void) { sceKernelLockMutex(sLock, 1, NULL); }
static void tm_unlock(void) { sceKernelUnlockMutex(sLock, 1); }

/* The system's free physically contiguous memory in bytes (-1 if it cannot be asked), and the line of the log that says it. */
static int tm_phy_free(void) {
    SceKernelFreeMemorySizeInfo info;
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    if (sceKernelGetFreeMemorySize(&info) < 0) {
        return -1;
    }
    return info.size_phycont;
}
static void tm_phy_log(const char *what) {
    int f = tm_phy_free();
    Vita_Log("tagmovie: physically contiguous memory free in the system: %d KB (%s)\n", f < 0 ? -1 : f / 1024, what);
}
/* May the film take `size` bytes of it: the system keeps tbomoviereserve MB free after. */
static int tm_phy_ok(uint32_t size, const char *what) {
    int f = tm_phy_free();
    unsigned keep = (unsigned)(gVitaTagMovieReserve < 0 ? 0 : gVitaTagMovieReserve) << 20;
    if (f < 0 || (unsigned)f >= size + keep) {
        return 1;
    }
    Vita_Log("tagmovie: %s of %u KB refused: %d KB of physically contiguous memory free, %u KB to be kept free (tbomoviereserve)\n", what,
             size / 1024, f / 1024, keep / 1024);
    return 0;
}

/* the allocator hooks (under sLock; the player calls them on its own threads): the block's size kept for the count */
static void tm_track(void *p, size_t n, int frame, int add) {
    int i;
    if (add) {
        for (i = 0; i < TM_TRACK; i++) {
            if (sM.blk[i].p == NULL) {
                sM.blk[i].p = p;
                sM.blk[i].n = n;
                sM.blk[i].frame = frame;
                break;
            }
        }
        if (frame) sM.liveFrame += n; else sM.liveOther += n;
        if (sM.liveFrame + sM.liveOther > sM.peak) sM.peak = sM.liveFrame + sM.liveOther;
        return;
    }
    for (i = 0; i < TM_TRACK; i++) {
        if (sM.blk[i].p == p) {
            if (sM.blk[i].frame) sM.liveFrame -= sM.blk[i].n; else sM.liveOther -= sM.blk[i].n;
            sM.blk[i].p = NULL;
            return;
        }
    }
}
static void *tm_alloc(void *arg, uint32_t alignment, uint32_t size) {
    void *p;
    (void)arg;
    p = memalign(alignment < 16 ? 16 : alignment, size);
    if (p == NULL) {
        Vita_Log("tagmovie: allocate(align %u, %u KB): out of memory\n", alignment, size / 1024);
    }
    if (p != NULL) {
        tm_lock();
        tm_track(p, size, 0, 1);
        tm_unlock();
    }
    return p;
}
static void tm_free(void *arg, void *p) {
    (void)arg;
    if (p == NULL) return;
    tm_lock();
    tm_track(p, 0, 0, 0);
    tm_unlock();
    free(p);
}
/* the frames: physically contiguous memory mapped for the GPU (as vitaGL's samples/video_playback) */
static void *tm_alloc_frame(void *arg, uint32_t alignment, uint32_t size) {
    SceUID blk;
    void *p = NULL;
    unsigned asked = size;
    int r;
    (void)arg;
    size = (size + TM_PHYCONT_ALIGN - 1) & ~(uint32_t)(TM_PHYCONT_ALIGN - 1);
    if (!tm_phy_ok(size, "allocateTexture")) {
        tm_lock();
        sM.allocFailed = 1;
        tm_unlock();
        return NULL;
    }
    blk = sceKernelAllocMemBlock("bt3_tagframe", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, size, NULL);
    if (blk < 0) {
        Vita_Log("tagmovie: allocateTexture(align %u, size %u KB -> %u KB): no physically contiguous memory (0x%08X)\n",
                 alignment, asked / 1024, size / 1024, (unsigned)blk);
        tm_lock();
        sM.allocFailed = 1;
        tm_unlock();
        return NULL;
    }
    r = sceKernelGetMemBlockBase(blk, &p);
    if (r < 0 || p == NULL) {
        Vita_Log("tagmovie: allocateTexture(%u KB): the block's base 0x%08X\n", size / 1024, (unsigned)r);
        sceKernelFreeMemBlock(blk);
        tm_lock();
        sM.allocFailed = 1;
        tm_unlock();
        return NULL;
    }
    r = sceGxmMapMemory(p, size, SCE_GXM_MEMORY_ATTRIB_RW);
    if (r < 0) {
        Vita_Log("tagmovie: allocateTexture(%u KB): sceGxmMapMemory 0x%08X\n", size / 1024, (unsigned)r);
        sceKernelFreeMemBlock(blk);
        tm_lock();
        sM.allocFailed = 1;
        tm_unlock();
        return NULL;
    }
    tm_lock();
    tm_track(p, size, 1, 1);
    tm_unlock();
    Vita_Log("tagmovie: allocateTexture(align %u, size %u KB -> %u KB) = %p\n", alignment, asked / 1024, size / 1024, p);
    tm_phy_log("after it");
    return p;
}
static void tm_free_frame(void *arg, void *p) {
    SceUID blk;
    size_t n = 0;
    int i, wait;
    uint64_t now;
    (void)arg;
    if (p == NULL) return;
    /* the drawing thread must not use it any more, and the chip may still be reading it: dropped first, given back later */
    tm_lock();
    for (i = 0; i < TM_TRACK; i++) {
        if (sM.blk[i].p == p) {
            n = sM.blk[i].n;
        }
    }
    if (sM.frame != NULL && sM.frame >= (const uint8_t *)p && sM.frame < (const uint8_t *)p + n) {
        sM.frame = NULL;
    }
    tm_track(p, 0, 1, 0);
    now = tm_now();
    wait = now - sM.freedAt > TM_FREE_DELAY_US; /* (one wait for the frames let go together) */
    if (wait) {
        sM.freedAt = now;
    }
    tm_unlock();
    if (wait) {
        sceKernelDelayThread(TM_FREE_DELAY_US);
    }
    blk = sceKernelFindMemBlockByAddr(p, 0);
    sceGxmUnmapMemory(p);
    if (blk >= 0) sceKernelFreeMemBlock(blk);
    Vita_Log("tagmovie: deallocateTexture(%p, %u KB)\n", p, (unsigned)(n / 1024));
}

/* A player with the film's source set, not started (the player starts it when the source is in: autoStart).
   -1 on error. */
static SceAvPlayerHandle tm_init(void) {
    SceAvPlayerInitData init;
    memset(&init, 0, sizeof(init));
    init.memoryReplacement.allocate = tm_alloc;
    init.memoryReplacement.deallocate = tm_free;
    init.memoryReplacement.allocateTexture = tm_alloc_frame;
    init.memoryReplacement.deallocateTexture = tm_free_frame;
    init.basePriority = TM_PRIORITY;
    init.numOutputVideoFrameBuffers = TM_OUT_FRAMES;
    init.autoStart = 1;
    {
        SceAvPlayerHandle h = sceAvPlayerInit(&init);
        Vita_Log("tagmovie: sceAvPlayerInit(priority 0x%X, %d output frames, autoStart) = 0x%08X\n", (unsigned)init.basePriority,
                 init.numOutputVideoFrameBuffers, (unsigned)h);
        return h;
    }
}

/* The file to play: the player's source set, in the order of vitaGL's samples/video_playback (Init, AddSource, then
   SetLooping; a player with autoStart starts when the source is in). A player that has played is stopped first, and made
   again when it refuses the new source. Every call's result is logged. 0 on success. */
static int tm_play(SceAvPlayerHandle *h, const char *file, int looping) {
    int r;
    if (TM_OK(*h)) {
        r = sceAvPlayerStop(*h);
        Vita_Log("tagmovie: sceAvPlayerStop = 0x%08X\n", (unsigned)r);
        r = sceAvPlayerAddSource(*h, file);
        Vita_Log("tagmovie: sceAvPlayerAddSource(%s) = 0x%08X (a player that played)\n", file, (unsigned)r);
        if (r >= 0) {
            r = sceAvPlayerSetLooping(*h, looping);
            Vita_Log("tagmovie: sceAvPlayerSetLooping(%d) = 0x%08X\n", looping, (unsigned)r);
            return 0;
        }
        sceAvPlayerClose(*h);
        *h = TM_NONE;
    }
    *h = tm_init();
    if (!TM_OK(*h)) {
        return -1;
    }
    r = sceAvPlayerAddSource(*h, file);
    Vita_Log("tagmovie: sceAvPlayerAddSource(%s) = 0x%08X\n", file, (unsigned)r);
    if (r < 0) {
        return -1;
    }
    r = sceAvPlayerSetLooping(*h, looping);
    Vita_Log("tagmovie: sceAvPlayerSetLooping(%d) = 0x%08X\n", looping, (unsigned)r);
    return 0;
}

/* The picture copied into the next buffer of the ring (the planes of a 4:2:0 two-plane frame: w x h luma, then w x h / 2 of V,U pairs).
   NULL when it cannot (the player's frame is used in place then). */
static const uint8_t *tm_copy(const uint8_t *src, uint32_t w, uint32_t h) {
    uint32_t size = (w * h * 3 / 2 + TM_PHYCONT_ALIGN - 1) & ~(uint32_t)(TM_PHYCONT_ALIGN - 1);
    uint64_t t0 = tm_now(), dt;
    int k = sCopyAt;
    if (sCopyOff) {
        return NULL;
    }
    if (sCopy[k].p == NULL || sCopy[k].size < size) {
        void *p = NULL;
        if (sCopy[k].p != NULL) {
            return NULL;
        }
        if (!tm_phy_ok(size, "copy buffer")) {
            if (k >= 2) { /* two buffers at least make a ring */
                sCopyRing = k;
                sCopyAt = k = 0;
                Vita_Log("tagmovie: the copies' ring is %d buffers (the memory kept free)\n", sCopyRing);
            } else {
                sCopyOff = 1;
                Vita_Log("tagmovie: no copies (the memory kept free): the player's frames used in place\n");
                return NULL;
            }
        } else {
            sCopy[k].blk = sceKernelAllocMemBlock("bt3_tagcopy", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, size, NULL);
            if (sCopy[k].blk < 0 || sceKernelGetMemBlockBase(sCopy[k].blk, &p) < 0 || p == NULL ||
                sceGxmMapMemory(p, size, SCE_GXM_MEMORY_ATTRIB_RW) < 0) {
                Vita_Log("tagmovie: no memory for a copy buffer of %u KB (0x%08X): the player's frames used in place\n", size / 1024,
                         (unsigned)sCopy[k].blk);
                if (sCopy[k].blk >= 0) {
                    sceKernelFreeMemBlock(sCopy[k].blk);
                }
                sCopy[k].blk = 0;
                sCopyOff = 1;
                return NULL;
            }
            sCopy[k].p = p;
            sCopy[k].size = size;
            Vita_Log("tagmovie: copy buffer %d: %u KB at %p\n", k, size / 1024, p);
            tm_phy_log("after it");
        }
    }
    memcpy(sCopy[k].p, src, w * h * 3 / 2);
    sCopyAt = (k + 1) % sCopyRing;
    dt = tm_now() - t0;
    sCopyUs += dt;
    sCopyN++;
    if (dt > sCopyMax) {
        sCopyMax = dt;
    }
    return (const uint8_t *)sCopy[k].p;
}
static void tm_copies_free(void) {
    int k;
    for (k = 0; k < TM_COPIES; k++) {
        if (sCopy[k].p != NULL) {
            if (sCopy[k].blk >= 0) {
                sceGxmUnmapMemory(sCopy[k].p);
                sceKernelFreeMemBlock(sCopy[k].blk);
            }
            sCopy[k].p = NULL;
        }
    }
    sCopyAt = 0;
    sCopyRing = TM_COPIES;
    sCopyOff = 0;
}

/* The thread's end: the frame given up first (the drawing thread stops reading it), the player closed, the state cleared. */
static void tm_finish(SceAvPlayerHandle h, int failed) {
    tm_lock();
    sM.frame = NULL;
    sM.stopping = 1;
    sM.allocFailed = 0;
    if (failed) {
        sM.failedAt = tm_now();
        if (++sFails >= 2) { /* (the pool is not given back and taken again for ever for a film that cannot play) */
            sNever = 1;
            Vita_Log("tagmovie: the film failed %d times: no film this session\n", sFails);
        }
    }
    tm_unlock();
    if (TM_OK(h)) {
        sceAvPlayerStop(h);
        sceAvPlayerClose(h);
    }
    if (sCopy[0].p != NULL) {
        sceKernelDelayThread(TM_FREE_DELAY_US); /* (the chip may still be reading the last picture shown) */
        tm_copies_free();
    }
    tm_lock();
    sPoolEndAt = tm_now();
    sM.running = 0;
    sM.stopping = 0;
    sM.inLoop = 0;
    tm_unlock();
    sceKernelExitDeleteThread(0);
}

static int tm_thread(SceSize args, void *argp) {
    SceAvPlayerHandle h = TM_NONE;
    int seen = 0; /* pictures given since the source began */
    uint64_t origin = 0, startedAt = 0, logAt, diagAt = 0;
    unsigned polls = 0, active = 0, audio = 0, infoDone = 0;
    (void)args;
    (void)argp;

    {
        int m = sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER); /* (a module already loaded answers an error code too) */
        Vita_Log("tagmovie: sceSysmoduleLoadModule(AVPLAYER) = 0x%08X\n", (unsigned)m);
    }
    /* A restart asked before this thread ran is dropped: the player starts from the beginning anyway. (Batch 33: the first thing the
       thread did was Stop + AddSource on the player it had just made, which then gave no picture for 3 s; its failure closed it, and
       the console went off a few seconds into the next player's film.) */
    tm_lock();
    sM.restart = 0;
    tm_unlock();
    tm_phy_log("the playing thread starts");
    /* (a fresh player: Init, AddSource, SetLooping: no Stop first) */
    if (tm_play(&h, TM_INTRO, 0) < 0) {
        Vita_Log("tagmovie: the player cannot play %s (the package carries tbo/ui/tag_ui_*.mp4?): the poster\n", TM_INTRO);
        tm_finish(h, 1);
    }
    tm_lock();
    sM.inLoop = 0;
    sM.core = (int)sceKernelGetCpuId();
    tm_unlock();
    Vita_Log("tagmovie: started, tbomovie=%d, the hardware player (%d output frames of physically contiguous memory), on the %s core\n", gVitaTagMovie,
             TM_OUT_FRAMES, gVitaCore3 == 1 ? "fourth" : "second");
    origin = startedAt = tm_now();
    logAt = origin + TM_LOG_US;
    diagAt = origin;
    for (;;) {
        SceAvPlayerFrameInfo info;
        uint64_t now = tm_now();
        int restart = 0;
        tm_lock();
        if (now - sM.touched > TM_IDLE_US) {
            tm_unlock();
            break;
        }
        if (sM.restart) {
            sM.restart = 0;
            restart = 1;
        }
        tm_unlock();
        if (restart && (!seen || (!sM.inLoop && now - startedAt < 1500000u))) {
            restart = 0; /* (the source began a moment ago, or gave no picture yet: it is at its start) */
        }
        if (restart) {
            /* the intro from its start */
            if (tm_play(&h, TM_INTRO, 0) < 0) {
                tm_finish(h, 1);
            }
            seen = 0;
            origin = startedAt = diagAt = now;
            polls = active = audio = infoDone = 0;
            tm_lock();
            sM.inLoop = 0;
            tm_unlock();
        }
        if (now >= logAt) {
            unsigned decoded, late, shown, replaced, uploads;
            uint64_t uploadUs, uploadMax, live, peak, copyUs, copyMax;
            unsigned copyN;
            size_t liveFrame, liveOther;
            int inLoop;
            tm_lock();
            decoded = sM.decoded;
            late = sM.late;
            shown = sM.shown;
            replaced = sM.replaced;
            uploads = sM.uploads;
            uploadUs = sM.uploadUs;
            uploadMax = sM.uploadMax;
            liveFrame = sM.liveFrame;
            liveOther = sM.liveOther;
            peak = sM.peak;
            inLoop = sM.inLoop;
            sM.decoded = sM.late = sM.shown = sM.replaced = sM.uploads = 0;
            sM.uploadUs = sM.uploadMax = 0;
            copyN = sCopyN;
            copyUs = sCopyUs;
            copyMax = sCopyMax;
            sCopyN = 0;
            sCopyUs = sCopyMax = 0;
            tm_unlock();
            live = liveFrame + liveOther;
            Vita_Log("tagmovie: %u frames from the player, %u shown, %u replaced before shown, %u late (over 85 ms); texture set %u us a "
                     "frame (worst %u us); memory %u KB (frames %u KB, other %u KB; peak %u KB); the %s; the %s core\n",
                     decoded, shown, replaced, late, uploads ? (unsigned)(uploadUs / uploads) : 0u, (unsigned)uploadMax,
                     (unsigned)(live / 1024), (unsigned)(liveFrame / 1024), (unsigned)(liveOther / 1024), (unsigned)(peak / 1024),
                     inLoop ? "loop" : "intro", gVitaCore3 == 1 ? "fourth" : "second");
            Vita_Log("tagmovie: copies %s: %u made, %u us each (worst %u us)\n", gVitaTagMovieCopy ? "on" : "off", copyN,
                     copyN ? (unsigned)(copyUs / copyN) : 0u, (unsigned)copyMax);
            tm_phy_log("every 2 s");
            logAt = now + TM_LOG_US;
        }
        /* the audio queue drained (the films have no audio track; an undrained queue could stall a player) */
        {
            SceAvPlayerFrameInfo ainfo;
            memset(&ainfo, 0, sizeof(ainfo));
            if (sceAvPlayerGetAudioData(h, &ainfo)) {
                if (audio++ == 0) {
                    Vita_Log("tagmovie: the player gave audio data (%u Hz, %u channels, %u bytes): drained\n", ainfo.details.audio.sampleRate,
                             (unsigned)ainfo.details.audio.channelCount, ainfo.details.audio.size);
                }
            }
        }
        /* the first seconds of a source: what the player says (IsActive, GetVideoData, the streams), every TM_DIAG_US */
        polls++;
        if (!seen && now - startedAt <= TM_NOFRAME_US + TM_DIAG_US) {
            int act = sceAvPlayerIsActive(h);
            active += act ? 1 : 0;
            if (now >= diagAt) {
                Vita_Log("tagmovie: %u ms after the source: IsActive=%d (%u of %u polls), %u frames so far, current time %u ms\n",
                         (unsigned)((now - startedAt) / 1000), act, active, polls, sM.decoded, (unsigned)sceAvPlayerCurrentTime(h));
                diagAt = now + TM_DIAG_US;
                if (!infoDone && now - startedAt >= 500000u) {
                    unsigned id;
                    infoDone = 1;
                    for (id = 0; id < 2; id++) {
                        SceAvPlayerStreamInfo si;
                        int r;
                        memset(&si, 0, sizeof(si));
                        r = sceAvPlayerGetStreamInfo(h, id, &si);
                        Vita_Log("tagmovie: GetStreamInfo(%u) = 0x%08X: type %u, %ux%u, duration %u ms\n", id, (unsigned)r, si.type,
                                 si.details.video.width, si.details.video.height, (unsigned)si.duration);
                    }
                }
            }
        }
        memset(&info, 0, sizeof(info));
        if (sceAvPlayerGetVideoData(h, &info)) {
            if (!seen) {
                Vita_Log("tagmovie: first picture %u ms after the source: %ux%u at %p, time stamp %u ms\n", (unsigned)((now - startedAt) / 1000),
                         info.details.video.width, info.details.video.height, (void *)info.pData, (unsigned)info.timeStamp);
            }
            /* the picture's time from the source's start (the player's timeStamp, in ms) */
            int late = now - origin > (uint64_t)info.timeStamp * 1000u + TM_LATE_US;
            const uint8_t *shown = info.pData;
            if (seen < 12) {
                Vita_Log("tagmovie: picture %d: %p (offset 0x%X in its 4 KB), time stamp %u ms\n", seen, (void *)info.pData,
                         (unsigned)((uintptr_t)info.pData & 0xFFF), (unsigned)info.timeStamp);
            }
            if (gVitaTagMovieCopy) {
                const uint8_t *c = tm_copy(info.pData, info.details.video.width, info.details.video.height);
                if (c != NULL) {
                    shown = c;
                }
            }
            tm_lock();
            sM.frame = shown;
            sM.w = info.details.video.width;
            sM.h = info.details.video.height;
            sM.serial++;
            sM.decoded++;
            if (late) {
                sM.late++;
            }
            tm_unlock();
            seen++;
            continue;
        }
        if (!seen && (now - startedAt > TM_NOFRAME_US || sM.allocFailed)) {
            Vita_Log("tagmovie: the player gives no picture from %s (%s; IsActive %u of %u polls, audio data %u times): the poster\n",
                     sM.inLoop ? TM_LOOP : TM_INTRO, sM.allocFailed ? "its memory could not be given" : "in 3 s", active, polls, audio);
            tm_finish(h, 1);
        }
        if (seen && !sceAvPlayerIsActive(h)) {
            /* the intro's end: the loop, looping; the loop ending (it should not) starts it again */
            if (tm_play(&h, TM_LOOP, 1) < 0) {
                Vita_Log("tagmovie: the player cannot play %s: the poster\n", TM_LOOP);
                tm_finish(h, 1);
            }
            seen = 0;
            origin = startedAt = diagAt = tm_now();
            polls = active = audio = infoDone = 0;
            tm_lock();
            sM.inLoop = 1;
            tm_unlock();
        }
        sceKernelDelayThread(TM_POLL_US);
    }
    tm_finish(h, 0);
    return 0;
}

/* The game's Tag Team screens opened (src/menu/port_tbo.c): the film from its start. */
extern void Vita_TagMovieSwBegin(void); /* vita_tagmovie_sw.c (tbomovie=2) */
void Port_TagMenuMovieBegin(void) {
    Vita_TagMovieSwBegin();
    if (sLock < 0) {
        sLock = sceKernelCreateMutex("bt3_tagmovie", 0, 0, NULL);
    }
    if (sLock < 0) {
        return;
    }
    tm_lock();
    sM.restart = 1;
    tm_unlock();
}

/* The drawing thread (vita_tbo.c, for the film's picture): the texture of the newest frame, 0 for none (the poster). */
unsigned Vita_TagMovieTexture(void) {
    uint64_t t0;
    unsigned tex;
    if (sLock < 0) {
        return 0;
    }
    tm_lock();
    t0 = tm_now();
    sM.touched = t0;
    if (!sM.running && !sM.stopping && !sNever && (sM.failedAt == 0 || t0 - sM.failedAt > TM_RETRY_US) && gVitaTagMovieMem == 0 &&
        sPoolSize == 0 && !(gVitaTagMovieStage == 1 && sStageDone)) {
        /* (the drawing thread's call) vitaGL's pool given back to the system for the film; taken again after it. The film starts only when
           the system shows that memory free: else the pool is taken again at once and there is no film this session (the poster) */
        unsigned total = 0, freeb = 0, got;
        int before, after;
        Vita_VglPhycontInfo(&total, &freeb);
        before = tm_phy_free();
        Vita_Log("tagmovie: vitaGL's pool: %u bytes, %u free (%u in use); the system's free physically contiguous memory %d KB; giving the pool back\n",
                 total, freeb, total - freeb, before < 0 ? -1 : before / 1024);
        got = Vita_VglPhycontRelease();
        after = tm_phy_free();
        if (got == 0) {
            Vita_Log("tagmovie: vitaGL's physically contiguous pool cannot be given back (something is allocated in it, or it is not there): the poster, no film this session\n");
            sNever = 1;
            sM.failedAt = t0;
        } else if (after >= 0 && (unsigned)after < ((unsigned)(18 + (gVitaTagMovieReserve < 0 ? 0 : gVitaTagMovieReserve)) << 20)) {
            unsigned back = Vita_VglPhycontReacquire(got);
            Vita_Log("tagmovie: the pool (%u KB) was given back but the system shows only %d KB free (the film needs 18 MB and %d MB kept free): "
                     "the pool taken again (%u KB): the poster, no film this session\n", got / 1024, after / 1024, gVitaTagMovieReserve, back / 1024);
            sNever = 1;
            sM.failedAt = t0;
        } else {
            sPoolSize = got;
            sPoolEndAt = t0;
            Vita_Log("tagmovie: vitaGL's physically contiguous pool (%u KB) given back to the system for the film; the system shows %d KB free\n",
                     got / 1024, after < 0 ? -1 : after / 1024);
            if (gVitaTagMovieStage == 1) {
                sStageDone = 1;
                Vita_Log("tagmovie: tbomoviestage=1: no player, the pool is taken again in %u ms\n", TM_POOL_BACK_US / 1000);
            }
        }
    }
    if (!sM.running && !sM.stopping && !sNever && gVitaTagMovieStage != 1 && (sM.failedAt == 0 || t0 - sM.failedAt > TM_RETRY_US) &&
        (gVitaTagMovieMem != 0 || sPoolSize != 0)) {
        SceUID th = Vita_ThreadOnCore3("bt3_tagmovie", tm_thread, TM_PRIORITY, TM_STACK, 0x00020000 /* else the second core */);
        if (th >= 0 && sceKernelStartThread(th, 0, NULL) >= 0) {
            sM.running = 1;
        } else {
            Vita_Log("tagmovie: the playing thread could not be started (0x%08X)\n", (unsigned)th);
            sM.failedAt = t0;
        }
    }
    if (!sM.running) {
        /* not playing: its texture given back (the drawing thread's, here) */
        tex = sM.tex;
        sM.tex = 0;
        tm_unlock();
        if (tex != 0) {
            Vita_VglTexFree(tex);
        }
        return 0;
    }
    if (sM.frame != NULL && sM.serial != sM.uploaded) {
        uint64_t u0 = tm_now(), dt;
        sM.tex = Vita_VglTexYuv(sM.tex, sM.frame, (int)sM.w, (int)sM.h);
        if (sM.serial - sM.uploaded > 1) {
            sM.replaced += sM.serial - sM.uploaded - 1;
        }
        sM.uploaded = sM.serial;
        dt = tm_now() - u0;
        sM.shown++;
        sM.uploads++;
        sM.uploadUs += dt;
        if (dt > sM.uploadMax) {
            sM.uploadMax = dt;
        }
    }
    tex = sM.frame != NULL ? sM.tex : 0;
    sM.texFrame = gGsFrame;
    tm_unlock();
    return tex;
}

/* vita_tbo.c's frame end (every 64th picture): the texture not drawn for TM_KEEP_FRAMES given back. */
void Vita_TagMovieIdle(void) {
    unsigned t = 0;
    if (sLock < 0) {
        return;
    }
    tm_lock();
    if (sM.tex != 0 && gGsFrame - sM.texFrame > TM_KEEP_FRAMES) {
        t = sM.tex;
        sM.tex = 0;
    }
    if (sPoolSize != 0 && !sM.running && !sM.stopping && sM.liveFrame == 0 && sCopy[0].p == NULL && tm_now() - sPoolEndAt > TM_POOL_BACK_US) {
        /* the player closed a while ago and its memory is back with the system: vitaGL's pool made again (drawing thread) */
        unsigned want = sPoolSize, got;
        sPoolSize = 0;
        tm_unlock();
        got = Vita_VglPhycontReacquire(want);
        Vita_Log("tagmovie: vitaGL's physically contiguous pool taken again: %u of %u KB\n", got / 1024, want / 1024);
        if (t != 0) {
            Vita_VglTexFree(t);
        }
        return;
    }
    tm_unlock();
    if (t != 0) {
        Vita_VglTexFree(t);
    }
}
