/*
 * An online session inside the running game, as the PC's 64-bit programs make it (port/src/gs/state.c, "Exchanging
 * the game's state"): the lobbies (Create Match, Join by address, Quick Match, the Ad Hoc lobby) need it, because a
 * lobby lives on while its matches come and go (net_lobby.c session_can -> Port_SessionCan). The Vita had only the
 * restart (vita_netglue.c execv: session.txt + the application started again), which a lobby does not outlive.
 *
 * How (the plan: psvita-agent notes/online/vita-multiplayer-plan.md 7.12):
 *   - the game's state is its global variables (the ranges port/vita/tools/vita_state_list.py writes into
 *     gVitaStateList from the linker's map at link time), its heap (vita_mem.c, one block), the blocks it took outside
 *     the heap (Port_LowAlloc: file handles, "IOP memory"; Port_GameBigAlloc: fighters' tables, a large stage, Tag
 *     Team's pools; kept in a list here), the game thread's stack from its position to its top, and its registers
 *     (vita_session_a.S);
 *   - a snapshot keeps that memory in 4 KB pieces: a piece all zero is not stored, and a piece that is the same as the
 *     first snapshot's (sBoot, taken at the game's first blank) is not stored again: the menus' snapshot (sOwn) is
 *     only what the game changed since its start. (Two whole copies of ~32 MB would not fit beside the game.)
 *   - putting a snapshot back runs on another stack and continues where the snapshot was taken: the function that
 *     took it returns a second time. A block the game gave back while a snapshot holds it is kept (not freed) until
 *     no snapshot holds it, so that it is where the state put back expects it.
 *   - around it: the reading thread idle (its jobs write into the game's memory), the render thread idle (it reads
 *     the game's lists), the emulated GS's memory kept and put back (gs_core.c Gs_StateKeep / Gs_StateBack, as the
 *     PC), the sound made to fit (snd_adx.c Port_AdxResync), and what a session needs of the Vita set and undone
 *     (what Vita_SessionLoad does after a restart: reads on the game's thread, the PC's way of computing, the PC's
 *     neutral Vita Settings).
 *
 * bt3.txt session_inproc: 0 the restart (the default), 1 sessions inside the running game, 3 measure only (the
 * snapshots taken and their sizes and times logged; nothing started). session_test=<blank>, session_leave=<n>,
 * session_again=<n>: a session without a network asked at that blank, left n blanks after it began, and asked again
 * n blanks after each return (the test of the exchange alone: menus -> the game's start -> menus).
 *
 * Built in BT4_VITA_NETLINK=1 builds only (with net.c, whose session begin and end it calls).
 */
#include <malloc.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void Vita_Log(const char *fmt, ...);
extern int gVitaSessionInproc;                      /* bt3.txt session_inproc (vita_net.c) */
extern int gVitaSessionTest[3];                     /* session_test, session_leave, session_again (vita_net.c) */
extern unsigned gPortVBlanks;                       /* plat_stub.c */
extern int gPortResim;                              /* vita_bt4.c */
extern void Vita_HeapRegion(uint8_t **base, uint32_t *size); /* vita_mem.c */
extern int Vita_CtxSave(uint32_t *ctx);             /* vita_session_a.S */
extern void Vita_CtxJump(const uint32_t *ctx);
extern void Vita_CallOnStack(void (*fn)(void), void *top);

/* ---- the list of the game's variables: written into the program by vita_state_list.py after the link ---- */
#define VITA_STATE_CAP 8192
#define VITA_STATE_MAGIC 0x314C5356u
/* [0] the mark, [1] ranges, [2] gPortVBlanks's link address, [3] gPortResim's, then (start, size) per range.
   volatile: read as the program holds it, not as this file's initializer says */
const volatile uint32_t gVitaStateList[4 + 2 * VITA_STATE_CAP] __attribute__((used, aligned(16))) = {VITA_STATE_MAGIC};

#define CH 4096u /* a snapshot's piece */
#define SAME ((uint8_t *)1)

