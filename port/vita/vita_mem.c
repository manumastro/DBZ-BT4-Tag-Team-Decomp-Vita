/*
 * Memory of the PS Vita build (port/src/plat_mem.c is the PC's).
 *
 * The PC build gives the game memory at the PS2's own addresses. A Vita program cannot choose where its memory
 * is, and it does not have to: the game takes its heap with one malloc and keeps ordinary 32-bit pointers, which
 * is what the Vita has. So here
 *   - the game heap is one block from the system (28.3 MB, the size of the PS2's region), wherever it lands;
 *   - the hardware registers are two arrays (the game's register macros go through PORT_HW, port_compat.h);
 *   - the data tables of the game's two programs are filled in from the user's own files at start
 *     (Vita_LoadGameData), through the table of addresses build.py generates (vita_dat.c).
 * Every address in a Vita program has its top bit set (0x81000000 and up). The PS2's never do, and nothing in the
 * PC builds has ever run that way (they keep everything below 2 GB): game code that treats a pointer as a signed
 * number or keeps a flag in its top bit would show here first. heap=cdram (bt3.txt) asks for the heap in the
 * graphics memory area instead, which lies lower; an experiment for that case, not a fix.
 */
#include <malloc.h>
#include "vita_paths.h"
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vita_dat.h"

#define HEAP_SIZE (0x02000000u - 0x003BE000u)
#define HEAP_SKIP 0x730u /* the PS2's first address is 0x3BE730: keep its position within a page */

unsigned char gPortHw[0x10000] __attribute__((aligned(16)));   /* stands for 0x10000000..0x1000FFFF */
unsigned char gPortGsReg[0x2000] __attribute__((aligned(16))); /* stands for 0x12000000..0x12001FFF */
volatile uint32_t gPortDataStripped = 1;                       /* the tables come from the disc, always */
uint32_t gVitaHeapBase, gVitaHeapUsed;

static uint32_t sHeapNext, sHeapEnd;
extern void Vita_Fatal(const char *fmt, ...);

void Vita_MapMemory(int cdram) {
    uint32_t size = (HEAP_SIZE + 0x3FFFFu) & ~0x3FFFFu; /* graphics memory comes in 256 KB steps */
    SceUID id = sceKernelAllocMemBlock("bt3_heap", cdram ? SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW : SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                       size, NULL);
    void *base = NULL;

    if (id < 0 || sceKernelGetMemBlockBase(id, &base) < 0 || base == NULL) {
        Vita_Fatal("no memory for the game heap (%u MB, %s): error 0x%08X", size >> 20, cdram ? "graphics memory" : "main memory", (unsigned)id);
    }
    memset(base, 0, size);
    gVitaHeapBase = (uint32_t)(uintptr_t)base;
    sHeapNext = gVitaHeapBase + HEAP_SKIP;
    sHeapEnd = gVitaHeapBase + HEAP_SIZE;
}

/* The game's malloc (port_compat.h renames it): a bump allocator over the heap block, 16-byte aligned. */
void *Port_Malloc(uint32_t size) {
    uint32_t p = (sHeapNext + 15) & ~15u;

    if (size > sHeapEnd - p) {
        Vita_Fatal("malloc(0x%X) does not fit in the game heap", size);
    }
    sHeapNext = p + size;
    gVitaHeapUsed = sHeapNext - gVitaHeapBase;
    return (void *)(uintptr_t)p;
}

/* Where the game heap is (port/vita/vita_session.c: part of the game's state) */
void Vita_HeapRegion(uint8_t **base, uint32_t *size) {
    *base = (uint8_t *)(uintptr_t)gVitaHeapBase;
    *size = HEAP_SIZE;
}

#ifdef BT4_VITA_NETLINK
/* (online sessions inside the running game, vita_session.c: the blocks the game takes outside its heap are kept in
   a list, part of every snapshot of the game's state, and one a snapshot holds is not given back) */
extern void *Vita_BlockAlloc(uint32_t size, int kind);
extern int Vita_BlockFree(void *p);
#endif

