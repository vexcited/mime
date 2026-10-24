/*
 * the arm64 virtual unwinder, see unwind_arm64.h.
 *
 * a function's unwind codes undo its prolog from the last instruction to the first, so an unwind from mid
 * prolog skips the codes of instructions that have not run, and one from mid epilog skips those that have.
 * packed unwind data encodes canonical frames in one word and is expanded to unwind codes here, so one
 * interpreter serves both.
 */
#include "unwind_arm64.h"

#define CODE_END 0xE4
#define CODE_END_C 0xE5
#define CODE_NOP 0xE3
#define CODE_SET_FP 0xE1
#define CODE_PAC_SIGN_LR 0xFC

/* bytes of unwind codes a packed record expands to, with room to spare. */
#define PACKED_CODE_BYTES 112

typedef struct LAYOUT {
    const uint8_t *codes;
    uint32_t size;
    uint32_t length;
    /* the codes belong to a fragment without prolog or epilog, or to a jump island. */
    int body_only;
    /* the epilog scopes of an unpacked record, or its only epilog that sits at the end of the function. */
    const uint32_t *scopes;
    uint32_t scope_count;
    int single_epilog;
    uint32_t single_index;
    /* the epilog of packed data has codes of its own, the prolog's without the instructions it does not repeat. */
    const uint8_t *epilog_codes;
    uint32_t epilog_size;
    const uint32_t *handler_word;
    uint8_t prolog_buffer[PACKED_CODE_BYTES];
    uint8_t epilog_buffer[PACKED_CODE_BYTES];
} LAYOUT;

/* ------------------------------------------------------------ packed unwind data */

/* an unwind code and whether the epilog repeats the instruction it stands for. */
typedef struct OP {
    uint8_t length;
    uint8_t byte[2];
    uint8_t in_epilog;
} OP;

typedef struct OPS {
    OP op[32];
    unsigned count;
    int failed;
} OPS;

static void add_op(OPS *ops, unsigned length, unsigned b0, unsigned b1, int in_epilog)
{
    if (ops->count == sizeof(ops->op) / sizeof(ops->op[0])) {
        ops->failed = 1;
        return;
    }
    OP *op = &ops->op[ops->count++];
    op->length = (uint8_t)length;
    op->byte[0] = (uint8_t)b0;
    op->byte[1] = (uint8_t)b1;
    op->in_epilog = (uint8_t)in_epilog;
}

static void add_alloc(OPS *ops, uint32_t bytes)
{
    uint32_t units = bytes / 16;
    if (units < 32)
        add_op(ops, 1, units, 0, 1);
    else
        add_op(ops, 2, 0xC0 | (units >> 8), units & 0xFF, 1);
}

/* the allocation of a frame larger than 4080 bytes takes two codes, as the instructions that add cannot reach farther. */
static void add_big_alloc(OPS *ops, uint32_t bytes)
{
    if (bytes > 4080) {
        add_alloc(ops, 4080);
        bytes -= 4080;
    }
    add_alloc(ops, bytes);
}