typedef struct Region { uint8_t *at; uint32_t n; } Region;
#define MAX_REGIONS (VITA_STATE_CAP + 4)
static Region *sReg;
static int sRegions = -1, sListed;
static uint32_t sGlobalBytes;
static uint8_t *sStackTop, *sStackBase; /* the game thread's stack: its top and its lowest address */

/* ---- the blocks outside the heap (vita_mem.c Port_LowAlloc / Port_GameBigAlloc) ---- */
typedef struct VitaBlock {
    uint32_t magic, check;
    struct VitaBlock *next, *prev;
    uint32_t size;
    uint16_t kind, dead; /* kind 1 low, 2 big; dead: the game gave it back, a snapshot still holds it */
    uint32_t pins;       /* snapshots that hold it */
    uint32_t pad[9];
} VitaBlock;             /* 64 bytes: the game's block after it stays 64-byte aligned */
#define BLOCK_MAGIC 0xB10C5E55u
static VitaBlock *sBlocks;
static unsigned sBlockCount, sBlockDead;
static volatile int sBlockLock;

static void block_lock(void) {
    while (__atomic_exchange_n(&sBlockLock, 1, __ATOMIC_ACQUIRE)) {
        sceKernelDelayThread(10);
    }
}
static void block_unlock(void) { __atomic_store_n(&sBlockLock, 0, __ATOMIC_RELEASE); }

static void block_unlink(VitaBlock *b) {
    if (b->prev != NULL) { b->prev->next = b->next; } else { sBlocks = b->next; }
    if (b->next != NULL) { b->next->prev = b->prev; }
    sBlockCount--;
    b->magic = 0;
    free(b);
}

void *Vita_BlockAlloc(uint32_t size, int kind) {
    VitaBlock *b = memalign(64, sizeof(VitaBlock) + size);
    if (b == NULL) {
        return NULL;
    }
    memset(b, 0, sizeof(VitaBlock) + size); /* (zeroed: Port_LowAlloc's are calloc's, and a stored state is smaller) */
    b->magic = BLOCK_MAGIC;
    b->check = (uint32_t)(uintptr_t)b ^ BLOCK_MAGIC;
    b->size = size;
    b->kind = (uint16_t)kind;
    block_lock();
    b->next = sBlocks;
    if (sBlocks != NULL) {
        sBlocks->prev = b;
    }
    sBlocks = b;
    sBlockCount++;
    block_unlock();
    return b + 1;
}

static VitaBlock *block_of(const void *p) {
    VitaBlock *b;
    if (p == NULL || (uintptr_t)p < sizeof(VitaBlock)) {
        return NULL;
    }
    b = (VitaBlock *)p - 1;
    return b->magic == BLOCK_MAGIC && b->check == ((uint32_t)(uintptr_t)b ^ BLOCK_MAGIC) ? b : NULL;
}

int Vita_BlockIs(const void *p) {
    return block_of(p) != NULL;
}

/* 1 if p was such a block (given back; or kept for a snapshot that holds it), 0 if not */
int Vita_BlockFree(void *p) {
    VitaBlock *b = block_of(p);
    if (b == NULL) {
        return 0;
    }
    block_lock();
    if (b->pins != 0) {
        if (!b->dead) {
            b->dead = 1;
            sBlockDead++;
        }
    } else {
        if (b->dead) {
            sBlockDead--;
        }
        block_unlink(b);
    }
    block_unlock();
    return 1;
}

