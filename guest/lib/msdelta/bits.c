/* the bit streams, the rift tables and the allocation of the msdelta library. */
#include "internal.h"

#ifdef _WIN32
#include <mime/win32/winbase.h>
#else
#include <stdlib.h>
#endif

void *MdAlloc(size_t size)
{
#ifdef _WIN32
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
#else
    return malloc(size ? size : 1);
#endif
}

void *MdAllocZero(size_t size)
{
#ifdef _WIN32
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
#else
    return calloc(1, size ? size : 1);
#endif
}

void *MdRealloc(void *memory, size_t size)
{
#ifdef _WIN32
    if (!memory)
        return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    return HeapReAlloc(GetProcessHeap(), 0, memory, size ? size : 1);
#else
    return realloc(memory, size ? size : 1);
#endif
}

void MsDeltaFree(void *memory)
{
    if (!memory)
        return;
#ifdef _WIN32
    HeapFree(GetProcessHeap(), 0, memory);
#else
    free(memory);
#endif
}

/* ------------------------------------------------------------------ bit streams */

bool MdBitsOpen(MD_BITS *bits, const uint8_t *data, size_t size)
{
    bits->Failed = true;
    if (!size)
        return false;
    unsigned padding = data[0] & 7;
    if (size == 1 && padding > 5)
        return false;
    bits->Data = data;
    bits->Pos = 3;
    bits->End = (uint64_t)size * 8 - padding;
    bits->Failed = false;
    return true;
}

uint64_t MdBitsLeft(const MD_BITS *bits)
{
    return bits->Pos < bits->End ? bits->End - bits->Pos : 0;
}

uint64_t MdBitsRead(MD_BITS *bits, unsigned count)
{
    if (count > 64 || count > MdBitsLeft(bits)) {
        bits->Failed = true;
        return 0;
    }
    uint64_t value = 0;
    unsigned done = 0;
    while (done < count) {
        unsigned shift = (unsigned)(bits->Pos & 7), take = 8 - shift;
        if (take > count - done)
            take = count - done;
        uint64_t part = (bits->Data[bits->Pos >> 3] >> shift) & ((1u << take) - 1);
        value |= part << done;
        done += take;
        bits->Pos += take;
    }
    return value;
}

uint64_t MdBitsReadNumber(MD_BITS *bits)
{
    unsigned zeros = 0;
    while (!MdBitsRead(bits, 1)) {
        if (bits->Failed || ++zeros >= 16) {
            bits->Failed = true;
            return 0;
        }
    }
    if (zeros >= 8) {
        uint64_t low = MdBitsRead(bits, 32);
        uint64_t high = MdBitsRead(bits, 4 * zeros - 28);
        return low | high << 32;
    }
    return MdBitsRead(bits, 4 * zeros + 4);
}

const uint8_t *MdBitsReadBuffer(MD_BITS *bits, size_t *size)
{
    uint64_t length = MdBitsReadNumber(bits);
    if (bits->Failed)
        return nullptr;
    bits->Pos = (bits->Pos + 7) & ~(uint64_t)7;
    uint64_t start = bits->Pos >> 3;
    if (length > (bits->End >> 3) || start > (bits->End >> 3) - length) {
        bits->Failed = true;
        return nullptr;
    }
    bits->Pos += length * 8;
    *size = (size_t)length;
    return bits->Data + start;
}

/* ------------------------------------------------------------------ rift tables */

void MdRiftFree(MD_RIFT *rift)
{
    MsDeltaFree(rift->Entries);
    memset(rift, 0, sizeof(*rift));
}

bool MdRiftAdd(MD_RIFT *rift, int64_t left, int64_t right)
{
    if (rift->Count == rift->Capacity) {
        size_t capacity = rift->Capacity ? rift->Capacity * 2 : 128;
        MD_RIFT_ENTRY *entries = MdRealloc(rift->Entries, capacity * sizeof(*entries));
        if (!entries)
            return false;
        rift->Entries = entries;
        rift->Capacity = capacity;
    }
    rift->Entries[rift->Count++] = (MD_RIFT_ENTRY){left, right};
    return true;
}