/* the unwind codes a packed record stands for: the prolog's in the order they are undone, then the epilog's. */
static int expand_packed(uint32_t packed, LAYOUT *l)
{
    uint32_t regf = (packed >> 13) & 7;
    uint32_t regi = (packed >> 16) & 15;
    uint32_t home = (packed >> 20) & 1;
    uint32_t chain = (packed >> 21) & 3;
    uint32_t frame = ((packed >> 23) & 0x1FF) * 16;
    l->length = ((packed >> 2) & 0x7FF) * 4;
    if (regi > 10)
        return 0;

    uint32_t int_size = regi * 8 + (chain == 1 ? 8 : 0);
    uint32_t fp_size = regf ? (regf + 1) * 8 : 0;
    uint32_t save_size = (int_size + fp_size + 64 * home + 15) & ~15u;
    if (save_size > frame)
        return 0;
    uint32_t local_size = frame - save_size;

    OPS ops = {};
    int allocated = 0;
    if (chain == 2)
        add_op(&ops, 1, CODE_PAC_SIGN_LR, 0, 1);

    for (uint32_t pair = 0; pair < regi / 2; pair++) {
        if (pair == 0) {
            add_op(&ops, 1, 0x20 | (save_size / 8), 0, 1);
            allocated = 1;
        } else {
            add_op(&ops, 2, 0xC8 | ((2 * pair) >> 2), (((2 * pair) & 3) << 6) | (2 * pair), 1);
        }
    }
    if (regi & 1) {
        uint32_t reg = regi - 1;
        if (chain == 1) {
            /* a pre-indexed store of a register and lr has no unwind code, so the allocation comes first. */
            if (regi == 1) {
                add_alloc(&ops, save_size);
                allocated = 1;
            }
            add_op(&ops, 2, 0xD6 | ((reg / 2) >> 2), (((reg / 2) & 3) << 6) | reg, 1);
        } else if (regi == 1) {
            add_op(&ops, 2, 0xD4, (save_size / 8 - 1), 1);
            allocated = 1;
        } else {
            add_op(&ops, 2, 0xD0 | (reg >> 2), ((reg & 3) << 6) | reg, 1);
        }
    } else if (chain == 1) {
        /* lr is x19 plus 11 to the save_reg codes. without registers before it the store allocates the area. */
        if (regi == 0) {
            add_op(&ops, 2, 0xD4 | (11 >> 3), ((11 & 7) << 5) | (save_size / 8 - 1), 1);
            allocated = 1;
        } else {
            add_op(&ops, 2, 0xD0 | (11 >> 2), ((11 & 3) << 6) | regi, 1);
        }
    }

    if (regf) {
        uint32_t count = regf + 1;
        for (uint32_t pair = 0; pair < count / 2; pair++) {
            uint32_t reg = 2 * pair;
            if (pair == 0 && !allocated) {
                add_op(&ops, 2, 0xDA | (reg >> 2), ((reg & 3) << 6) | (save_size / 8 - 1), 1);
                allocated = 1;
            } else {
                add_op(&ops, 2, 0xD8 | (reg >> 2), ((reg & 3) << 6) | ((int_size + 16 * pair) / 8), 1);
            }
        }
        if (count & 1) {
            uint32_t reg = count - 1;
            add_op(&ops, 2, 0xDC | (reg >> 2), ((reg & 3) << 6) | ((int_size + 8 * reg) / 8), 1);
        }
    }
    if (!allocated && save_size)
        add_alloc(&ops, save_size);
    if (home) {
        for (int i = 0; i < 4; i++)
            add_op(&ops, 1, CODE_NOP, 0, 0);
    }

    if (chain >= 2) {
        if (local_size < 16)
            return 0;
        if (local_size <= 512) {
            add_op(&ops, 1, 0x80 | (local_size / 8 - 1), 0, 1);
        } else {
            add_big_alloc(&ops, local_size);
            add_op(&ops, 1, 0x40, 0, 1);
        }
        add_op(&ops, 1, CODE_SET_FP, 0, 0);
    } else if (local_size) {
        add_big_alloc(&ops, local_size);
    }
    if (ops.failed)
        return 0;

    /* the codes undo the prolog back to front. */
    uint32_t prolog = 0, epilog = 0;
    for (unsigned i = ops.count; i-- > 0;) {
        const OP *op = &ops.op[i];
        for (unsigned b = 0; b < op->length; b++)
            l->prolog_buffer[prolog++] = op->byte[b];
        if (op->in_epilog) {
            for (unsigned b = 0; b < op->length; b++)
                l->epilog_buffer[epilog++] = op->byte[b];
        }
    }
    l->prolog_buffer[prolog++] = CODE_END;
    l->epilog_buffer[epilog++] = CODE_END;
    l->codes = l->prolog_buffer;
    l->size = prolog;
    l->epilog_codes = l->epilog_buffer;
    l->epilog_size = epilog;
    return 1;
}

/* ------------------------------------------------------------ unwind data */

