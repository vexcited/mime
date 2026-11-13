/* writes the compound storage that installer databases live in. */
#include "compound.h"
#ifdef _WIN32
#include <rtl.h>
#else
#include <string.h>
#endif

#define CFB_END 0xFFFFFFFEu
#define CFB_FREE 0xFFFFFFFFu
#define SECTOR 512u

typedef struct WRITE_ENTRY {
    const MIME_CFB_NODE *Node;
    uint32_t Left, Right, Child, Start;
    uint8_t Color;
} WRITE_ENTRY;

typedef struct WRITE_STATE {
    WRITE_ENTRY *Entries;
    uint32_t Count;
    MIME_CFB_ALLOC Allocate;
    MIME_CFB_FREE Free;
} WRITE_STATE;

static size_t name_length(const uint16_t *name)
{
    size_t length = 0;
    while (name[length])
        length++;
    return length;
}

static uint16_t upper(uint16_t value)
{
    return value >= 'a' && value <= 'z' ? (uint16_t)(value - 32) : value;
}

/* shorter names sort first, equal lengths by their upper case letters. */
static int before(const MIME_CFB_NODE *a, const MIME_CFB_NODE *b)
{
    size_t left = name_length(a->Name), right = name_length(b->Name);
    if (left != right)
        return left < right;
    for (size_t i = 0; i < left; i++) {
        uint16_t x = upper(a->Name[i]), y = upper(b->Name[i]);
        if (x != y)
            return x < y;
    }
    return 0;
}

static uint32_t count_nodes(const MIME_CFB_NODE *list)
{
    uint32_t count = 0;
    for (; list; list = list->Next)
        count += 1 + (list->Type == 1 ? count_nodes(list->Children) : 0);
    return count;
}

static uint32_t balance(WRITE_STATE *state, const uint32_t *order, uint32_t low, uint32_t high,
                        uint32_t depth, uint32_t red_depth)
{
    if (low >= high)
        return CFB_FREE;
    uint32_t middle = low + (high - low) / 2;
    uint32_t index = order[middle];
    /* The incomplete last level is red: every path to a nil leaf has the same black height. */
    state->Entries[index].Color = depth == red_depth ? 0 : 1;
    state->Entries[index].Left = balance(state, order, low, middle, depth + 1, red_depth);
    state->Entries[index].Right = balance(state, order, middle + 1, high, depth + 1, red_depth);
    return index;
}

static int build(WRITE_STATE *state, const MIME_CFB_NODE *list, uint32_t *root)
{
    uint32_t count = 0;
    for (const MIME_CFB_NODE *node = list; node; node = node->Next)
        count++;
    *root = CFB_FREE;
    if (!count)
        return 1;
    const MIME_CFB_NODE **nodes = state->Allocate(count * sizeof(*nodes));
    uint32_t *order = state->Allocate(count * sizeof(*order));
    if (!nodes || !order) {
        state->Free(nodes);
        state->Free(order);
        return 0;
    }
    uint32_t at = 0;
    for (const MIME_CFB_NODE *node = list; node; node = node->Next) {
        uint32_t place = at++;
        while (place && before(node, nodes[place - 1])) {
            nodes[place] = nodes[place - 1];
            place--;
        }
        nodes[place] = node;
    }
    uint32_t first = state->Count;
    for (uint32_t i = 0; i < count; i++) {
        state->Entries[state->Count].Node = nodes[i];
        state->Entries[state->Count].Left = state->Entries[state->Count].Right = CFB_FREE;
        state->Entries[state->Count].Child = CFB_FREE;
        order[i] = state->Count++;
    }
    uint32_t red_depth = 0;
    for (uint64_t levels = (uint64_t)count + 1; levels > 1; levels >>= 1)
        red_depth++;
    *root = balance(state, order, 0, count, 0, red_depth);
    int ok = 1;
    for (uint32_t i = 0; ok && i < count; i++)
        if (nodes[i]->Type == 1)
            ok = build(state, nodes[i]->Children, &state->Entries[first + i].Child);
    state->Free(nodes);
    state->Free(order);
    return ok;
}

static void put16(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
    put16(p, value);
    put16(p + 2, value >> 16);
}