/* Memory the port hands to the game by address (file handles, the "second processor's" memory): zeroed. */
void *Port_LowAlloc(size_t size) {
#ifdef BT4_VITA_NETLINK
    void *p = Vita_BlockAlloc((uint32_t)size, 1);
#else
    void *p = calloc(1, size);
#endif

    if (p == NULL) {
        Vita_Fatal("out of memory (%u bytes)", (unsigned)size);
    }
    return p;
}

void Port_LowFree(void *addr) {
#ifdef BT4_VITA_NETLINK
    if (Vita_BlockFree(addr)) {
        return;
    }
#endif
    free(addr);
}

static uint8_t *read_file(const char *path, size_t *size) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    uint8_t *buf;
    SceOff n;

    if (fd < 0) {
        return NULL;
    }
    n = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    buf = n > 0 && n < 0x4000000 ? malloc((size_t)n) : NULL;
    if (buf == NULL || sceIoRead(fd, buf, (SceSize)n) != (int)n) {
        sceIoClose(fd);
        free(buf);
        return NULL;
    }
    sceIoClose(fd);
    *size = (size_t)n;
    return buf;
}

#ifdef PORT_VITA_BT4
#include "bt4_online_compat.h"
/* Which build of BT4 the disc is. Team BT4's B14 Rev2 only, English or Spanish: its three programs' sizes and CRC-32
   per language are BT4decomp's (bt4_online_compat.h, [0] Spanish, [1] English). Another build (B13, an older
   revision) has other programs and other archives, and stopped later on a file it does not have ("archive not
   found"); now the start says what was found. A size that is not one of B14 Rev2's stops the start; a CRC-32 that is
   not is said in the log only. */
static uint32_t crc32_of(const uint8_t *p, size_t n) {
    uint32_t table[256], c = ~0u, k, i;
    for (i = 0; i < 256; i++) {
        uint32_t v = i;
        for (k = 0; k < 8; k++) {
            v = (v >> 1) ^ ((v & 1) ? 0xEDB88320u : 0);
        }
        table[i] = v;
    }
    while (n-- > 0) {
        c = table[(c ^ *p++) & 255] ^ (c >> 8);
    }
    return ~c;
}

static void bt4_build_check(int file, const char *disc, const char *name, const uint8_t *data, size_t size, int english) {
    extern void Vita_Log(const char *fmt, ...);
    extern void Vita_DiscSay(char *out, int size);
    const Bt4BaseFile *f = &kBt4BaseFiles[file];
    uint32_t crc;

    if (size != f->size[0] && size != f->size[1]) {
        char where[300];
        Vita_DiscSay(where, sizeof(where));
        Vita_Log("vita: %s/%s is %u bytes: not Team BT4's B14 Rev2 (%u English, %u Spanish)\n", disc, name, (unsigned)size,
                 f->size[1], f->size[0]);
        Vita_Fatal("This is not Team BT4's B14 Rev2. Found:\n  %s\n  %s of %u bytes (B14 Rev2's: %u English,\n  %u Spanish).\n\n"
                   "This port needs Team BT4's B14 Rev2 (English or Spanish).\n"
                   "Other builds (B13, older revisions) are not supported.",
                   where, name, (unsigned)size, f->size[1], f->size[0]);
    }
    crc = crc32_of(data, size);
    if (crc != f->crc[english]) {
        Vita_Log("vita: %s/%s: CRC-32 %08x, not B14 Rev2 %s's %08x (another build, or a changed file)\n", disc, name, (unsigned)crc,
                 english ? "English" : "Spanish", f->crc[english]);
    } else {
        Vita_Log("vita: %s: B14 Rev2 %s's (CRC-32 %08x)\n", name, english ? "English" : "Spanish", (unsigned)crc);
    }
}
#endif

/* Copies the values of the game's data tables in from <disc>/SLUS_216.78 (its loaded sections as one image from
   0x100000) and <disc>/BIN/DBZP.BIN, and checks them against the checksum of port/data: the same step as
   Port_LoadGameData of the PC build, with the destinations as addresses the Vita's loader has already placed. */
#define ROM_BASE 0x100000u
#define ROM_END 0x2FF180u