static int expand_xdata(const uint32_t *x, LAYOUT *l)
{
    uint32_t w0 = x[0];
    if ((w0 >> 18) & 3)
        return 0;
    uint32_t has_handler = (w0 >> 20) & 1;
    uint32_t single = (w0 >> 21) & 1;
    uint32_t epilogs = (w0 >> 22) & 0x1F;
    uint32_t words = (w0 >> 27) & 0x1F;
    uint32_t next = 1;
    if (!(w0 >> 22)) {
        epilogs = x[1] & 0xFFFF;
        words = (x[1] >> 16) & 0xFF;
        next = 2;
    }
    l->length = (w0 & 0x3FFFF) * 4;
    if (single) {
        l->single_epilog = 1;
        l->single_index = epilogs;
    } else {
        l->scopes = x + next;
        l->scope_count = epilogs;
        next += epilogs;
    }
    l->codes = (const uint8_t *)(x + next);
    l->size = words * 4;
    next += words;
    if (has_handler)
        l->handler_word = x + next;
    return 1;
}

static int expand(uint64_t image_base, const A64_FUNCTION *function, LAYOUT *l, int parent)
{
    uint32_t unwind = function->unwind;
    switch (unwind & 3) {
    case 0:
        return expand_xdata((const uint32_t *)(uintptr_t)(image_base + unwind), l);
    case 3:
        /* a jump island unwinds as the body of the function it links to. */
        if (parent)
            return 0;
        if (!expand(image_base, (const A64_FUNCTION *)(uintptr_t)(image_base + (unwind & ~3u)), l, 1))
            return 0;
        l->body_only = 1;
        l->scopes = nullptr;
        l->scope_count = 0;
        l->single_epilog = 0;
        l->handler_word = nullptr;
        return 1;
    default:
        if (!expand_packed(unwind, l))
            return 0;
        l->body_only = (unwind & 3) == 2;
        return 1;
    }
}

uint32_t a64_function_length(uint64_t image_base, const A64_FUNCTION *function)
{
    uint32_t unwind = function->unwind;
    switch (unwind & 3) {
    case 0:
        return (((const uint32_t *)(uintptr_t)(image_base + unwind))[0] & 0x3FFFF) * 4;
    case 3:
        return 0;
    default:
        return ((unwind >> 2) & 0x7FF) * 4;
    }
}

/* the code of a jump island that links to a parent record is a few instructions. */
#define ISLAND_LENGTH 16

const A64_FUNCTION *a64_find_function(uint64_t image_base, const A64_FUNCTION *table, uint32_t count, uint32_t rva)
{
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (table[mid].begin <= rva)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (!lo)
        return nullptr;
    const A64_FUNCTION *found = &table[lo - 1];
    uint32_t length = a64_function_length(image_base, found);
    if (!length)
        length = ISLAND_LENGTH;
    return rva - found->begin < length ? found : nullptr;
}

/* ------------------------------------------------------------ the codes */

static unsigned code_length(uint8_t b)
{
    if (b < 0xC0)
        return 1;
    if (b < 0xE0)
        return 2;
    switch (b) {
    case 0xE0:
        return 4;
    case 0xE2:
        return 2;
    case 0xE7:
        return 3;
    case 0xF8:
        return 2;
    case 0xF9:
        return 3;
    case 0xFA:
        return 4;
    case 0xFB:
        return 5;
    default:
        return 1;
    }
}

/* the number of codes before the one that ends the sequence, which is the instructions of a prolog or epilog. */
static uint32_t count_codes(const uint8_t *codes, uint32_t size, uint32_t index, int *ends_in_ret)
{
    uint32_t count = 0;
    *ends_in_ret = 0;
    while (index < size) {
        uint8_t b = codes[index];
        if (b == CODE_END) {
            *ends_in_ret = 1;
            break;
        }
        if (b == CODE_END_C)
            break;
        index += code_length(b);
        count++;
    }
    return count;
}

enum { KIND_X, KIND_D, KIND_Q };

/* a code that restores registers from the stack. */
typedef struct SAVE {
    uint8_t kind;
    uint8_t reg;
    uint8_t reg2; /* 0xFF for a single register */
    uint8_t next; /* a pair that save_next may follow */
    uint32_t offset;
    uint32_t release; /* bytes the stack pointer rises by after the load, for a pre-indexed store */
} SAVE;

