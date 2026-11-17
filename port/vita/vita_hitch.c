/*
 * bt3.txt hitchlog=<ms> (default 0 = off; the recorder needs no libperf): the picture that stayed on the screen longer
 * than <ms> (a fight's picture put up that long after the one before: 60 = four blanks and more) is explained in the
 * log, one block of lines each, so that a hitch can be told by its cause instead of by its size.
 *
 *  - a thread of its own (bt3_hitch, the list reader's core, above everything else there) looks every 3 ms at every thread the port
 *    watches (the game's, the list reader, the drawing thread, sound, sampler, render, log, strips, reads): its state
 *    (running, ready: wanting a core and not given one, waiting), what it waits on (the object's kind and name, and for a
 *    mutex the thread that holds it), the processor time it has used, and how often something took its core;
 *  - the places that know a long moment note it in a ring (Vita_HitchEv): the drawing thread's waits for the render thread
 *    (by the line that asked), a read of the memory card, vitaGL's memory walk, a part of the game's step (gzones=1), a
 *    render thread's packet, the sound's lock, and the moments the chip's copies are let go and a model generation begins;
 *  - vita_vgl.c pace_cb (the display taking a picture) tells when a picture was late and hands over what its record
 *    knows (when the game gave the list, the reader's and the drawing thread's times, the swap, the chip).
 *
 * The block (from the start of the late picture's way, its tick, to its put-up): the late picture's line, one line for
 * each thread (the time it ran, was ready, waited, and what for), then the noted moments with the time before the put-up.
 * Nothing runs without the setting; a block takes ~1 ms of this thread, which is not one of the game's.
 */
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void Vita_Log(const char *fmt, ...);
extern int Vita_WatchGet(int slot, int *id, const char **name);
extern void Vita_WatchThread(int slot, int id, const char *name);
extern unsigned Vita_UpMs(void);

int gVitaHitch;    /* bt3.txt hitchlog=<ms>; set to 0 until the recorder runs */
int gVitaHitchWant; /* what the file said */
int gVitaHitchMax = 60; /* bt3.txt hitchmax: blocks written at most */

#define NT 10      /* the watched threads (vita_main.c sWatch) */
#define NS 1536    /* samples kept: 4.6 s at 3 ms */
#define NE 1024    /* noted moments kept */
#define NP 4       /* late pictures waiting to be written */
#define WINDOW_MAX 600000ull

typedef struct Samp {
    uint64_t t;
    uint32_t wid[NT], owner[NT], run[NT];
    uint16_t pre[NT], intr[NT];
    uint8_t st[NT], wt[NT];
} Samp;
typedef struct Ev {
    uint64_t t;
    uint32_t dur, arg2;
    uint16_t kind, arg;
} Ev;
typedef struct Pend {
    uint64_t t0, t1;
    char text[760];
} Pend;
typedef struct Obj {
    uint32_t id;
    char kind;
    char name[24];
} Obj;

static Samp *sSamp;
static Ev *sEv;
static Pend sPend[NP];
static volatile unsigned sSampN, sEvN, sPendW, sPendR;
static unsigned sBlocks, sDropped, sLate;
static Obj sObj[48];
static int sObjN;

static const char *const kEvName[] = {
    "?", "the drawing thread waited for the render thread, line", "read of the memory card (KB)", "vitaGL's memory walk (mem_sample)",
    "the game's step, part", "the render thread's packet, kind", "the game waited for the sound's lock", "the sound mixed under its lock",
    "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?",
    "the chip's copies let go (all, 1; fighters only, 2)", "a model generation of the seam begins", "?", "?"};
#define NKIND (int)(sizeof(kEvName) / sizeof(kEvName[0]))

