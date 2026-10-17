/* an mpeg audio layer iii decoder of mpeg 1, 2 and 2.5, fed through a ring of input as l3codeca's. */
#ifndef L3CODECA_MP3_H
#define L3CODECA_MP3_H

#include <stdint.h>
#include <mime/win32/winbase.h>

typedef struct Mp3HuffSpec {
    const uint32_t *codes;
    uint8_t size, linbits;
} Mp3HuffSpec;

typedef struct Mp3Bands {
    uint16_t l[23], s[14];
} Mp3Bands;

extern const Mp3HuffSpec MP3_HUFF[32];
extern const uint32_t *const MP3_QUAD[2];
extern const int32_t MP3_WINDOW[512];
extern const Mp3Bands MP3_BANDS[9];
extern const float MP3_IMDCT36[18 * 36], MP3_IMDCT12[6 * 12], MP3_WINDOWS[4 * 36], MP3_SYNTH[32 * 32];
extern const float MP3_ALIAS[16], MP3_INTENSITY[14];

#define MP3_RING 8192

enum {
    MP3_OK,
    MP3_NEED_DATA,
    MP3_SEARCHING,
    MP3_SYNC_LOST,
    MP3_FATAL,
};

typedef struct Mp3Channel {
    float overlap[576];
    float last[2][576];  /* the spectra of the last frame, which concealment repeats */
    float v[1024];
} Mp3Channel;

typedef struct Mp3Decoder {
    BYTE ring[MP3_RING + 8];
    DWORD start, end;    /* the bytes held, from start to end */
    DWORD bit;           /* the bit of ring[start] the stream is at */
    DWORD lock;          /* the masked header of the stream, zero before sync */
    DWORD checks;        /* the frames whose successor is still checked */
    BYTE reservoir[6144];  /* 511 bytes kept and a frame, room for corrupt lengths read past them */
    DWORD reserved;
    DWORD v_offset;
    DWORD noise;
    BYTE shift, eight, downmix;
    Mp3Channel ch[2];
} Mp3Decoder;

void Mp3Init(Mp3Decoder *d, int shift, BOOL eight, BOOL downmix);
void Mp3Reset(Mp3Decoder *d);
DWORD Mp3Feed(Mp3Decoder *d, const BYTE *data, DWORD size);
DWORD Mp3Space(const Mp3Decoder *d);
int Mp3Decode(Mp3Decoder *d, BYTE *out, DWORD room, DWORD *written);
void Mp3Setup(void);

#endif