/* decodes the code at p as a register save; returns its length, or 0 for another kind of code. */
static unsigned decode_save(const uint8_t *p, uint32_t room, SAVE *s)
{
    uint8_t b = p[0];
    unsigned length = code_length(b);
    if (length > room)
        return 0;
    uint8_t b1 = length > 1 ? p[1] : 0;
    s->kind = KIND_X;
    s->reg2 = 0xFF;
    s->next = 0;
    s->offset = 0;
    s->release = 0;
    if (b >= 0x20 && b < 0x40) {
        s->reg = 19;
        s->reg2 = 20;
        s->next = 1;
        s->release = (b & 0x1F) * 8;
    } else if (b >= 0x40 && b < 0x80) {
        s->reg = A64_FP;
        s->reg2 = A64_LR;
        s->offset = (b & 0x3F) * 8;
    } else if (b >= 0x80 && b < 0xC0) {
        s->reg = A64_FP;
        s->reg2 = A64_LR;
        s->release = ((b & 0x3F) + 1) * 8;
    } else if (b >= 0xC8 && b < 0xD0) {
        s->reg = 19 + (((b & 3) << 2) | (b1 >> 6));
        s->offset = (b1 & 0x3F) * 8;
        if (b >= 0xCC) {
            s->offset = 0;
            s->release = ((b1 & 0x3F) + 1) * 8;
        }
        s->reg2 = s->reg + 1;
        s->next = 1;
    } else if (b >= 0xD0 && b < 0xD4) {
        s->reg = 19 + (((b & 3) << 2) | (b1 >> 6));
        s->offset = (b1 & 0x3F) * 8;
    } else if (b >= 0xD4 && b < 0xD6) {
        s->reg = 19 + (((b & 1) << 3) | (b1 >> 5));
        s->release = ((b1 & 0x1F) + 1) * 8;
    } else if (b >= 0xD6 && b < 0xD8) {
        s->reg = 19 + 2 * (((b & 1) << 2) | (b1 >> 6));
        s->reg2 = A64_LR;
        s->offset = (b1 & 0x3F) * 8;
    } else if (b >= 0xD8 && b < 0xDC) {
        s->kind = KIND_D;
        s->reg = 8 + (((b & 1) << 2) | (b1 >> 6));
        s->reg2 = s->reg + 1;
        s->next = 1;
        if (b >= 0xDA)
            s->release = ((b1 & 0x3F) + 1) * 8;
        else
            s->offset = (b1 & 0x3F) * 8;
    } else if (b >= 0xDC && b < 0xDE) {
        s->kind = KIND_D;
        s->reg = 8 + (((b & 1) << 2) | (b1 >> 6));
        s->offset = (b1 & 0x3F) * 8;
    } else if (b == 0xDE) {
        s->kind = KIND_D;
        s->reg = 8 + (b1 >> 5);
        s->release = ((b1 & 0x1F) + 1) * 8;
    } else if (b == 0xE7) {
        uint8_t b2 = p[2];
        if (b1 & 0x80)
            return 0;
        unsigned pair = (b1 >> 6) & 1, pre = (b1 >> 5) & 1, type = b2 >> 6;
        if (type > 2)
            return 0;
        s->kind = (uint8_t)type;
        s->reg = b1 & 0x1F;
        if (pair) {
            s->reg2 = s->reg + 1;
            s->next = 1;
        }
        /* as in the other pre-indexed codes, the amount released is one unit more than the field. */
        if (pre)
            s->release = ((b2 & 0x3F) + 1) * 16;
        else
            s->offset = (b2 & 0x3F) * ((pair || type == KIND_Q) ? 16 : 8);
    } else {
        return 0;
    }
    return length;
}

typedef struct UNWINDER {
    A64_CONTEXT *c;
    A64_POINTERS *p;
    uint64_t low, high;
    int fault;
    int clear_flag;
} UNWINDER;

static uint64_t load(UNWINDER *u, uint64_t address)
{
    uint64_t value = 0;
    if (address < u->low || u->high < 8 || address > u->high - 8) {
        u->fault = 1;
        return 0;
    }
    __builtin_memcpy(&value, (const void *)(uintptr_t)address, 8);
    return value;
}