bool MdRiftCopy(MD_RIFT *to, const MD_RIFT *from)
{
    memset(to, 0, sizeof(*to));
    if (!from->Count)
        return true;
    to->Entries = MdAlloc(from->Count * sizeof(*to->Entries));
    if (!to->Entries)
        return false;
    memcpy(to->Entries, from->Entries, from->Count * sizeof(*to->Entries));
    to->Count = to->Capacity = from->Count;
    return true;
}

/* a stable sort on the left value. a merge sort, the tables are short. */
static void sort_range(MD_RIFT_ENTRY *a, MD_RIFT_ENTRY *scratch, size_t n)
{
    if (n < 2)
        return;
    size_t half = n / 2;
    sort_range(a, scratch, half);
    sort_range(a + half, scratch, n - half);
    size_t i = 0, j = half, k = 0;
    while (i < half && j < n)
        scratch[k++] = a[j].Left < a[i].Left ? a[j++] : a[i++];
    while (i < half)
        scratch[k++] = a[i++];
    while (j < n)
        scratch[k++] = a[j++];
    memcpy(a, scratch, n * sizeof(*a));
}

void MdRiftSort(MD_RIFT *rift)
{
    bool sorted = true;
    for (size_t i = 1; i < rift->Count && sorted; i++)
        sorted = rift->Entries[i - 1].Left <= rift->Entries[i].Left;
    if (sorted)
        return;
    MD_RIFT_ENTRY *scratch = MdAlloc(rift->Count * sizeof(*scratch));
    if (!scratch)
        return;
    sort_range(rift->Entries, scratch, rift->Count);
    MsDeltaFree(scratch);
}

/* the stretch that holds a position: the last one that starts at or before it, the last one for a position before all. */
static size_t stretch_of(const MD_RIFT *rift, int64_t position)
{
    if (position < rift->Entries[0].Left)
        return rift->Count - 1;
    size_t low = 0, high = rift->Count - 1;
    while (low < high) {
        size_t middle = (low + high + 1) >> 1;
        if (rift->Entries[middle].Left <= position)
            low = middle;
        else
            high = middle - 1;
    }
    return low;
}

int64_t MdRiftMap(const MD_RIFT *rift, int64_t position)
{
    if (!rift->Count)
        return position;
    const MD_RIFT_ENTRY *e = &rift->Entries[stretch_of(rift, position)];
    return (int64_t)((uint64_t)position + (uint64_t)e->Right - (uint64_t)e->Left);
}

int64_t MdRiftGetRift(const MD_RIFT *rift, int64_t position, int64_t *last)
{
    if (!rift->Count) {
        *last = INT64_MAX;
        return 0;
    }
    size_t k = stretch_of(rift, position);
    if (position < rift->Entries[0].Left)
        *last = rift->Entries[0].Left - 1;
    else
        *last = k + 1 < rift->Count ? rift->Entries[k + 1].Left - 1 : INT64_MAX;
    return (int64_t)((uint64_t)rift->Entries[k].Right - (uint64_t)rift->Entries[k].Left);
}