/* what a thread waits on: its name and kind; a mutex's holder is looked at again each time (id in *owner) */
static int obj_get(uint32_t id, uint32_t *owner) {
    int i;
    for (i = 0; i < sObjN; i++) {
        if (sObj[i].id == id) {
            break;
        }
    }
    if (i == sObjN) {
        Obj *o;
        SceKernelMutexInfo mi;
        SceKernelSemaInfo si;
        SceKernelEventFlagInfo ei;
        if (sObjN == (int)(sizeof(sObj) / sizeof(sObj[0]))) {
            sObjN = 0; /* (full: begun again) */
        }
        o = &sObj[sObjN];
        memset(o, 0, sizeof(*o));
        o->id = id;
        memset(&mi, 0, sizeof(mi));
        mi.size = sizeof(mi);
        memset(&si, 0, sizeof(si));
        si.size = sizeof(si);
        memset(&ei, 0, sizeof(ei));
        ei.size = sizeof(ei);
        if (sceKernelGetMutexInfo((SceUID)id, &mi) >= 0) {
            o->kind = 'M';
            snprintf(o->name, sizeof(o->name), "%.23s", mi.name);
        } else if (sceKernelGetSemaInfo((SceUID)id, &si) >= 0) {
            o->kind = 'S';
            snprintf(o->name, sizeof(o->name), "%.23s", si.name);
        } else if (sceKernelGetEventFlagInfo((SceUID)id, &ei) >= 0) {
            o->kind = 'E';
            snprintf(o->name, sizeof(o->name), "%.23s", ei.name);
        } else {
            o->kind = '?';
        }
        i = sObjN++;
    }
    if (sObj[i].kind == 'M' && owner != NULL) {
        SceKernelMutexInfo mi;
        memset(&mi, 0, sizeof(mi));
        mi.size = sizeof(mi);
        if (sceKernelGetMutexInfo((SceUID)id, &mi) >= 0) {
            *owner = (uint32_t)mi.currentOwnerId;
        }
    }
    return i;
}

static void sample(uint64_t now) {
    Samp *s = &sSamp[sSampN % NS];
    int i;
    s->t = now;
    for (i = 0; i < NT; i++) {
        int id;
        const char *nm;
        SceKernelThreadInfo ti;
        s->st[i] = 0;
        s->owner[i] = 0;
        if (!Vita_WatchGet(i, &id, &nm) || id <= 0) {
            continue;
        }
        memset(&ti, 0, sizeof(ti));
        ti.size = sizeof(ti);
        if (sceKernelGetThreadInfo(id, &ti) < 0) {
            continue;
        }
        s->st[i] = (uint8_t)ti.status;
        s->wt[i] = (uint8_t)ti.waitType;
        s->wid[i] = (uint32_t)ti.waitId;
        s->run[i] = (uint32_t)ti.runClocks;
        s->pre[i] = (uint16_t)ti.threadPreemptCount;
        s->intr[i] = (uint16_t)ti.intrPreemptCount;
        if ((ti.status & 8) && ti.waitId != 0xFFFFFFFFu && ti.waitId != 0) {
            obj_get((uint32_t)ti.waitId, &s->owner[i]);
        }
    }
    __sync_synchronize();
    sSampN++;
}

/* an event of the ring noted: start, length, and what (see kEvName) */
void Vita_HitchEv(unsigned kind, unsigned arg, unsigned arg2, unsigned long long t0, unsigned long long dur) {
    Ev *e;
    unsigned k;
    if (gVitaHitch <= 0 || sEv == NULL) {
        return;
    }
    k = __atomic_fetch_add(&sEvN, 1, __ATOMIC_RELAXED) % NE;
    e = &sEv[k];
    e->kind = 0;
    e->t = t0 != 0 ? t0 : sceKernelGetProcessTimeWide();
    e->dur = dur > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)dur;
    e->arg = (uint16_t)arg;
    e->arg2 = arg2;
    __sync_synchronize();
    e->kind = (uint16_t)kind;
}
void Vita_HitchMark(unsigned kind, unsigned arg) {
    Vita_HitchEv(kind, arg, 0, 0, 0);
}

/* the display took a picture late (vita_vgl.c pace_cb): `from` the start of what is looked at, `to` now, `text` the picture's own line */
void Vita_HitchPut(unsigned long long from, unsigned long long to, const char *text) {
    Pend *p;
    if (gVitaHitch <= 0) {
        return;
    }
    if (sPendW - sPendR >= NP) {
        sDropped++;
        return;
    }
    p = &sPend[sPendW % NP];
    if (to - from > WINDOW_MAX) {
        from = to - WINDOW_MAX;
    }
    p->t0 = from;
    p->t1 = to;
    snprintf(p->text, sizeof(p->text), "%s", text);
    __sync_synchronize();
    sPendW++;
}

static const char *owner_name(uint32_t tid, char *buf, size_t n) {
    int i, id;
    const char *nm;
    for (i = 0; i < NT; i++) {
        if (Vita_WatchGet(i, &id, &nm) && id > 0 && (uint32_t)id == tid) {
            return nm;
        }
    }
    snprintf(buf, n, "thread %x", tid);
    return buf;
}