/* ---- the regions ---- */
static void regions_init(void) {
    uint8_t *heap;
    uint32_t heapSize, i, n;
    sRegions = 0;
    sReg = calloc(MAX_REGIONS, sizeof(Region));
    if (sReg == NULL) {
        return;
    }
    n = gVitaStateList[1];
    if (gVitaStateList[0] != VITA_STATE_MAGIC || n == 0 || n > VITA_STATE_CAP) {
        Vita_Log("session: the program holds no list of the game's variables (built without vita_state_list.py?)\n");
    } else {
        /* link addresses -> run-time ones: the data segment as a whole was moved by the loader */
        uintptr_t delta = (uintptr_t)&gPortVBlanks - gVitaStateList[2];
        if ((uintptr_t)&gPortResim - delta != gVitaStateList[3]) {
            Vita_Log("session: the list of the game's variables is not this program's (marks %08x %08x, here %08x %08x)\n",
                     (unsigned)gVitaStateList[2], (unsigned)gVitaStateList[3], (unsigned)(uintptr_t)&gPortVBlanks,
                     (unsigned)(uintptr_t)&gPortResim);
        } else {
            for (i = 0; i < n; i++) {
                sReg[sRegions].at = (uint8_t *)(gVitaStateList[4 + 2 * i] + delta);
                sReg[sRegions].n = gVitaStateList[5 + 2 * i];
                sGlobalBytes += sReg[sRegions].n;
                sRegions++;
            }
            sListed = 1;
        }
    }
    Vita_HeapRegion(&heap, &heapSize);
    sReg[sRegions].at = heap;
    sReg[sRegions].n = heapSize;
    sRegions++;
    {
        SceKernelThreadInfo info;
        memset(&info, 0, sizeof(info));
        info.size = sizeof(info);
        if (sceKernelGetThreadInfo(0, &info) >= 0) {
            sStackTop = (uint8_t *)info.stack + info.stackSize;
            sStackBase = (uint8_t *)info.stack;
        }
    }
    Vita_Log("session: %d ranges of the game's variables (%u KB), heap %p + %u KB, game stack top %p\n", sRegions - 1,
             (unsigned)(sGlobalBytes >> 10), (void *)heap, (unsigned)(heapSize >> 10), (void *)sStackTop);
}

/* ---- snapshots ---- */
#define SLAB_SZ (256u * 1024u)
#define MAX_SLABS 256

typedef struct Slab { uint8_t *base; SceUID uid; } Slab;
typedef struct SPart { uint8_t *at; uint32_t n; VitaBlock *blk; uint32_t first; int base; } SPart; /* first: its first piece */
typedef struct Snap {
    SPart *part;
    int parts;
    uint8_t **pg;                 /* per piece: NULL all zero, SAME as the base's, else 2-byte len + RLE/raw data */
    uint32_t pieces;
    Slab slabs[MAX_SLABS];
    uint32_t nSlabs, sysSlabs, slabPos, poolBytes;
    uint8_t *stackAt, *stackCopy;
    uint32_t stackN;
    uint32_t ctx[28] __attribute__((aligned(8)));
    struct Snap *base;
    uint32_t nZero, nSame, nStored, blocks, blockBytes, us;
    int taken;
} Snap;

static uint32_t rle_pack(const uint8_t *src, uint32_t n, uint8_t *dst) {
    uint32_t i = 0, o = 0;
    while (i < n) {
        if (src[i] == 0) {
            uint32_t run = 1;
            while (i + run < n && src[i + run] == 0 && run < 128) {
                run++;
            }
            dst[o++] = (uint8_t)(run - 1);
            i += run;
        } else {
            uint32_t run = 1;
            while (i + run < n && run < 128) {
                if (src[i + run] == 0 && i + run + 1 < n && src[i + run + 1] == 0) {
                    break;
                }
                run++;
            }
            dst[o++] = (uint8_t)(0x80u | (run - 1));
            memcpy(dst + o, src + i, run);
            o += run;
            i += run;
        }
    }
    return o;
}

static void rle_unpack(const uint8_t *src, uint8_t *dst, uint32_t n) {
    uint32_t len = (uint32_t)src[0] | ((uint32_t)src[1] << 8);
    if (len == 0) {
        memcpy(dst, src + 2, n);
        return;
    }
    {
        const uint8_t *p = src + 2, *end = p + len;
        uint32_t o = 0;
        while (p < end && o < n) {
            uint8_t tag = *p++;
            uint32_t run = (uint32_t)(tag & 0x7Fu) + 1u;
            if (run > n - o) {
                run = n - o;
            }
            if ((tag & 0x80u) == 0) {
                memset(dst + o, 0, run);
            } else {
                memcpy(dst + o, p, run);
                p += run;
            }
            o += run;
        }
    }
}