static void restore_register(UNWINDER *u, unsigned kind, unsigned reg, uint64_t address)
{
    if (kind == KIND_X) {
        if (reg > A64_LR) {
            u->fault = 1;
            return;
        }
        u->c->x[reg] = load(u, address);
        if (u->p && reg >= 19)
            u->p->x[reg - 19] = (uint64_t *)(uintptr_t)address;
        return;
    }
    if (reg > 31) {
        u->fault = 1;
        return;
    }
    u->c->v[reg].low = load(u, address);
    if (kind == KIND_Q)
        u->c->v[reg].high = load(u, address + 8);
    if (u->p && reg >= 8 && reg < 16)
        u->p->d[reg - 8] = (uint64_t *)(uintptr_t)address;
}

static void apply_save(UNWINDER *u, const SAVE *s)
{
    uint64_t address = u->c->sp + s->offset;
    unsigned slot = s->kind == KIND_Q ? 16 : 8;
    restore_register(u, s->kind, s->reg, address);
    if (s->reg2 != 0xFF)
        restore_register(u, s->kind, s->reg2, address + slot);
    u->c->sp += s->release;
}

enum { STEP_NEXT, STEP_END, STEP_DONE, STEP_FAIL };

/* runs the code at codes[index], whose length the caller knows. */
static int execute(UNWINDER *u, const uint8_t *codes, uint32_t size, uint32_t index)
{
    A64_CONTEXT *c = u->c;
    uint8_t b = codes[index];
    uint8_t b1 = code_length(b) > 1 ? codes[index + 1] : 0;
    SAVE save;

    if (b < 0x20) {
        c->sp += (uint64_t)(b & 0x1F) * 16;
    } else if (b >= 0xC0 && b < 0xC8) {
        c->sp += ((uint64_t)((b & 7) << 8) | b1) * 16;
    } else if (b == 0xE0) {
        c->sp += (((uint64_t)b1 << 16) | ((uint64_t)codes[index + 2] << 8) | codes[index + 3]) * 16;
    } else if (b == CODE_SET_FP) {
        c->sp = c->x[A64_FP];
    } else if (b == 0xE2) {
        c->sp = c->x[A64_FP] - (uint64_t)b1 * 8;
    } else if (b == CODE_NOP) {
        /* nothing to undo. */
    } else if (b == 0xE6) {
        /* the pair after the one the next code in the list saves, which ran before in the prolog. */
        uint32_t j = index;
        unsigned n = 0;
        while (j < size && codes[j] == 0xE6) {
            j++;
            n++;
        }
        SAVE base;
        if (j >= size || !decode_save(codes + j, size - j, &base) || !base.next)
            return STEP_FAIL;
        save = base;
        save.reg = (uint8_t)(base.reg + 2 * n);
        save.reg2 = (uint8_t)(base.reg2 + 2 * n);
        save.offset = base.offset + n * (base.kind == KIND_Q ? 32 : 16);
        save.release = 0;
        apply_save(u, &save);
    } else if (b == 0xEA) {
        /* the stack holds a context record, which is all there is to restore. */
        uint64_t address = c->sp;
        if (address < u->low || u->high < sizeof(A64_CONTEXT) || address > u->high - sizeof(A64_CONTEXT))
            return STEP_FAIL;
        const A64_CONTEXT *saved = (const A64_CONTEXT *)(uintptr_t)address;
        if (u->p) {
            for (unsigned i = 0; i < 10; i++)
                u->p->x[i] = (uint64_t *)&saved->x[19 + i];
            u->p->x[10] = (uint64_t *)&saved->x[A64_FP];
            u->p->x[11] = (uint64_t *)&saved->x[A64_LR];
            for (unsigned i = 0; i < 8; i++)
                u->p->d[i] = (uint64_t *)&saved->v[8 + i].low;
        }
        *c = *saved;
        return STEP_DONE;
    } else if (b == 0xE9) {
        /* a machine frame holds the stack pointer and the pc to return to. */
        uint64_t sp = load(u, c->sp);
        uint64_t pc = load(u, c->sp + 8);
        c->sp = sp;
        c->pc = pc;
        c->flags &= ~A64_CONTEXT_UNWOUND_TO_CALL;
        return u->fault ? STEP_FAIL : STEP_DONE;
    } else if (b == 0xEC) {
        u->clear_flag = 1;
    } else if (b == CODE_PAC_SIGN_LR) {
        /* the return address was signed: drop the authentication code from the top bits. */
        c->x[A64_LR] &= 0x0000FFFFFFFFFFFFull;
    } else if (decode_save(codes + index, size - index, &save)) {
        apply_save(u, &save);
    } else {
        return STEP_FAIL;
    }
    return u->fault ? STEP_FAIL : STEP_NEXT;
}