static void thread_line(int slot, unsigned lo, unsigned hi, uint64_t t1) {
    struct { uint32_t wid; uint8_t wt; uint32_t owner; unsigned ms10; } w[6];
    int nw = 0, id, i, other10 = 0;
    const char *nm;
    unsigned k, ms[4] = {0, 0, 0, 0}; /* (unseen, running, ready, waiting) in tenths of a ms */
    uint32_t run0 = 0, run1 = 0;
    unsigned pre0 = 0, pre1 = 0, in0 = 0, in1 = 0;
    int first = 1;
    char line[700], tmp[24];
    int n;
    if (!Vita_WatchGet(slot, &id, &nm) || id <= 0) {
        return;
    }
    memset(w, 0, sizeof(w));
    for (k = lo; k <= hi; k++) {
        const Samp *s = &sSamp[k % NS];
        const Samp *nx = k < hi ? &sSamp[(k + 1) % NS] : NULL;
        uint64_t dt = (nx != NULL ? nx->t : t1) - s->t;
        unsigned d10 = (unsigned)(dt > 20000 ? 20000 : dt) / 100;
        int st = s->st[slot] == 0 ? 0 : (s->st[slot] & 1) ? 1 : (s->st[slot] & 2) ? 2 : (s->st[slot] & 8) ? 3 : 0;
        ms[st] += d10;
        if (st != 0 && first) {
            run0 = s->run[slot];
            pre0 = s->pre[slot];
            in0 = s->intr[slot];
            first = 0;
        }
        if (st != 0) {
            run1 = s->run[slot];
            pre1 = s->pre[slot];
            in1 = s->intr[slot];
        }
        if (st == 3) {
            for (i = 0; i < nw; i++) {
                if (w[i].wid == s->wid[slot] && w[i].wt == s->wt[slot]) {
                    break;
                }
            }
            if (i == nw && nw < 6) {
                w[nw].wid = s->wid[slot];
                w[nw].wt = s->wt[slot];
                nw++;
            }
            if (i < nw) {
                w[i].ms10 += d10;
                if (s->owner[slot] != 0) {
                    w[i].owner = s->owner[slot];
                }
            } else {
                other10 += (int)d10;
            }
        }
    }
    n = snprintf(line, sizeof(line), "vita:   hitch %-11s: CPU %u ms, running %u, ready %u, waiting %u (ms) | put aside %u, by interrupts %u",
                 nm, (unsigned)((uint32_t)(run1 - run0) / 1000), ms[1] / 10, ms[2] / 10, ms[3] / 10, (unsigned)(uint16_t)(pre1 - pre0), (unsigned)(uint16_t)(in1 - in0));
    for (i = 0; i < nw && n < (int)sizeof(line) - 100; i++) {
        if (w[i].ms10 < 50) {
            continue; /* (under 5 ms) */
        }
        if (w[i].wid == 0xFFFFFFFFu || w[i].wid == 0) {
            n += snprintf(line + n, sizeof(line) - (size_t)n, " | %u ms in a wait of type %x without an object (sleep)", w[i].ms10 / 10, w[i].wt);
        } else {
            const Obj *o = NULL;
            int j;
            for (j = 0; j < sObjN; j++) {
                if (sObj[j].id == w[i].wid) {
                    o = &sObj[j];
                }
            }
            n += snprintf(line + n, sizeof(line) - (size_t)n, " | %u ms on %s \"%s\" (%x, type %x)", w[i].ms10 / 10,
                          o != NULL && o->kind == 'M' ? "a mutex" : o != NULL && o->kind == 'S' ? "a semaphore" : o != NULL && o->kind == 'E' ? "an event flag" : "an object",
                          o != NULL ? o->name : "", w[i].wid, w[i].wt);
            if (w[i].owner != 0) {
                n += snprintf(line + n, sizeof(line) - (size_t)n, " held by %s", owner_name(w[i].owner, tmp, sizeof(tmp)));
            }
        }
    }
    (void)other10;
    Vita_Log("%s\n", line);
}