static uint8_t *slab_alloc(Snap *s, uint32_t need) {
    if (s->nSlabs == 0 || s->slabPos + need > SLAB_SZ) {
        uint8_t *p;
        SceUID uid = -1;
        if (s->nSlabs >= MAX_SLABS) {
            return NULL;
        }
        p = malloc(SLAB_SZ);
        if (p == NULL) {
            void *base = NULL;
            uid = sceKernelAllocMemBlock("bt4_snap", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, SLAB_SZ, NULL);
            if (uid < 0 || sceKernelGetMemBlockBase(uid, &base) < 0 || base == NULL) {
                if (uid >= 0) {
                    sceKernelFreeMemBlock(uid);
                }
                return NULL;
            }
            p = (uint8_t *)base;
            s->sysSlabs++;
        }
        s->slabs[s->nSlabs].base = p;
        s->slabs[s->nSlabs].uid = uid;
        s->nSlabs++;
        s->slabPos = 0;
    }
    {
        uint8_t *ret = s->slabs[s->nSlabs - 1].base + s->slabPos;
        s->slabPos += need;
        s->poolBytes += need;
        return ret;
    }
}

static int is_zero(const uint8_t *p, uint32_t n) {
    const uint32_t *w = (const uint32_t *)p;
    uint32_t i, k = n / 4;
    if (((uintptr_t)p & 3) != 0) {
        for (i = 0; i < n; i++) {
            if (p[i]) { return 0; }
        }
        return 1;
    }
    for (i = 0; i < k; i++) {
        if (w[i]) { return 0; }
    }
    for (i = k * 4; i < n; i++) {
        if (p[i]) { return 0; }
    }
    return 1;
}

static void snap_free(Snap *s) {
    int k;
    block_lock();
    for (k = 0; k < s->parts; k++) {
        VitaBlock *b = s->part[k].blk;
        if (b != NULL && b->pins != 0 && --b->pins == 0 && b->dead) {
            sBlockDead--;
            block_unlink(b);
        }
    }
    block_unlock();
    free(s->part);
    free(s->pg);
    for (k = 0; k < (int)s->nSlabs; k++) {
        if (s->slabs[k].uid >= 0) {
            sceKernelFreeMemBlock(s->slabs[k].uid);
        } else {
            free(s->slabs[k].base);
        }
    }
    free(s->stackCopy);
    memset(s, 0, sizeof(*s));
}

/* the base's piece for piece j of part k of s (NULL: zero) */
static const uint8_t *base_piece(const Snap *s, int k, uint32_t j) {
    const Snap *b = s->base;
    const SPart *bp;
    uint8_t *q;
    if (b == NULL || s->part[k].base < 0) {
        return NULL;
    }
    bp = &b->part[s->part[k].base];
    q = b->pg[bp->first + j];
    return q == SAME ? NULL : q; /* (the base has no base of its own) */
}

