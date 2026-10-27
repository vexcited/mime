/* the bcj2 filter: x86 branch targets split into a main stream and call, jump and range coded streams. */
#include "stream.h"
#ifdef _WIN32
#include <rtl.h>
#else
#include <string.h>
#endif

#define BUFFER_SIZE 65536u
#define TOP_VALUE (1u << 24)

typedef struct BUFFERED {
    CBS_STREAM *Source;
    uint8_t Data[BUFFER_SIZE];
    size_t Position, Length;
    bool Ended;
} BUFFERED;

typedef struct BCJ2 {
    CBS_STREAM Base;
    BUFFERED Input[4];
    uint16_t Probs[258];
    uint32_t Range, Code;
    uint64_t Position, Size;
    uint8_t Previous;
    uint8_t Tail[4];
    unsigned TailPosition, TailLength;
    bool Started;
    int Status;
} BCJ2;

enum { MAIN, CALL, JUMP, RANGE };

/* the next byte of an input, or -1 at its end or after an error, which goes to the status. */
static int next_byte(BCJ2 *z, unsigned which)
{
    BUFFERED *b = &z->Input[which];
    if (b->Position == b->Length) {
        if (b->Ended)
            return -1;
        size_t got = 0;
        int status = b->Source->Read(b->Source, b->Data, BUFFER_SIZE, &got);
        if (status != CBS_OK && z->Status == CBS_OK)
            z->Status = status;
        b->Position = 0;
        b->Length = got;
        if (!got || status != CBS_OK) {
            b->Ended = true;
            if (!got)
                return -1;
        }
    }
    return b->Data[b->Position++];
}

static bool is_branch(uint8_t previous, uint8_t b)
{
    return (b & 0xFE) == 0xE8 || (previous == 0x0F && (b & 0xF0) == 0x80);
}

static unsigned decode_bit(BCJ2 *z, uint16_t *prob)
{
    if (z->Range < TOP_VALUE) {
        int c = next_byte(z, RANGE);
        if (c < 0) {
            if (z->Status == CBS_OK)
                z->Status = CBS_CORRUPT;
            c = 0;
        }
        z->Range <<= 8;
        z->Code = z->Code << 8 | (uint32_t)c;
    }
    uint32_t p = *prob;
    uint32_t bound = (z->Range >> 11) * p;
    if (z->Code < bound) {
        z->Range = bound;
        *prob = (uint16_t)(p + ((2048 - p) >> 5));
        return 0;
    }
    z->Range -= bound;
    z->Code -= bound;
    *prob = (uint16_t)(p - (p >> 5));
    return 1;
}

static int bcj2_read(CBS_STREAM *stream, uint8_t *out, size_t want, size_t *got)
{
    BCJ2 *z = (BCJ2 *)stream;
    size_t n = 0;
    if (!z->Started) {
        z->Started = true;
        for (int i = 0; i < 5; i++) {
            int c = next_byte(z, RANGE);
            if (c < 0) {
                *got = 0;
                return z->Status != CBS_OK ? z->Status : CBS_CORRUPT;
            }
            z->Code = z->Code << 8 | (uint32_t)c;
        }
        z->Range = 0xFFFFFFFFu;
    }
    while (n < want && z->Position < z->Size && z->Status == CBS_OK) {
        if (z->TailPosition < z->TailLength) {
            out[n++] = z->Tail[z->TailPosition++];
            z->Position++;
            continue;
        }
        int c = next_byte(z, MAIN);
        if (c < 0) {
            if (z->Status == CBS_OK)
                z->Status = CBS_CORRUPT;
            break;
        }
        uint8_t b = (uint8_t)c;
        out[n++] = b;
        z->Position++;
        if (!is_branch(z->Previous, b) || z->Position == z->Size) {
            z->Previous = b;
            continue;
        }
        unsigned index = b == 0xE8 ? z->Previous : b == 0xE9 ? 256 : 257;
        if (!decode_bit(z, &z->Probs[index])) {
            z->Previous = b;
            continue;
        }
        uint32_t target = 0;
        for (int i = 0; i < 4; i++) {
            int d = next_byte(z, b == 0xE8 ? CALL : JUMP);
            if (d < 0) {
                if (z->Status == CBS_OK)
                    z->Status = CBS_CORRUPT;
                break;
            }
            target = target << 8 | (uint32_t)d;
        }
        uint32_t relative = target - (uint32_t)(z->Position + 4);
        z->Tail[0] = (uint8_t)relative;
        z->Tail[1] = (uint8_t)(relative >> 8);
        z->Tail[2] = (uint8_t)(relative >> 16);
        z->Tail[3] = (uint8_t)(relative >> 24);
        z->TailPosition = 0;
        z->TailLength = 4;
        z->Previous = z->Tail[3];
    }
    *got = n;
    return z->Status;
}

static void bcj2_destroy(CBS_STREAM *stream)
{
    BCJ2 *z = (BCJ2 *)stream;
    for (int i = 0; i < 4; i++)
        CbsStreamDestroy(z->Input[i].Source);
    CbsFree(z);
}

int CbsStreamBcj2(CBS_STREAM **out, CBS_STREAM *inputs[4], uint64_t out_size)
{
    *out = nullptr;
    BCJ2 *z = CbsAllocZero(sizeof(BCJ2));
    if (!z)
        return CBS_MEMORY;
    z->Base.Read = bcj2_read;
    z->Base.Destroy = bcj2_destroy;
    for (int i = 0; i < 4; i++)
        z->Input[i].Source = inputs[i];
    for (int i = 0; i < 258; i++)
        z->Probs[i] = 1024;
    z->Size = out_size;
    *out = &z->Base;
    return CBS_OK;
}
