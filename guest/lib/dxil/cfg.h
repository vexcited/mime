/*
 * structured control flow for spir-v: a dxil function's blocks plus added
 * blocks so every loop has one continue block and one merge, every
 * selection its own merge, and early returns their own copy of the return
 * block when sharing it would cross constructs.
 */
#ifndef MIME_LIB_DXIL_CFG_H
#define MIME_LIB_DXIL_CFG_H

#include "ir.h"

/* values carried by phis of added blocks are numbered from here, past the function's own. */
#define CFG_SYNTH 0x40000000u
#define CFG_CONST 0x60000000u
#define CFG_UNDEF 0x7FFFFFFFu

enum { TERM_BRANCH, TERM_COND, TERM_SWITCH, TERM_RETURN, TERM_UNREACHABLE };
enum { HEADER_NONE, HEADER_SELECTION, HEADER_LOOP };

typedef struct CfgPhi {
    uint32_t result;
    uint32_t type;
    uint32_t count;
    uint32_t *preds;
    uint32_t *values;
} CfgPhi;

typedef struct CfgBlock {
    /* the dxil block this block runs, DX_NONE for added blocks. */
    uint32_t source;
    /* a copy of a return block: its values are defined again. */
    int copy;
    int dead;
    uint8_t term;
    /* branch condition or switch selector. */
    uint32_t cond;
    /* a conditional branch has true then false, a switch default then the cases. */
    uint32_t *succ;
    uint32_t succ_count;
    uint64_t *cases;
    CfgPhi *phis;
    uint32_t phi_count;
    uint8_t header;
    uint32_t merge, cont;
    uint32_t *pred;
    uint32_t pred_count;
    uint32_t idom;
    uint32_t depth;
    /* the innermost loop holding the block. for headers, the enclosing loop and their body's size. */
    uint32_t loop;
    uint32_t parent;
    uint32_t body_size;
} CfgBlock;

typedef struct Cfg {
    BcArena *arena;
    /* temporaries, released as each pass ends. */
    BcArena scratch;
    uint32_t *pool;
    uint32_t pool_capacity;
    const DxModule *module;
    const DxFunction *function;
    CfgBlock *blocks;
    uint32_t count, capacity;
    /* result types of the added phis, indexed by id - CFG_SYNTH. */
    uint32_t *synth_types;
    uint32_t synth_count, synth_capacity;
    uint32_t entry;
    /* write order, every block after its dominator. */
    uint32_t *order;
    uint32_t order_count;
    char error[96];
} Cfg;

/* 0 on success, the reason in cfg->error otherwise. */
int cfg_build(Cfg *cfg, const DxModule *m, const DxFunction *f, BcArena *arena);
/* in the structured graph. */
int cfg_dominates(const Cfg *cfg, uint32_t a, uint32_t b);

#endif