/* The memory of a snapshot (everything but the stack and the registers). 0: done, -1: no memory. */
static int snap_memory(Snap *s, Snap *base) {
    VitaBlock *b;
    int k, cap;
    uint32_t j, stored = 0, pieces = 0;
    uint8_t *cls;
    unsigned long long t0 = sceKernelGetProcessTimeWide();

    memset(s, 0, sizeof(*s));
    s->base = base;
    block_lock();
    cap = sRegions + (int)sBlockCount;
    s->part = calloc((size_t)cap, sizeof(SPart));
    if (s->part == NULL) {
        block_unlock();
        return -1;
    }
    for (k = 0; k < sRegions; k++) {
        s->part[s->parts].at = sReg[k].at;
        s->part[s->parts].n = sReg[k].n;
        s->part[s->parts].base = base != NULL ? k : -1;
        s->parts++;
    }
    for (b = sBlocks; b != NULL && s->parts < cap; b = b->next) {
        if (b->dead) {
            continue;
        }
        s->part[s->parts].at = (uint8_t *)(b + 1);
        s->part[s->parts].n = b->size;
        s->part[s->parts].blk = b;
        s->part[s->parts].base = -1;
        if (base != NULL) {
            int m;
            for (m = sRegions; m < base->parts; m++) {
                if (base->part[m].blk == b && base->part[m].n == b->size) {
                    s->part[s->parts].base = m;
                    break;
                }
            }
        }
        b->pins++;
        s->blocks++;
        s->blockBytes += b->size;
        s->parts++;
    }
    block_unlock();
    for (k = 0; k < s->parts; k++) {
        s->part[k].first = pieces;
        pieces += (s->part[k].n + CH - 1) / CH;
    }
    s->pieces = pieces;
    s->pg = calloc(pieces, sizeof(uint8_t *));
    if (s->pg == NULL) {
        snap_free(s);
        return -1;
    }
    /* store changed pieces in 256 KB slabs with zero-run RLE (falls back to system memory if C heap is full) */
    for (k = 0; k < s->parts; k++) {
        const SPart *p = &s->part[k];
        for (j = 0; j * CH < p->n; j++) {
            const uint8_t *m = p->at + j * CH, *bq;
            uint32_t n = p->n - j * CH < CH ? p->n - j * CH : CH;
            uint8_t c;
            if (base != NULL && p->base >= 0) {
                bq = base_piece(s, k, j);
                if (bq == NULL) {
                    c = is_zero(m, n) ? 1 : 2;
                } else {
                    uint8_t bbuf[CH];
                    rle_unpack(bq, bbuf, n);
                    c = memcmp(m, bbuf, n) == 0 ? 1 : 2;
                }
            } else {
                c = is_zero(m, n) ? 0 : 2;
            }
            if (c == 0) {
                s->nZero++;
            } else if (c == 1) {
                s->pg[p->first + j] = base_piece(s, k, j) == NULL ? NULL : SAME;
                s->nSame++;
            } else {
                uint8_t tmp[CH + 64];
                uint32_t plen = rle_pack(m, n, tmp);
                uint32_t pay = plen < n ? plen : n;
                uint16_t hdr = plen < n ? (uint16_t)plen : 0;
                uint8_t *q = slab_alloc(s, 2u + pay);
                if (q == NULL) {
                    snap_free(s);
                    return -1;
                }
                q[0] = (uint8_t)(hdr & 0xFFu);
                q[1] = (uint8_t)(hdr >> 8);
                memcpy(q + 2, hdr != 0 ? tmp : m, pay);
                s->pg[p->first + j] = q;
                s->nStored++;
            }
        }
    }
    (void)stored;
    (void)cls;
    s->us = (uint32_t)(sceKernelGetProcessTimeWide() - t0);
    return 0;
}

/* Takes a whole snapshot. Returns 0 after taking it, 1 when it was put back (snap_load) and this returns again,
   -1 when there was no memory for it. */
static int __attribute__((noinline)) snap_save(Snap *s, Snap *base) {
    uint8_t *sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    sp -= 256;
    if (snap_memory(s, base) != 0) {
        return -1;
    }
    s->stackAt = sp;
    s->stackN = (uint32_t)(sStackTop - sp);
    s->stackCopy = malloc(s->stackN);
    if (s->stackCopy == NULL) {
        snap_free(s);
        return -1;
    }
    s->taken = 1;
    memcpy(s->stackCopy, s->stackAt, s->stackN); /* (the last thing before the registers: the stack as it is now) */
    return Vita_CtxSave(s->ctx);
}

static Snap *sLoading;

#define LOADER_STACK 0x10000

/* Copies the snapshot's memory back and continues at its save. Runs on its own stack at the bottom of the game
   thread's (the system stops a thread whose stack pointer is outside its own stack when it calls the kernel: a
   stack from the C heap crashed in free(), console run online_sess_s2); and calls nothing of the kernel. */
static void loader(void) {
    Snap *s = sLoading;
    int k;
    uint32_t j;
    for (k = 0; k < s->parts; k++) {
        const SPart *p = &s->part[k];
        for (j = 0; j * CH < p->n; j++) {
            uint32_t n = p->n - j * CH < CH ? p->n - j * CH : CH;
            const uint8_t *q = s->pg[p->first + j];
            if (q == SAME) {
                q = base_piece(s, k, j);
            }
            if (q == NULL) {
                memset(p->at + j * CH, 0, n);
            } else {
                rle_unpack(q, p->at + j * CH, n);
            }
        }
    }
    memcpy(s->stackAt, s->stackCopy, s->stackN);
    Vita_CtxJump(s->ctx);
}

