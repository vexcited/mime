#include "internal.h"

void jxr_bits_reader(JxrBits *b, const uint8_t *data, size_t size)
{
    jxr_memset(b, 0, sizeof(*b));
    b->data = data;
    b->size = size;
}

void jxr_bits_writer(JxrBits *b, const MimeAllocator *alloc)
{
    jxr_memset(b, 0, sizeof(*b));
    b->enc = true;
    b->alloc = alloc;
}

uint32_t jxr_get(JxrBits *b, int n)
{
    uint32_t v = 0;
    while (n > 0) {
        size_t byte = b->pos >> 3;
        if (byte >= b->size) {
            b->error = true;
            v <<= n;
            break;
        }
        int bit = (int)(b->pos & 7);
        int take = 8 - bit < n ? 8 - bit : n;
        uint32_t chunk = ((uint32_t)b->data[byte] >> (8 - bit - take)) & ((1u << take) - 1);
        v = (v << take) | chunk;
        b->pos += take;
        n -= take;
    }
    return v;
}

static void bits_byte(JxrBits *b, uint8_t v)
{
    if (b->error)
        return;
    if (b->len == b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 4096;
        uint8_t *buf = mime_alloc(b->alloc, cap);
        if (!buf) {
            b->error = true;
            return;
        }
        if (b->len)
            jxr_memcpy(buf, b->buf, b->len);
        mime_free(b->alloc, b->buf);
        b->buf = buf;
        b->cap = cap;
    }
    b->buf[b->len++] = v;
}

void jxr_put(JxrBits *b, uint32_t v, int n)
{
    while (n > 0) {
        int take = n > 16 ? 16 : n;
        n -= take;
        b->acc = (b->acc << take) | ((v >> n) & ((1u << take) - 1));
        b->nacc += take;
        while (b->nacc >= 8) {
            b->nacc -= 8;
            bits_byte(b, (uint8_t)(b->acc >> b->nacc));
        }
        b->acc &= (1u << b->nacc) - 1;
    }
}

void jxr_align(JxrBits *b)
{
    if (b->enc) {
        if (b->nacc)
            jxr_put(b, 0, 8 - b->nacc);
    } else {
        b->pos = (b->pos + 7) & ~(size_t)7;
    }
}