int MimeCfbWrite(const MIME_CFB_NODE *children, const uint8_t root_class[16], MIME_CFB_ALLOC allocate,
                 MIME_CFB_FREE release, uint8_t **out, size_t *size)
{
    *out = nullptr;
    *size = 0;
    WRITE_STATE state = {.Allocate = allocate, .Free = release};
    uint32_t total = count_nodes(children) + 1;
    state.Entries = allocate((size_t)total * sizeof(WRITE_ENTRY));
    if (!state.Entries)
        return 0;
    memset(state.Entries, 0, (size_t)total * sizeof(WRITE_ENTRY));
    state.Entries[0].Left = state.Entries[0].Right = CFB_FREE;
    state.Count = 1;
    int ok = build(&state, children, &state.Entries[0].Child);
    /* small streams share the mini stream in 64 byte blocks, the rest take whole sectors. */
    size_t mini_blocks = 0, big_sectors = 0;
    for (uint32_t i = 1; ok && i < total; i++) {
        const MIME_CFB_NODE *node = state.Entries[i].Node;
        if (node->Type != 2)
            continue;
        if (node->Size > 0x7FFFFFFF)
            ok = 0;
        else if (node->Size < 4096)
            mini_blocks += (node->Size + 63) / 64;
        else
            big_sectors += (node->Size + SECTOR - 1) / SECTOR;
    }
    if (!ok || mini_blocks > UINT32_MAX / 64 || big_sectors > UINT32_MAX / 2) {
        release(state.Entries);
        return 0;
    }
    size_t directory_sectors = ((size_t)total + 3) / 4;
    size_t mini_fat_sectors = (mini_blocks * 4 + SECTOR - 1) / SECTOR;
    size_t mini_sectors = (mini_blocks * 64 + SECTOR - 1) / SECTOR;
    size_t body = directory_sectors + mini_fat_sectors + mini_sectors + big_sectors;
    size_t fat_sectors = 1, difat_sectors = 0;
    for (;;) {
        difat_sectors = fat_sectors > 109 ? (fat_sectors - 109 + 126) / 127 : 0;
        if (fat_sectors * 128 >= fat_sectors + difat_sectors + body)
            break;
        fat_sectors++;
    }
    size_t sectors = fat_sectors + difat_sectors + body;
    size_t length = (sectors + 1) * SECTOR;
    uint8_t *file = allocate(length);
    uint32_t *fat = allocate(fat_sectors * 128 * sizeof(uint32_t));
    if (!file || !fat) {
        release(file);
        release(fat);
        release(state.Entries);
        return 0;
    }
    memset(file, 0, length);
    for (size_t i = 0; i < fat_sectors * 128; i++)
        fat[i] = CFB_FREE;
    uint32_t directory_start = (uint32_t)(fat_sectors + difat_sectors);
    uint32_t mini_fat_start = directory_start + (uint32_t)directory_sectors;
    uint32_t mini_start = mini_fat_start + (uint32_t)mini_fat_sectors;
    uint32_t next_big = mini_start + (uint32_t)mini_sectors;
    for (size_t i = 0; i < fat_sectors; i++)
        fat[i] = 0xFFFFFFFDu;
    for (size_t i = 0; i < difat_sectors; i++)
        fat[fat_sectors + i] = 0xFFFFFFFCu;
#define CHAIN(start, count) \
    for (size_t k_ = 0; k_ < (count); k_++) \
        fat[(start) + k_] = k_ + 1 < (count) ? (uint32_t)((start) + k_ + 1) : CFB_END
    CHAIN(directory_start, directory_sectors);
    CHAIN(mini_fat_start, mini_fat_sectors);
    CHAIN(mini_start, mini_sectors);
    uint8_t *mini = file + ((size_t)mini_start + 1) * SECTOR;
    uint32_t *mini_fat = (uint32_t *)(void *)(file + ((size_t)mini_fat_start + 1) * SECTOR);
    size_t mini_used = 0;
    for (size_t i = 0; i < mini_fat_sectors * 128; i++)
        put32((uint8_t *)&mini_fat[i], CFB_FREE);
    for (uint32_t i = 1; i < total; i++) {
        const MIME_CFB_NODE *node = state.Entries[i].Node;
        state.Entries[i].Start = CFB_END;
        if (node->Type != 2 || !node->Size)
            continue;
        if (node->Size < 4096) {
            size_t blocks = (node->Size + 63) / 64;
            state.Entries[i].Start = (uint32_t)mini_used;
            memcpy(mini + mini_used * 64, node->Data, node->Size);
            for (size_t k = 0; k < blocks; k++)
                put32((uint8_t *)&mini_fat[mini_used + k], k + 1 < blocks ? (uint32_t)(mini_used + k + 1) : CFB_END);
            mini_used += blocks;
        } else {
            size_t count = (node->Size + SECTOR - 1) / SECTOR;
            state.Entries[i].Start = next_big;
            memcpy(file + ((size_t)next_big + 1) * SECTOR, node->Data, node->Size);
            CHAIN(next_big, count);
            next_big += (uint32_t)count;
        }
    }
    uint8_t *directory = file + ((size_t)directory_start + 1) * SECTOR;
    for (size_t i = 0; i < directory_sectors * 4; i++) {
        uint8_t *entry = directory + i * 128;
        if (i >= total) {
            put32(entry + 68, CFB_FREE);
            put32(entry + 72, CFB_FREE);
            put32(entry + 76, CFB_FREE);
            continue;
        }
        WRITE_ENTRY *source = &state.Entries[i];
        const uint16_t *name = i ? source->Node->Name : (const uint16_t[]){'R', 'o', 'o', 't', ' ', 'E', 'n', 't', 'r', 'y', 0};
        size_t characters = name_length(name);
        for (size_t k = 0; k < characters; k++)
            put16(entry + k * 2, name[k]);
        put16(entry + 64, (uint32_t)(characters + 1) * 2);
        entry[66] = i ? source->Node->Type : 5;
        entry[67] = i ? source->Color : 1;
        put32(entry + 68, source->Left);
        put32(entry + 72, source->Right);
        put32(entry + 76, source->Child);
        const uint8_t *class_id = i ? source->Node->ClassId : root_class;
        if (class_id)
            memcpy(entry + 80, class_id, 16);
        if (i) {
            memcpy(entry + 96, source->Node->Metadata, 20);
            put32(entry + 116, source->Node->Type == 2 ? source->Start : 0);
            put32(entry + 120, source->Node->Type == 2 ? (uint32_t)source->Node->Size : 0);
        } else {
            put32(entry + 116, mini_sectors ? mini_start : CFB_END);
            put32(entry + 120, (uint32_t)(mini_used * 64));
        }
    }
    uint8_t *header = file;
    static const uint8_t signature[] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    memcpy(header, signature, sizeof(signature));
    put16(header + 24, 0x3E);
    put16(header + 26, 3);
    put16(header + 28, 0xFFFE);
    put16(header + 30, 9);
    put16(header + 32, 6);
    put32(header + 44, (uint32_t)fat_sectors);
    put32(header + 48, directory_start);
    put32(header + 56, 4096);
    put32(header + 60, mini_fat_sectors ? mini_fat_start : CFB_END);
    put32(header + 64, (uint32_t)mini_fat_sectors);
    put32(header + 68, difat_sectors ? (uint32_t)fat_sectors : CFB_END);
    put32(header + 72, (uint32_t)difat_sectors);
    for (size_t i = 0; i < 109; i++)
        put32(header + 76 + i * 4, i < fat_sectors ? (uint32_t)i : CFB_FREE);
    for (size_t d = 0; d < difat_sectors; d++) {
        uint8_t *sector = file + (fat_sectors + d + 1) * SECTOR;
        for (size_t k = 0; k < 127; k++) {
            size_t at = 109 + d * 127 + k;
            put32(sector + k * 4, at < fat_sectors ? (uint32_t)at : CFB_FREE);
        }
        put32(sector + 127 * 4, d + 1 < difat_sectors ? (uint32_t)(fat_sectors + d + 1) : CFB_END);
    }
    for (size_t i = 0; i < fat_sectors; i++)
        for (size_t k = 0; k < 128; k++)
            put32(file + (i + 1) * SECTOR + k * 4, fat[i * 128 + k]);
    release(fat);
    release(state.Entries);
    *out = file;
    *size = length;
    return 1;
}
