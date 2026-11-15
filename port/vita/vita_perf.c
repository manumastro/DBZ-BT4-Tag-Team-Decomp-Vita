/*
 * The processor's own event counters, for the log of the development build (and of the released one with
 * bt3.txt report=1). They need libperf (https://github.com/shoui520/libperf): a kernel plugin that gives retail
 * Vitas access to the Cortex-A9 performance monitor, and its user module, loaded from app0:libperf.suprx when it is
 * there (it is not in the VPK). Each thread of the port counts for itself: cycles, instructions, data and
 * instruction cache misses, mispredicted branches. Without them nothing is counted and nothing changes.
 */
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <stdint.h>
#include <string.h>
#include <psp2/perf.h>

int gVitaPerfOn;
/* bt3.txt zones=1 (the development build): without the plugin, the zones (vita_vgl.c Vita_Z, one drawn list in
   eight) timed by the system's clock instead of the cycle counter: microseconds times 444, so that the log's
   "cycles" read the same. Coarser (a mark costs a system call) but enough to see where a list's time goes. */
int gVitaZoneT;
/* bt3.txt gzones=1 (the development build): the game's thread timed by part, a step at a time (Vita_GZ(n) at the
   end of part n: src/battle/battle.c, src/sys/heap.c's Heap_TagCheck, gs_core.c around snap_make). With libperf the
   thread's own cycle counter (its waits not counted), else the system's clock (waits counted). The report's "game
   thread by part" line, ms a step. */
int gVitaGZOn;
unsigned long long gVitaGZ[40];
unsigned gVitaGZSteps;
static unsigned sGzLast;
int Vita_PmonGet(int thid, unsigned int counter, unsigned int *value);
void Vita_GZ(int z) {
    unsigned now;
    if (!gVitaGZOn) {
        return;
    }
    Vita_PmonGet(0, 31, &now);
    if (z == 0) {
        gVitaGZSteps++; /* (the step's start: what came since the last mark is the end of the step before) */
        gVitaGZ[39] += now - sGzLast;
    } else if (z > 0 && z < 39) {
        gVitaGZ[z] += now - sGzLast;
    }
    sGzLast = now;
}
extern unsigned long long sceKernelGetProcessTimeWide(void);
int Vita_PmonGet(int thid, unsigned int counter, unsigned int *value) {
    if (gVitaPerfOn) {
        return scePerfArmPmonGetCounterValue(thid, counter, value);
    }
    *value = counter == 31 ? (unsigned)(sceKernelGetProcessTimeWide() * 444u) : 0;
    return 0;
}

#ifndef PORT_VITA_RELEASE
#include <psp2/appmgr.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include <taihen.h>
#endif
/* Loads the user module; 0 if the kernel plugin is not there. Called once, before any thread counts. The development
   build first loads the kernel plugin itself when it is in the app's folder (libperf.skprx beside eboot.bin), as
   VitaShell loads its own: no line in tai/config.txt and no restart; it stays loaded until the console restarts (a
   second start finds it there: 0x8002D013). Its result is kept for the log (gVitaPerfK). */
int gVitaPerfK;
int Vita_PerfInit(void) {
    int status = 0;
    SceUID id;
#ifndef PORT_VITA_RELEASE
    {
        char tid[16] = {0}, path[64];
        SceIoStat st;
        if (sceAppMgrAppParamGetString(0, 12, tid, sizeof(tid)) >= 0 && tid[0] != 0) {
            snprintf(path, sizeof(path), "ux0:app/%s/libperf.skprx", tid);
            if (sceIoGetstat(path, &st) >= 0) {
                gVitaPerfK = taiLoadStartKernelModule(path, 0, NULL, 0);
            }
        }
    }
#endif
    id = sceKernelLoadStartModule("app0:libperf.suprx", 0, NULL, 0, NULL, &status);
    gVitaPerfOn = id >= 0 && status == 0;
    return gVitaPerfOn ? 1 : (id < 0 ? id : status);
}

/* The calling thread starts counting (counter 0: data cache refills, 1: instruction cache refills, 2: instructions
   renamed, 3: mispredicted branches; 31: cycles). */
void Vita_PerfThreadStart(void) {
    if (!gVitaPerfOn) {
        return;
    }
    scePerfArmPmonStop(0);
    scePerfArmPmonReset(0);
    scePerfArmPmonSelectEvent(0, 0, 0x03);
    scePerfArmPmonSelectEvent(0, 1, 0x01);
    scePerfArmPmonSelectEvent(0, 2, 0x68);
    scePerfArmPmonSelectEvent(0, 3, 0x10);
    scePerfArmPmonStart(0);
}

/* The calling thread's counters now: cycles, data misses, instruction misses, instructions, mispredictions. */
void Vita_PerfRead(uint32_t v[5]) {
    if (!gVitaPerfOn) {
        memset(v, 0, 5 * sizeof(uint32_t));
        return;
    }
    scePerfArmPmonGetCounterValue(0, 31, &v[0]);
    scePerfArmPmonGetCounterValue(0, 0, &v[1]);
    scePerfArmPmonGetCounterValue(0, 1, &v[2]);
    scePerfArmPmonGetCounterValue(0, 2, &v[3]);
    scePerfArmPmonGetCounterValue(0, 3, &v[4]);
}

/* Adds what was counted since `last` to `sum` (64-bit) and moves `last` on. */
void Vita_PerfAccum(uint32_t last[5], uint64_t sum[5]) {
    uint32_t now[5];
    int i;

    Vita_PerfRead(now);
    for (i = 0; i < 5; i++) {
        sum[i] += (uint32_t)(now[i] - last[i]);
        last[i] = now[i];
    }
}