/* the table that undoes a table: where the positions went, from where they came. the line of positions is a circle. */
bool MdRiftReverse(MD_RIFT *out, const MD_RIFT *rift, int64_t start)
{
    memset(out, 0, sizeof(*out));
    size_t n = rift->Count;
    if (!n)
        return true;
    const MD_RIFT_ENTRY *e = rift->Entries;
    size_t same = 1;
    while (same < n && e[same].Left == e[0].Left)
        same++;
    if (same >= n)
        return MdRiftAdd(out, start, (int64_t)((uint64_t)start + (uint64_t)e[0].Right - (uint64_t)e[0].Left));
    /* the largest unsigned value that stands for the end of the line is also the smallest signed one. */
    const int64_t infinity = INT64_MIN;
    typedef struct {
        int64_t Low, High;
    } SPAN;
    SPAN *spans = MdAlloc(2 * n * sizeof(*spans));
    if (!spans)
        return false;
    size_t spans_count = 0;
    size_t next = 0;
    while (next < n && e[next].Left <= start)
        next++;
    if (next == 0)
        next = n;
    size_t current = next - 1, first = current;
    if (next >= n)
        next = 0;
    int64_t position = start;
    bool ok = true;
    for (unsigned long guard = 0; ok && guard < 100000000ul; guard++) {
        int64_t shift = (int64_t)((uint64_t)e[current].Right - (uint64_t)e[current].Left);
        uint64_t length = (uint64_t)e[next].Left - (uint64_t)position;
        int64_t y = (int64_t)((uint64_t)shift + (uint64_t)position);
        if (y != infinity && length > (uint64_t)INT64_MIN - (uint64_t)y)
            length = (uint64_t)INT64_MIN - (uint64_t)y;
        if (length) {
            int64_t end = (int64_t)((uint64_t)y + length);
            size_t low = 0;
            while (low < spans_count) {
                if (spans[low].High == infinity || spans[low].High >= y)
                    break;
                low++;
            }
            size_t high = low;
            while (high < spans_count) {
                if (end != infinity && spans[high].Low > end)
                    break;
                high++;
            }
            if (low == high) {
                ok = MdRiftAdd(out, y, (int64_t)((uint64_t)y - (uint64_t)shift));
                for (size_t k = spans_count; k > low; k--)
                    spans[k] = spans[k - 1];
                spans_count++;
                spans[low] = (SPAN){y, end};
            } else {
                if (y < spans[low].Low)
                    ok = MdRiftAdd(out, y, (int64_t)((uint64_t)y - (uint64_t)shift));
                else
                    y = spans[low].Low;
                for (size_t k = low; ok && k + 1 < high; k++)
                    ok = MdRiftAdd(out, spans[k].High, (int64_t)((uint64_t)spans[k].High - (uint64_t)shift));
                int64_t top = spans[high - 1].High;
                if (top != infinity && (top < end || end == infinity))
                    ok = ok && MdRiftAdd(out, top, (int64_t)((uint64_t)top - (uint64_t)shift));
                else
                    end = top;
                if (low + 1 != high && high < spans_count)
                    for (size_t k = high; k < spans_count; k++)
                        spans[k - (high - low - 1)] = spans[k];
                spans_count -= high - low - 1;
                spans[low] = (SPAN){y, end};
            }
        }
        position = (int64_t)((uint64_t)position + length);
        if (position == e[next].Left) {
            current = next;
            if (first == next)
                break;
            next = next + 1 < n ? next + 1 : 0;
        }
    }
    MsDeltaFree(spans);
    if (!ok) {
        MdRiftFree(out);
        return false;
    }
    MdRiftSort(out);
    return true;
}

bool MdRiftSum(MD_RIFT *out, const MD_RIFT *a, const MD_RIFT *b)
{
    memset(out, 0, sizeof(*out));
    if (!a->Count)
        return MdRiftCopy(out, b);
    if (!b->Count)
        return MdRiftCopy(out, a);
    out->Entries = MdAlloc((a->Count + b->Count) * sizeof(*out->Entries));
    if (!out->Entries)
        return false;
    memcpy(out->Entries, a->Entries, a->Count * sizeof(*out->Entries));
    memcpy(out->Entries + a->Count, b->Entries, b->Count * sizeof(*out->Entries));
    out->Count = out->Capacity = a->Count + b->Count;
    MdRiftSort(out);
    return true;
}

/* adds the entries that carry the positions x..last of the first table, which start at y in the second, through it. */
static bool carry(MD_RIFT *out, const MD_RIFT *b, uint64_t x, uint64_t last, uint64_t y)
{
    for (;;) {
        int64_t limit;
        uint64_t shift = (uint64_t)MdRiftGetRift(b, (int64_t)y, &limit);
        if (!MdRiftAdd(out, (int64_t)x, (int64_t)(shift + y)))
            return false;
        if (last - x <= (uint64_t)limit - y)
            return true;
        x += (uint64_t)limit - y + 1;
        y = (uint64_t)limit + 1;
    }
}