static void snap_load(Snap *s) {
    VitaBlock *b, *next;
    int k;
    /* the blocks (here, on the game's own stack: free() may call the kernel): those the snapshot holds are the
       game's again; those it does not are let go, or kept for another snapshot that holds them */
    block_lock();
    for (b = sBlocks; b != NULL; b = next) {
        int in = 0;
        next = b->next;
        for (k = sRegions; k < s->parts; k++) {
            if (s->part[k].blk == b) {
                in = 1;
                break;
            }
        }
        if (in) {
            if (b->dead) {
                b->dead = 0;
                sBlockDead--;
            }
        } else if (b->pins != 0) {
            if (!b->dead) {
                b->dead = 1;
                sBlockDead++;
            }
        } else {
            if (b->dead) {
                sBlockDead--;
            }
            block_unlink(b);
        }
    }
    block_unlock();
    sLoading = s;
    Vita_CallOnStack(loader, sStackBase + 0x4000 + LOADER_STACK); /* (does not come back: the save returns 1) */
}

/* ---- around an exchange ---- */
static void quiet(void) {
    extern void Port_GsSync(void);      /* gs_core.c: the render thread done with the lists handed over */
    extern void Vita_ReadDrain(void);   /* vita_read.c: no read in flight into the game's memory */
    Vita_ReadDrain();
    Port_GsSync();
}

static void free_mem(unsigned *userKb, unsigned *cHeapKb) {
    SceKernelFreeMemorySizeInfo fm;
    struct mallinfo mi = mallinfo();
    extern unsigned int _newlib_heap_size_user;
    memset(&fm, 0, sizeof(fm));
    fm.size = sizeof(fm);
    sceKernelGetFreeMemorySize(&fm);
    *userKb = (unsigned)(fm.size_user >> 10);
    /* the C heap: what newlib has not handed out of its fixed size (arena not yet grown + free in the arena) */
    *cHeapKb = (unsigned)((_newlib_heap_size_user - (unsigned)mi.arena + (unsigned)mi.fordblks) >> 10);
}

static void snap_say(const char *what, const Snap *s) {
    unsigned user, cheap;
    free_mem(&user, &cheap);
    Vita_Log("session: %s at blank %u: %u pieces of 4 KB (%u zero, %u as at the start, %u stored = %u KB -> %u KB in %u slabs, %u sys), %u blocks outside "
             "the heap (%u KB, %u kept for a snapshot), stack %u KB; %u.%03u ms; free: C heap %u KB, system %u KB\n",
             what, gPortVBlanks, s->pieces, s->nZero, s->nSame, s->nStored, s->nStored * (CH >> 10), s->poolBytes >> 10,
             s->nSlabs, s->sysSlabs, s->blocks, s->blockBytes >> 10,
             sBlockDead, s->stackN >> 10, s->us / 1000, s->us % 1000, cheap, user);
}

/* a checksum of the game's regions (the list is stable: the same twice in a row) */
static uint32_t regions_sum(void) {
    uint32_t h = 2166136261u;
    int k;
    for (k = 0; k < sRegions; k++) {
        const uint32_t *w = (const uint32_t *)sReg[k].at;
        uint32_t i, n = sReg[k].n / 4;
        for (i = 0; i < n; i++) {
            h = (h ^ w[i]) * 16777619u;
        }
    }
    return h;
}

