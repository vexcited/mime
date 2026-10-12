/*
 * mime's x86 and x64 processor: state, decoded instructions and the
 * interface between the portable core and its environment.
 *
 * the core only depends on the c compiler, so the same sources build
 * mimex86.dll for the guest and the differential tests on the host.
 * guest memory is accessed in place: an x86 address is a pointer of the
 * process running the core.
 */
#ifndef MIME_X86_H
#define MIME_X86_H

#include <stddef.h>
#include <stdint.h>

enum {
    X86_RAX, X86_RCX, X86_RDX, X86_RBX, X86_RSP, X86_RBP, X86_RSI, X86_RDI,
    X86_R8, X86_R9, X86_R10, X86_R11, X86_R12, X86_R13, X86_R14, X86_R15,
};

/* eflags bits. */
#define X86_CF 0x0001u
#define X86_PF 0x0004u
#define X86_AF 0x0010u
#define X86_ZF 0x0040u
#define X86_SF 0x0080u
#define X86_TF 0x0100u
#define X86_IF 0x0200u
#define X86_DF 0x0400u
#define X86_OF 0x0800u
#define X86_AC 0x40000u
#define X86_ID 0x200000u
#define X86_ARITH_FLAGS (X86_CF | X86_PF | X86_AF | X86_ZF | X86_SF | X86_OF)

/*
 * lazy flags: the last flag producing operation is recorded and the
 * flags computed only when read. the low bits give the operand size.
 */
enum {
    X86_LF_NONE = 0, /* the arithmetic flags are materialized in Flags */
    X86_LF_ADD = 1,
    X86_LF_SUB = 2,  /* also cmp and neg with a = 0 */
    X86_LF_LOGIC = 3,
    X86_LF_INC = 4,  /* cf from LfCarry */
    X86_LF_DEC = 5,
    X86_LF_ADC = 6,  /* carry in from LfCarry */
    X86_LF_SBB = 7,
};
#define X86_LF(op, size_log2) (((op) << 2) | (size_log2))

typedef union X86Xmm {
    uint8_t b[16];
    int8_t sb[16];
    uint16_t w[16 / 2];
    int16_t sw[16 / 2];
    uint32_t d[4];
    int32_t sd[4];
    uint64_t q[2];
    int64_t sq[2];
    float f[4];
    double fd[2];
} __attribute__((aligned(16))) X86Xmm;

typedef struct X86Cpu X86Cpu;

/* the services the environment gives the core. */
typedef struct X86Env {
    /* the syscall instruction, int 0x2e and sysenter. */
    void (*syscall)(X86Cpu *cpu);
    /* raises a processor exception; does not return. code is a windows status. */
    void (*fault)(X86Cpu *cpu, uint32_t code, uint64_t info0, uint64_t info1);
    /* rdtsc. */
    uint64_t (*timestamp)(X86Cpu *cpu);
    /* the code at [address, address + size) changed: drops translations. */
    void (*code_changed)(X86Cpu *cpu, uint64_t address, uint64_t size);
} X86Env;

struct X86Cpu {
    uint64_t r[16];
    uint64_t rip;
    uint32_t flags;      /* eflags; the arithmetic bits are valid when LfOp is X86_LF_NONE */
    uint32_t lf_op;      /* X86_LF(op, size) */
    uint64_t lf_res;
    uint64_t lf_a;
    uint64_t lf_b;
    uint64_t lf_carry;   /* carry in for inc, dec, adc and sbb */
    uint64_t fs_base;
    uint64_t gs_base;
    X86Xmm xmm[16];
    uint32_t mxcsr;
    uint32_t mode64;     /* 1 in 64 bit code, 0 in 32 bit compatibility code */

    /* x87, kept in double precision. st(i) is fpr[(top + i) & 7]. */
    double fpr[8];
    uint16_t fcw;
    uint16_t fsw;        /* without top */
    uint8_t ftop;
    uint8_t ftag;        /* abridged tag: bit set when the register is valid */
    uint16_t fop;
    uint64_t fpu_ip;
    uint64_t fpu_dp;
    uint64_t mmx_mode;   /* set while mmx instructions own the fp registers */
    uint64_t mm[8];      /* mmx registers, aliasing the fp registers in mmx mode */

    /* the address emulation returns at, see x86_run. */
    uint64_t exit_rip;
    /* start of the instruction being executed, for faults. */
    uint64_t insn_rip;
    const X86Env *env;
    void *user;          /* the environment's per thread data */
    void *blocks;        /* the decoded block cache */
    uint64_t steps;      /* executed instructions, for statistics */
};

/* decoded instruction. */
typedef struct X86Insn {
    uint8_t len;
    uint8_t map;         /* 0 one byte, 1 0f, 2 0f 38, 3 0f 3a */
    uint8_t opcode;
    uint8_t modrm;
    uint8_t has_modrm;
    uint8_t mod;
    uint8_t reg;         /* modrm.reg with rex.r */
    uint8_t rm;          /* modrm.rm with rex.b; a register when mod is 3 */
    uint8_t opsize;      /* 2, 4 or 8 for word sized operations */
    uint8_t addrsize;    /* 4 or 8 */
    uint8_t rep;         /* 0, 0xf2 or 0xf3 */
    uint8_t lock;
    uint8_t p66;         /* operand size prefix seen */
    uint8_t rex;         /* the rex byte, 0 when absent */
    uint8_t seg;         /* 4 fs, 5 gs, 0 otherwise */
    uint8_t mandatory;   /* sse prefix: 0, 0x66, 0xf2 or 0xf3 */
    int8_t base;         /* memory base register, -1 none, -2 rip relative */
    int8_t index;        /* index register, -1 none */
    uint8_t scale;       /* index shift */
    uint8_t valid;
    int32_t disp;
    uint64_t imm;
    uint64_t imm2;
} X86Insn;

/* decodes the instruction at address; returns 0 for undefined encodings. */
int x86_decode(X86Insn *insn, const uint8_t *code, uint64_t address, int mode64);

/* runs x86 code from cpu->rip until it reaches cpu->exit_rip. */
void x86_run(X86Cpu *cpu);

/* executes one instruction, for tests. */
void x86_step(X86Cpu *cpu);

/* resets the state of a fresh thread. */
void x86_init_cpu(X86Cpu *cpu, const X86Env *env, int mode64);

/* the complete eflags value. */
uint32_t x86_get_flags(X86Cpu *cpu);
void x86_set_flags(X86Cpu *cpu, uint32_t flags);

/* drops every decoded block, after code changed. */
void x86_flush_blocks(X86Cpu *cpu);
void x86_free_blocks(X86Cpu *cpu);

/* the block cache allocator, provided by the environment. */
void *x86_alloc(size_t size);
void x86_free(void *p, size_t size);

/* fxsave layout, used by fxsave, fxrstor and thread contexts. */
void x86_fxsave(X86Cpu *cpu, uint8_t *area);
void x86_fxrstor(X86Cpu *cpu, const uint8_t *area);

/* the 80 bit extended format of the x87. */
void x86_double_to_ext(double v, uint8_t out[10]);
double x86_ext_to_double(const uint8_t in[10]);

#endif