static void dump(const Pend *p) {
    unsigned newest = sSampN, lo, hi, k, ne, i, j;
    unsigned idx[40];
    int found = 0, slot;
    uint64_t t1 = p->t1;
    if (newest == 0) {
        return;
    }
    hi = newest - 1;
    if (sSamp[hi % NS].t < p->t0) {
        sLate++;
        return; /* (the samples are behind: none in the window) */
    }
    lo = hi;
    while (lo > 0 && hi - lo + 1 < NS && sSamp[(lo - 1) % NS].t >= p->t0) {
        lo--;
    }
    sBlocks++;
    Vita_Log("vita: hitch %u at %u ms: %s\n", sBlocks, Vita_UpMs(), p->text);
    for (slot = 0; slot < NT; slot++) {
        thread_line(slot, lo, hi, sSamp[hi % NS].t > t1 ? sSamp[hi % NS].t : t1);
    }
    /* the noted moments of the window, the oldest first (at most 40, the longest kept when there are more) */
    ne = sEvN;
    for (k = ne > NE ? ne - NE : 0; k < ne; k++) {
        const Ev *e = &sEv[k % NE];
        if (e->kind == 0 || e->t < p->t0 || e->t > t1) {
            continue;
        }
        if (found < 40) {
            idx[found++] = k % NE;
        } else {
            unsigned m = 0;
            for (i = 1; i < 40; i++) {
                if (sEv[idx[i]].dur < sEv[idx[m]].dur) {
                    m = i;
                }
            }
            if (e->dur > sEv[idx[m]].dur) {
                idx[m] = k % NE;
            }
        }
    }
    for (i = 1; i < (unsigned)found; i++) { /* (by start) */
        unsigned v = idx[i];
        for (j = i; j > 0 && sEv[idx[j - 1]].t > sEv[v].t; j--) {
            idx[j] = idx[j - 1];
        }
        idx[j] = v;
    }
    if (found > 0) {
        char line[1300];
        int n = snprintf(line, sizeof(line), "vita:   hitch noted (ms before the put-up, length):");
        for (i = 0; i < (unsigned)found && n < (int)sizeof(line) - 160; i++) {
            const Ev *e = &sEv[idx[i]];
            n += snprintf(line + n, sizeof(line) - (size_t)n, " [-%.1f, %.1f ms: %s %u]", (double)(long long)(t1 - e->t) / 1000.0, e->dur / 1000.0,
                          e->kind < NKIND ? kEvName[e->kind] : "?", (unsigned)e->arg);
        }
        Vita_Log("%s\n", line);
    }
}

static int hitch_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    for (;;) {
        sceKernelDelayThread(3000);
        sample(sceKernelGetProcessTimeWide());
        while (sPendR != sPendW) {
            Pend p = sPend[sPendR % NP];
            sPendR++;
            if (sBlocks < (unsigned)gVitaHitchMax) {
                dump(&p);
            }
        }
    }
    return 0;
}

/* (vita_vgl.c, with the sampler's thread) 1: the recorder runs */
int Vita_HitchStart(void) {
    SceUID th;
    if (gVitaHitchWant <= 0 || gVitaHitch > 0) {
        return gVitaHitch > 0;
    }
    sSamp = (Samp *)calloc(NS, sizeof(Samp));
    sEv = (Ev *)calloc(NE, sizeof(Ev));
    if (sSamp == NULL || sEv == NULL) {
        Vita_Log("vita: hitchlog: no memory for the recorder\n");
        return 0;
    }
    /* (the list reader's core, at the highest priority an application has: on the render thread's core it would wait for that
       thread, which spins, exactly in the heavy moments; here it takes ~30 us of the list reader's every 3 ms) */
    th = sceKernelCreateThread("bt3_hitch", hitch_thread, 64, 0x8000, 0, 0x00020000, NULL);
    if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
        Vita_Log("vita: hitchlog: no thread (%08x)\n", (unsigned)th);
        return 0;
    }
    Vita_WatchThread(9, th, "hitch");
    __sync_synchronize();
    gVitaHitch = gVitaHitchWant;
    Vita_Log("vita: hitchlog=%d: a block of lines for each picture on the screen longer than %d ms (at most %d blocks)\n", gVitaHitchWant, gVitaHitchWant, gVitaHitchMax);
    return 1;
}

/* the report's line: blocks written, pictures left out (the recorder was busy), blocks without samples */
void Vita_HitchLog(void) {
    static unsigned lastB, lastD, lastL;
    if (gVitaHitch <= 0 || (sBlocks == lastB && sDropped == lastD && sLate == lastL)) {
        return;
    }
    Vita_Log("vita:   hitchlog: %u blocks so far (%u pictures not written for the recorder was busy, %u without samples)\n", sBlocks, sDropped, sLate);
    lastB = sBlocks;
    lastD = sDropped;
    lastL = sLate;
}
