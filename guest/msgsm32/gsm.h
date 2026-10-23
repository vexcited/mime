/* GSM 06.10 frames, per direction state, and the Windows 65-byte blocks of two frames. */
#ifndef MSGSM32_GSM_H
#define MSGSM32_GSM_H

#include <mime/win32/winbase.h>

typedef struct GSM_FRAME {
    SHORT larc[8];
    SHORT nc[4], bc[4], mc[4], xmaxc[4];
    SHORT xmc[4][13];
} GSM_FRAME;

typedef struct GSM_ENCODER {
    SHORT dp[120];   /* reconstructed residual of the last 120 samples. */
    SHORT z1;
    LONG l_z2;
    SHORT mp;
    SHORT larpp[8];  /* the last frame's decoded log area ratios. */
    SHORT u[8];
} GSM_ENCODER;

typedef struct GSM_DECODER {
    SHORT drp[160];
    SHORT nrp;
    SHORT larpp[8];
    SHORT msr;
    SHORT v[9];
} GSM_DECODER;

void GsmReset(GSM_ENCODER *encoder, GSM_DECODER *decoder);
void GsmEncode(GSM_ENCODER *g, const SHORT *in, GSM_FRAME *f);
void GsmDecode(GSM_DECODER *g, const GSM_FRAME *f, SHORT *out);
void GsmPack(const GSM_FRAME frames[2], BYTE *out);
void GsmUnpack(const BYTE *in, GSM_FRAME frames[2]);

#endif
