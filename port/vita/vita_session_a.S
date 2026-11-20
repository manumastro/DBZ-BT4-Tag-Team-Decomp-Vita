/*
 * The game thread's registers kept and put back, and a call on another stack (port/vita/vita_session.c: an online
 * session inside the running game; port/src/gs/state.c does the same on the PC with setjmp / ucontext).
 *
 *   int  Vita_CtxSave(uint32_t ctx[27])   keeps r4-r11, sp, lr, d8-d15 and the float unit's mode; returns 0, and 1 when
 *                                          Vita_CtxJump comes back to it
 *   void Vita_CtxJump(const uint32_t ctx[27])          continues at that save (does not return)
 *   void Vita_CallOnStack(void (*fn)(void), void *top) runs fn on the stack that ends at top (fn does not return)
 */
    .syntax unified
    .arch armv7-a
    .fpu neon
    .arm
    .text

    .global Vita_CtxSave
    .type Vita_CtxSave, %function
    .align 2
Vita_CtxSave:
    stmia r0!, {r4-r11}
    str sp, [r0], #4
    str lr, [r0], #4
    vstmia r0!, {d8-d15}
    vmrs r1, fpscr
    str r1, [r0]
    mov r0, #0
    bx lr
    .size Vita_CtxSave, . - Vita_CtxSave

    .global Vita_CtxJump
    .type Vita_CtxJump, %function
    .align 2
Vita_CtxJump:
    ldmia r0!, {r4-r11}
    ldr r2, [r0], #4
    ldr lr, [r0], #4
    vldmia r0!, {d8-d15}
    ldr r1, [r0]
    vmsr fpscr, r1
    mov sp, r2
    mov r0, #1
    bx lr
    .size Vita_CtxJump, . - Vita_CtxJump

    .global Vita_CallOnStack
    .type Vita_CallOnStack, %function
    .align 2
Vita_CallOnStack:
    bic r1, r1, #7
    mov sp, r1
    blx r0
1:  b 1b
    .size Vita_CallOnStack, . - Vita_CallOnStack