bool MdRiftMultiply(MD_RIFT *out, const MD_RIFT *a, const MD_RIFT *b)
{
    memset(out, 0, sizeof(*out));
    if (!a->Count)
        return MdRiftCopy(out, b);
    if (!b->Count)
        return MdRiftCopy(out, a);
    if (a->Entries[0].Left != INT64_MIN) {
        const MD_RIFT_ENTRY *last = &a->Entries[a->Count - 1];
        uint64_t y = (uint64_t)last->Right - (uint64_t)last->Left + (uint64_t)INT64_MIN;
        if (!carry(out, b, (uint64_t)INT64_MIN, (uint64_t)a->Entries[0].Left - 1, y))
            goto fail;
    }
    for (size_t i = 0; i < a->Count; i++) {
        uint64_t last = INT64_MAX;
        if (i + 1 < a->Count) {
            if (a->Entries[i + 1].Left == a->Entries[i].Left)
                continue;
            last = (uint64_t)a->Entries[i + 1].Left - 1;
        }
        if (!carry(out, b, (uint64_t)a->Entries[i].Left, last, (uint64_t)a->Entries[i].Right))
            goto fail;
    }
    MdRiftSort(out);
    return true;
fail:
    MdRiftFree(out);
    return false;
}

void MdRiftReduce(MD_RIFT *rift)
{
    size_t kept = 0;
    for (size_t i = 0; i < rift->Count;) {
        int64_t left = rift->Entries[i].Left, right = rift->Entries[i].Right;
        size_t votes = 1;
        i++;
        for (; i < rift->Count && rift->Entries[i].Left == left; i++) {
            if (rift->Entries[i].Right == right)
                votes++;
            else if (votes)
                votes--;
            else {
                right = rift->Entries[i].Right;
                votes = 1;
            }
        }
        if (!kept || (uint64_t)rift->Entries[kept - 1].Right - (uint64_t)rift->Entries[kept - 1].Left !=
                         (uint64_t)right - (uint64_t)left) {
            rift->Entries[kept++] = (MD_RIFT_ENTRY){left, right};
        }
    }
    if (kept == 1) {
        if (rift->Entries[0].Left != rift->Entries[0].Right) {
            rift->Entries[0].Right = (int64_t)((uint64_t)rift->Entries[0].Right - (uint64_t)rift->Entries[0].Left);
            rift->Entries[0].Left = 0;
        } else
            kept = 0;
    } else if (kept > 1 &&
               (uint64_t)rift->Entries[0].Right - (uint64_t)rift->Entries[0].Left ==
                   (uint64_t)rift->Entries[kept - 1].Right - (uint64_t)rift->Entries[kept - 1].Left) {
        memmove(rift->Entries, rift->Entries + 1, (kept - 1) * sizeof(*rift->Entries));
        kept--;
    }
    rift->Count = kept;
}

/* ------------------------------------------------------------------ the numbers of a rift table */

bool MdHuffmanBuild(MD_HUFFMAN *h, const uint8_t *lengths, unsigned symbols)
{
    memset(h, 0, sizeof(*h));
    for (unsigned i = 0; i < symbols; i++) {
        if (lengths[i] > MD_HUFFMAN_MAX)
            return false;
        if (lengths[i]) {
            h->Count[lengths[i]]++;
            if (lengths[i] > h->Maximum)
                h->Maximum = lengths[i];
        }
    }
    unsigned room = 2;
    for (unsigned length = 1; length <= MD_HUFFMAN_MAX; length++) {
        if (h->Count[length] > room)
            return false;
        room = 2 * (room - h->Count[length]);
    }
    uint32_t code = 0;
    for (unsigned length = MD_HUFFMAN_MAX; length >= 1; length--) {
        h->Start[length] = code;
        code = (h->Count[length] + code) >> 1;
    }
    unsigned total = 0;
    for (unsigned length = 1; length <= MD_HUFFMAN_MAX; length++) {
        h->Offset[length] = (uint16_t)total;
        total += h->Count[length];
    }
    h->Sorted = MdAlloc((total ? total : 1) * sizeof(uint16_t));
    if (!h->Sorted)
        return false;
    uint16_t next[MD_HUFFMAN_MAX + 1];
    memcpy(next, h->Offset, sizeof(next));
    for (unsigned i = 0; i < symbols; i++)
        if (lengths[i])
            h->Sorted[next[lengths[i]]++] = (uint16_t)i;
    return true;
}