/* What a session needs of the Vita (Vita_SessionLoad after a restart), set at its begin and undone at its end */
static int sWasExact, sWasNeutral, sWasPf;
static void conditions(int on) {
    extern int gVitaSimExact, gVitaNetNeutral, gVitaNetNeutralWanted;
    extern void Vita_ReadSync(int on); /* vita_read.c: reads on the caller's thread (asyncread=0) while on */
#ifdef BT4_PS2FLOAT_BUILD
    extern int gVitaPs2Float;
    extern void PortPF_Init(int on); /* vita_ps2float.c */
    if (on) {
        sWasPf = gVitaPs2Float;
        if (sWasPf == 0) {
            PortPF_Init(1); /* the PC computes with the PS2's floats: the session too (ps2float=0 outside it) */
        }
    } else if (sWasPf == 0) {
        PortPF_Init(0);
    }
    Vita_Log("session: ps2float %d\n", gVitaPs2Float);
#endif
    if (on) {
        sWasExact = gVitaSimExact;
        sWasNeutral = gVitaNetNeutral;
        gVitaSimExact = 1;
        if (gVitaNetNeutralWanted) {
            gVitaNetNeutral = 1;
        }
    } else {
        gVitaSimExact = sWasExact;
        gVitaNetNeutral = sWasNeutral;
    }
    Vita_ReadSync(on);
    Vita_Log("session: %s: reads on the game's thread %s, the PC's way of computing %s, neutral Vita Settings %s\n",
             on ? "the session's conditions set" : "the player's conditions back", on ? "on" : "as before", gVitaSimExact ? "on" : "off",
             gVitaNetNeutral ? "on" : "off");
}

/* ---- the session ---- */
extern void Gs_StateKeep(void), Gs_StateBack(void);                       /* gs_core.c (PORT_VITA) */
extern void Port_AdxResync(void);                                         /* snd_adx.c */
extern void Port_SeResync(void);                                          /* snd_se.c */
extern void Port_NetSessionBegin(int role, const char *address, int port); /* net.c */
extern void Port_NetSessionEnd(void);

static Snap sBoot, sOwn;
static int sBootTaken, sReqRole, sReqPort, sOwnKept, sTestSession;
static volatile int sReq;
static char sReqAddr[128];
static unsigned sBegan, sSessions;
static unsigned long long sT0;

int Port_SessionCan(void) {
    return gVitaSessionInproc == 1 && sBootTaken > 0;
}
int Port_SessionAsked(void) {
    return sOwnKept;
}
void Port_SessionRequest(int role, const char *address, int port) {
    if (gVitaSessionInproc != 1) {
        Vita_Log("session: asked for (role %d, %s:%d) with session_inproc=%d: nothing starts\n", role, address != NULL ? address : "?", port,
                 gVitaSessionInproc);
        if (gVitaSessionInproc == 3) {
            sReq = 3; /* (measure: the menus' snapshot as a session would take it) */
        }
        return;
    }
    sReqRole = role;
    sReqPort = port;
    snprintf(sReqAddr, sizeof(sReqAddr), "%s", address != NULL ? address : "");
    sReq = 1;
}

static void measure_own(const char *why) {
    Snap s;
    uint32_t h1, h2;
    quiet();
    h1 = regions_sum();
    if (snap_memory(&s, &sBoot) != 0) {
        Vita_Log("session: %s: no memory for the menus' snapshot\n", why);
        return;
    }
    h2 = regions_sum();
    snap_say(why, &s);
    if (h1 != h2) {
        Vita_Log("session: the game's memory changed while the snapshot was taken (%08x -> %08x)\n", (unsigned)h1, (unsigned)h2);
    }
    snap_free(&s);
}

