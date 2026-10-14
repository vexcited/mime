/*
 * arm64 definitions shared between c and assembly.
 */
#ifndef MIME_KTRAP_H
#define MIME_KTRAP_H

/* KTRAP_FRAME layout, see ke.h. */
#define KTRAP_X0 0x000
#define KTRAP_X30 0x0F0
#define KTRAP_SP 0x0F8
#define KTRAP_PC 0x100
#define KTRAP_SPSR 0x108
#define KTRAP_ESR 0x110
#define KTRAP_FAR 0x118
#define KTRAP_FLAGS 0x120
#define KTRAP_V 0x130
#define KTRAP_FPCR 0x330
#define KTRAP_FRAME_SIZE 0x340

#define KTRAP_FLAG_FP_SAVED 1

/* callee saved frame pushed by KiSwapContext and KiCallUserMode, the switch frame also keeps fpcr. */
#define KSWITCH_FRAME_SIZE 0xB0
#define KSWITCH_FPCR 0xA0
#define KCALLOUT_FRAME_SIZE 0xB0
#define KCALLOUT_PREVIOUS_SP 0xA0

#define ESR_EC_SVC64 0x15

/* KPCR.Prcb.Number. */
#define KPCR_NUMBER 0x28
#define KPCR_USER_MODE 0x11

#endif