void Vita_LoadGameData(const char *disc) {
    char path[300];
    uint8_t *elf, *src[2] = {NULL, NULL};
    size_t elfSize = 0, srcSize[2] = {0, 0};
    uint64_t hash = 0xCBF29CE484222325ull;
    uint32_t i, k;

    snprintf(path, sizeof(path), "%s/" PORT_VITA_ELF, disc);
    elf = read_file(path, &elfSize);
    if (elf == NULL || elfSize < 0x34 || memcmp(elf, "\177ELF", 4) != 0) {
        Vita_Fatal("the game's program was not found:\n  %s\nCopy the files of your own disc there (see the README).", path);
    }
#ifdef PORT_VITA_BT4
    {   /* which release of BT4 B14 Rev2: the Spanish one's program is 8 bytes shorter (bt4_online_compat.h); the game's
           texts that depend on it (Port_GameProfileEnglish, vita_bt4.c) follow. Another build stops here. */
        extern int gVitaBt4English;
        extern void Vita_Log(const char *fmt, ...);
        gVitaBt4English = elfSize == kBt4BaseFiles[0].size[1];
        bt4_build_check(0, disc, PORT_VITA_ELF, elf, elfSize, gVitaBt4English);
        Vita_Log("vita: BT4 B14 Rev2, the %s release (" PORT_VITA_ELF " of %u bytes)\n", gVitaBt4English ? "English" : "Spanish",
                 (unsigned)elfSize);
        snprintf(path, sizeof(path), "%s/BIN/DBZ4.BIN", disc);
        {
            size_t n = 0;
            uint8_t *idx = read_file(path, &n);
            if (idx == NULL) {
                Vita_Fatal("BT4's archive index was not found:\n  %s\n\nThis port needs Team BT4's B14 Rev2 (English or Spanish).\n"
                           "Other builds (B13, older revisions) are not supported.", path);
            }
            bt4_build_check(1, disc, "BIN/DBZ4.BIN", idx, n, gVitaBt4English);
            free(idx);
        }
    }
#endif
    {
        uint32_t shoff, sh[6];
        uint16_t shentsize, shnum;
        memcpy(&shoff, elf + 0x20, 4);
        memcpy(&shentsize, elf + 0x2E, 2);
        memcpy(&shnum, elf + 0x30, 2);
        srcSize[0] = ROM_END - ROM_BASE;
        src[0] = calloc(1, srcSize[0]);
        if (src[0] == NULL) {
            Vita_Fatal("out of memory");
        }
        for (i = 0; i < shnum && (size_t)shoff + (size_t)(i + 1) * shentsize <= elfSize; i++) {
            memcpy(sh, elf + shoff + (size_t)i * shentsize, sizeof(sh)); /* name, type, flags, addr, offset, size */
            if ((sh[2] & 2) && sh[1] != 8 && sh[5] != 0 && sh[3] >= ROM_BASE && sh[3] + sh[5] <= ROM_END &&
                (size_t)sh[4] + sh[5] <= elfSize) {
                memcpy(src[0] + (sh[3] - ROM_BASE), elf + sh[4], sh[5]);
            }
        }
        free(elf);
    }
    snprintf(path, sizeof(path), "%s/BIN/DBZP.BIN", disc);
    src[1] = read_file(path, &srcSize[1]);
    if (src[1] == NULL) {
        Vita_Fatal("the game's menu program was not found:\n  %s", path);
    }
#ifdef PORT_VITA_BT4
    {
        extern int gVitaBt4English;
        bt4_build_check(2, disc, "BIN/DBZP.BIN", src[1], srcSize[1], gVitaBt4English);
    }
#endif
    for (i = 0; i < gPortDatCount; i++) {
        const PortDatRun *r = &gPortDatRuns[i];
        const uint8_t *from;

        if (r->source > 1 || (size_t)r->offset + r->length > srcSize[r->source]) {
            Vita_Fatal("the list of the game's data does not fit the files in\n  %s", disc);
        }
        from = src[r->source] + r->offset;
        memcpy(r->dst, from, r->length);
        for (k = 0; k < r->length; k++) {
            hash = (hash ^ from[k]) * 0x100000001B3ull;
        }
    }
    free(src[0]);
    free(src[1]);
    if (hash != gPortDatChecksum) {
#ifdef PORT_VITA_BT4
        /* (BT4decomp: the tables' sum is the release's of one language; the English and the Spanish B14 Rev2 discs
           differ in them, and BT4decomp's PC build too accepts both, after checking the programs another way) */
        extern void Vita_Log(const char *fmt, ...);
        Vita_Log("vita: the data tables' sum is %016llx, not the list's %016llx (the other language's BT4 disc, or another one)\n",
                 (unsigned long long)hash, (unsigned long long)gPortDatChecksum);
#else
        Vita_Fatal("SLUS_216.78 / BIN/DBZP.BIN in\n  %s\nare not the unmodified USA release (SLUS-21678).", disc);
#endif
    }
}