/* runs the codes from index on, skipping the first ones, which stand for instructions the pc has not passed. */
static int run(UNWINDER *u, const uint8_t *codes, uint32_t size, uint32_t index, uint32_t skip)
{
    while (index < size) {
        uint8_t b = codes[index];
        unsigned length = code_length(b);
        if (index + length > size)
            return STEP_FAIL;
        if (b == CODE_END)
            return STEP_END;
        if (b == CODE_END_C) {
            /* what follows is the prolog of the function this fragment belongs to, all of which ran. */
            skip = 0;
        } else if (skip) {
            skip--;
        } else {
            int step = execute(u, codes, size, index);
            if (step != STEP_NEXT)
                return step;
        }
        index += length;
    }
    return STEP_END;
}

int a64_virtual_unwind(uint64_t image_base, uint64_t pc, const A64_FUNCTION *function, A64_CONTEXT *context,
                       A64_POINTERS *pointers, A64_UNWIND_RESULT *result, uint64_t low, uint64_t high)
{
    LAYOUT l = {};
    if (!expand(image_base, function, &l, 0))
        return 0;

    const uint8_t *codes = l.codes;
    uint32_t size = l.size;
    uint32_t index = 0, skip = 0;
    uint64_t offset = pc - image_base - function->begin;

    if (!l.body_only && offset < l.length) {
        int ret;
        uint32_t prolog = count_codes(codes, size, 0, &ret);
        if (offset / 4 < prolog) {
            skip = prolog - (uint32_t)(offset / 4);
        } else if (l.epilog_codes) {
            uint32_t instructions = count_codes(l.epilog_codes, l.epilog_size, 0, &ret) + 1;
            if (l.length >= instructions * 4 && offset >= l.length - instructions * 4) {
                codes = l.epilog_codes;
                size = l.epilog_size;
                skip = (uint32_t)((offset - (l.length - instructions * 4)) / 4);
            }
        } else if (l.single_epilog) {
            uint32_t count = count_codes(codes, size, l.single_index, &ret);
            uint32_t instructions = count + (ret ? 1 : 0);
            if (l.single_index < size && l.length >= instructions * 4 && offset >= l.length - instructions * 4) {
                index = l.single_index;
                skip = (uint32_t)((offset - (l.length - instructions * 4)) / 4);
            }
        } else {
            for (uint32_t i = 0; i < l.scope_count; i++) {
                uint32_t scope = l.scopes[i];
                uint64_t start = (uint64_t)(scope & 0x3FFFF) * 4;
                uint32_t first = scope >> 22;
                if (first >= size || offset < start)
                    continue;
                uint32_t count = count_codes(codes, size, first, &ret);
                if (offset < start + (uint64_t)(count + (ret ? 1 : 0)) * 4) {
                    index = first;
                    skip = (uint32_t)((offset - start) / 4);
                    break;
                }
            }
        }
    }

    UNWINDER u = {};
    u.c = context;
    u.p = pointers;
    u.low = low;
    u.high = high;
    int step = run(&u, codes, size, index, skip);
    if (step == STEP_FAIL)
        return 0;

    /* arm64 handlers address their frame from the caller's restored stack pointer. */
    result->establisher = context->sp;
    result->handler = 0;
    result->handler_data = nullptr;
    if (step == STEP_END) {
        context->pc = context->x[A64_LR];
        if (u.clear_flag)
            context->flags &= ~A64_CONTEXT_UNWOUND_TO_CALL;
        else
            context->flags |= A64_CONTEXT_UNWOUND_TO_CALL;
        if (l.handler_word) {
            result->handler = l.handler_word[0];
            result->handler_data = l.handler_word + 1;
        }
    }
    return 1;
}

void a64_unwind_leaf(A64_CONTEXT *context)
{
    context->pc = context->x[A64_LR];
    context->flags |= A64_CONTEXT_UNWOUND_TO_CALL;
}
