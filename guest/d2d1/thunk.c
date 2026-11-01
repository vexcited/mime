/*
 * the context interface of the targets that the factory makes. a window, dc, wic or dxgi target answers the device
 * context interfaces with a second vtable inside the object, whose methods of the render target part first move
 * the interface pointer back to the object and then run the method of the target. the offset is the place of
 * ctx_vtbl in D2dRt, which follows the object header and the gdi interface.
 */
#include "d2d_private.h"
#include <stddef.h>

extern const void *const d2d_rt_base[];

#if defined(__x86_64__) || defined(__aarch64__)
#define CTX_OFFSET 40
#else
#define CTX_OFFSET 20
#endif

_Static_assert(offsetof(D2dRt, ctx_vtbl) == CTX_OFFSET, "the thunks move the pointer by this much");

#define STR2(x) #x
#define STR(x) STR2(x)

#if defined(__x86_64__)
#define THUNK(n)                                                                                                       \
    __attribute__((naked)) static void thunk_##n(void)                                                                 \
    {                                                                                                                  \
        __asm__("subq $" STR(CTX_OFFSET) ", %rcx\n\tjmpq *d2d_rt_base+" #n "*8(%rip)");                               \
    }
#elif defined(__aarch64__)
#define THUNK(n)                                                                                                       \
    __attribute__((naked)) static void thunk_##n(void)                                                                 \
    {                                                                                                                  \
        __asm__("sub x0, x0, #" STR(CTX_OFFSET) "\n\tadrp x16, d2d_rt_base+" #n "*8\n\tldr x16, [x16, :lo12:d2d_rt_base+" #n "*8]\n\tbr x16"); \
    }
#else
#define THUNK(n)                                                                                                       \
    __attribute__((naked)) static void thunk_##n(void)                                                                 \
    {                                                                                                                  \
        __asm__("subl $" STR(CTX_OFFSET) ", 4(%esp)\n\tjmpl *_d2d_rt_base+" #n "*4");                                 \
    }
#endif

#define LIST(X)                                                                                                        \
    X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19)       \
    X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31) X(32) X(33) X(34) X(35) X(36) X(37)         \
    X(38) X(39) X(40) X(41) X(42) X(43) X(44) X(45) X(46) X(47) X(48) X(49) X(50) X(51) X(52) X(53) X(54) X(55) X(56)

LIST(THUNK)

#define ADDRESS(n) (void *)thunk_##n,
const void *const d2d_rt_thunks[57] = {LIST(ADDRESS)};