void MdHuffmanFree(MD_HUFFMAN *h)
{
    MsDeltaFree(h->Sorted);
    h->Sorted = nullptr;
}

int MdHuffmanDecode(const MD_HUFFMAN *h, MD_BITS *bits)
{
    uint32_t code = 0;
    for (unsigned length = 1; length <= h->Maximum; length++) {
        code = code << 1 | (uint32_t)MdBitsRead(bits, 1);
        if (bits->Failed)
            return -1;
        if (h->Count[length] && code >= h->Start[length] && code - h->Start[length] < h->Count[length])
            return h->Sorted[h->Offset[length] + code - h->Start[length]];
    }
    bits->Failed = true;
    return -1;
}

typedef struct MD_INT_FORMAT {
    MD_HUFFMAN Huffman;
} MD_INT_FORMAT;

/* the lengths of the 252 symbols are sent as two lists of explicit ones and a rule for the rest. */
static bool int_format_read(MD_BITS *bits, MD_INT_FORMAT *format)
{
    memset(format, 0, sizeof(*format));
    unsigned positive = (unsigned)MdBitsRead(bits, 8), negative = (unsigned)MdBitsRead(bits, 8);
    unsigned run = (unsigned)MdBitsRead(bits, 8);
    if (bits->Failed || positive > 126 || negative > 126 || 252 - negative - positive < run)
        return false;
    uint8_t lengths[252];
    for (unsigned i = 0; i < positive; i++)
        lengths[i] = (uint8_t)(MdBitsRead(bits, 4) + 1);
    unsigned last = 0;
    for (unsigned j = 0;; j++) {
        last = (unsigned)MdBitsRead(bits, 4);
        if (negative <= j)
            break;
        lengths[126 + j] = (uint8_t)(last + 1);
    }
    int length = (int)last + 1;
    unsigned counter = run;
    for (unsigned i = positive; i < 126; i++) {
        if (counter == 0) {
            length--;
            counter = 252 - i - negative;
        } else
            counter--;
        lengths[i] = (uint8_t)length;
    }
    for (unsigned i = negative; i < 126; i++) {
        if (counter == 0) {
            length--;
            counter = 126 - i;
        } else
            counter--;
        lengths[126 + i] = (uint8_t)length;
    }
    return !bits->Failed && MdHuffmanBuild(&format->Huffman, lengths, 252);
}

static int64_t int_format_number(const MD_INT_FORMAT *format, MD_BITS *bits)
{
    int symbol = MdHuffmanDecode(&format->Huffman, bits);
    if (symbol < 0)
        return 0;
    bool negative = symbol >= 126;
    unsigned k = (unsigned)(negative ? symbol - 126 : symbol);
    uint64_t value = k;
    if (k >= 4) {
        unsigned count = (k >> 1) - 1;
        value = (uint64_t)((k & 1) + 2) << count | MdBitsRead(bits, count);
    }
    return negative ? (int64_t)~value : (int64_t)value;
}

bool MdRiftRead(MD_BITS *bits, MD_RIFT *rift)
{
    memset(rift, 0, sizeof(*rift));
    if (!MdBitsRead(bits, 1))
        return !bits->Failed;
    MD_INT_FORMAT lefts, shifts;
    if (!int_format_read(bits, &lefts))
        return false;
    bool ok = int_format_read(bits, &shifts);
    uint64_t count = ok ? MdBitsReadNumber(bits) : 0;
    ok = ok && !bits->Failed && count <= 0x0FFFFFFFFFFFFFFFull;
    uint64_t left = 0, shift = 0;
    for (uint64_t i = 0; ok && i < count; i++) {
        left += (uint64_t)int_format_number(&lefts, bits);
        shift += (uint64_t)int_format_number(&shifts, bits);
        ok = !bits->Failed && MdRiftAdd(rift, (int64_t)left, (int64_t)(left + shift));
    }
    MdHuffmanFree(&lefts.Huffman);
    MdHuffmanFree(&shifts.Huffman);
    if (!ok) {
        MdRiftFree(rift);
        return false;
    }
    MdRiftSort(rift);
    return true;
}