/* (BT4decomp) Blocks the game takes outside its heap (Port_GameBigAlloc): each fighter's tables BT4 keeps once
   per fighter (src/battle/bobj_a.c), a Tag Team's files and pool. From the C library's heap (_newlib_heap_size_user),
   each with a header in front saying it is one, so that Heap_Free (src/sys/heap.c), which asks Port_GameBigFree
   first about every block it frees, gives it back here; an address inside the game heap is the heap's at once. */
#define BIG_MAGIC 0xB16B7A4Cu
unsigned Port_GameBigAlloc(int size) {
#ifdef BT4_VITA_NETLINK
    void *b = Vita_BlockAlloc((uint32_t)(size > 0 ? size : 0), 2);
    if (b == NULL) {
        Vita_Fatal("out of memory: %d bytes outside the game heap", size);
    }
    return (unsigned)(uintptr_t)b;
#else
    uint8_t *p = memalign(64, (size_t)(size > 0 ? size : 0) + 64);
    if (p == NULL) {
        Vita_Fatal("out of memory: %d bytes outside the game heap", size);
    }
    ((uint32_t *)p)[0] = BIG_MAGIC;
    ((uint32_t *)p)[1] = (uint32_t)(uintptr_t)p ^ BIG_MAGIC;
    return (unsigned)(uintptr_t)(p + 64);
#endif
}
int Port_GameBigFree(unsigned addr) {
    uint32_t *h;
    if (addr == 0 || (addr >= gVitaHeapBase && addr < gVitaHeapBase + HEAP_SIZE) || addr < 64) {
        return 0;
    }
#ifndef BT4_VITA_NETLINK
    h = (uint32_t *)(uintptr_t)(addr - 64);
    if (h[0] != BIG_MAGIC || h[1] != ((uint32_t)(uintptr_t)h ^ BIG_MAGIC)) {
        return 0;
    }
#else
    (void)h;
    {
        extern int Vita_BlockIs(const void *p);
        if (!Vita_BlockIs((const void *)(uintptr_t)addr)) {
            return 0;
        }
    }
#endif
    {   /* The lists already handed over may still read the block: Tag Team reloads a fighter's model files in the
           fight (btl_obj.c: BtlRes_TagCommitReload frees the old ones), and the list reader, one list behind, then
           followed a model's DMA CALL into the next file loaded at the same place (a list of the 2 vs 2 demo fight,
           about 105 s in: the VU1 interpreter for seconds, or a crash in vif()). On the PS2 the DMA was over long
           before a disc read could finish. Rare (a fighter's files): the wait is for one list at most. */
        extern void Port_GsSync(void);
        extern unsigned gVitaBigFreeSync;
        extern void Vita_Log(const char *fmt, ...);
        Port_GsSync();
        if (gVitaBigFreeSync++ < 64) {
            Vita_Log("vita: a block outside the game heap at %08x given back (%u ms), after the lists handed over\n", addr,
                     (unsigned)(sceKernelGetProcessTimeWide() / 1000));
        }
    }
#ifdef BT4_VITA_NETLINK
    return Vita_BlockFree((void *)(uintptr_t)addr);
#else
    h[0] = 0;
    free(h);
    return 1;
#endif
}
unsigned gVitaBigFreeSync; /* for the log: blocks outside the game heap given back after the lists had been read */

/* Which game the shared code runs (include/port_variant.h): 1, the Budokai Tenkaichi 4 mod, in BT4decomp's build */
#ifdef PORT_VITA_BT4
int gPortVariant = 1;
#else
int gPortVariant = 0;
#endif

/* The program's end address on the PS2: a table of the PS2's C library points at it; nothing reads it here
   (src/sys/heap.c takes the number itself in this build). */
unsigned char D_3BE71C[16];