/* At the top of every blank of the game's (plat_stub.c Port_VBlank), on the game's thread */
void Port_SessionPoll(void) {
    if (gVitaSessionInproc == 0 || sBootTaken < 0) {
        return;
    }
    if (!sBootTaken) {
        int r;
        uint32_t h1;
        if (sRegions < 0) {
            regions_init();
        }
        if (!sListed) {
            sBootTaken = -1;
            return;
        }
        quiet();
        h1 = regions_sum();
        sBootTaken = 1;
        r = snap_save(&sBoot, NULL);
        if (r < 0) {
            Vita_Log("session: no memory for the snapshot of the game's start: sessions restart the application\n");
            sBootTaken = -1;
            return;
        }
        if (r == 0) {
            snap_say("the game's start kept", &sBoot);
            if (regions_sum() != h1) {
                Vita_Log("session: the game's memory changed while its start was kept\n");
            }
            return;
        }
        /* a session begins: this is the game's first blank again */
        Port_AdxResync();
        Port_SeResync();
        Vita_Log("session: the game starts again for the session (%u ms after it was asked for)\n",
                 (unsigned)((sceKernelGetProcessTimeWide() - sT0) / 1000));
        sBegan = 1;
        return;
    }
    if (gVitaSessionInproc == 3) {
        static const unsigned at[] = {600, 1800, 3600, 7200};
        unsigned k;
        for (k = 0; k < sizeof(at) / sizeof(at[0]); k++) {
            if (gPortVBlanks == at[k]) {
                measure_own("the menus' snapshot (measure)");
            }
        }
        if (gPortVBlanks > 7200 && gPortVBlanks % 3600 == 0) {
            measure_own("the menus' snapshot (measure)");
        }
        if (sReq == 3) {
            sReq = 0;
            measure_own("the menus' snapshot as a session would take it (measure)");
        }
        return;
    }
    {   /* the test without a network: session_test=<blank> asks for one, session_leave=<n> leaves it n blanks after its
           begin, session_again=<n> asks again n blanks after each return */
        if (gVitaSessionTest[0] > 0 && gPortVBlanks == (unsigned)gVitaSessionTest[0] && !sOwnKept) {
            Vita_Log("session: test: asked for at blank %u\n", gPortVBlanks);
            gVitaSessionTest[0] = -1; /* (once; session_again sets the next at the return) */
            sTestSession = 1;
            sReqRole = 0;
            sReq = 1;
        }
        if (sOwnKept && sTestSession && sBegan != 0 && gVitaSessionTest[1] > 0 && ++sBegan > (unsigned)gVitaSessionTest[1]) {
            Vita_Log("session: test: leaving at blank %u\n", gPortVBlanks);
            sBegan = 0;
            {
                extern void Port_SessionReturn(void);
                Port_SessionReturn();
            }
        }
    }
    if (sReq == 1) {
        int r;
        sReq = 0;
        sT0 = sceKernelGetProcessTimeWide();
        quiet();
        Gs_StateKeep();
        {
            extern void Vita_TboSessionKeep(void); /* vita_tbo.c: the screens' pictures to make again at the return */
            Vita_TboSessionKeep();
        }
        r = snap_save(&sOwn, &sBoot);
        if (r < 0) {
            Vita_Log("session: no memory for the menus' snapshot: the session does not start\n");
            return;
        }
        if (r == 0) {
            snap_say("the menus kept for a session", &sOwn);
            sOwnKept = 1;
            sSessions++;
            conditions(1);
            if (!sTestSession) {
                Port_NetSessionBegin(sReqRole, sReqAddr, sReqPort); /* connects (net.c) */
            }
            quiet();
            snap_load(&sBoot); /* (does not return: the start's save returns 1, above) */
        }
        /* back from the session, at the blank it was asked for */
        {
            unsigned ms = (unsigned)((sceKernelGetProcessTimeWide() - sT0) / 1000);
            gPortResim = 0;
            sOwnKept = 0;
            sTestSession = 0;
            sBegan = 0;
            conditions(0);
            Gs_StateBack();
            Port_AdxResync();
            Port_SeResync();
            snap_free(&sOwn);
            {
                extern void Vita_TboSessionBack(void); /* vita_tbo.c (session_warm): those pictures read again now */
                Vita_TboSessionBack();
            }
            {
                unsigned user, cheap;
                free_mem(&user, &cheap);
                Vita_Log("session: back in the menus at blank %u (session %u, %u ms to come back); free: C heap %u KB, system %u KB\n",
                         gPortVBlanks, sSessions, ms, cheap, user);
            }
            if (gVitaSessionTest[0] < 0 && gVitaSessionTest[2] > 0) {
                gVitaSessionTest[0] = (int)(gPortVBlanks + (unsigned)gVitaSessionTest[2]);
            }
        }
    }
}

/* The session is over (net.c Port_NetLeave): the menus as they were come back (does not return) */
void Port_SessionReturn(void) {
    if (!sOwnKept) {
        return;
    }
    if (!sTestSession) {
        Port_NetSessionEnd();
    }
    sT0 = sceKernelGetProcessTimeWide();
    quiet();
    snap_load(&sOwn);
}
