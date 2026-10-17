/*
 * the hid report descriptor parser: a descriptor in, for each top level
 * collection the preparsed data hid.dll and raw input read, laid out as
 * windows' hidparse lays it out, and the table of report ids. two passes:
 * the first checks the structure and sizes the blobs, the second fills
 * them. see docs/hid.md.
 */
#include "../ntos.h"

#define CAP_SIZE 104
#define NODE_SIZE 16
#define HEADER_SIZE 44
#define MAX_USAGES 256
#define MAX_DEPTH 32

enum { T_INPUT, T_OUTPUT, T_FEATURE };

enum {
    CAP_ARRAY_MORE = 0x1,
    CAP_CONSTANT = 0x2,
    CAP_BUTTON = 0x4,
    CAP_ABSOLUTE = 0x8,
    CAP_RANGE = 0x10,
    CAP_ALIAS = 0x20,
    CAP_STRING_RANGE = 0x40,
    CAP_DESIGNATOR_RANGE = 0x80,
};

typedef struct ITEM {
    UCHAR Prefix;
    ULONG Size;
    ULONG Unsigned;
    LONG Signed;
} ITEM;

typedef struct REPORT {
    UCHAR Id;
    UCHAR Collection; /* 1 based */
    BOOLEAN Claimed;
    ULONG Bits[3];
} REPORT;

typedef struct TOKEN {
    UCHAR Token;
    ULONG Data;
} TOKEN;

typedef struct GLOBALS {
    USHORT UsagePage;
    LONG LogicalMin, LogicalMax, PhysicalMin, PhysicalMax, UnitExp;
    ULONG Unit;
    ULONG ReportSize, ReportCount;
    LONG Report; /* index in the report table, -1 for none */
    ULONG Tokens;
    TOKEN Token[4];
} GLOBALS;

typedef struct USAGE {
    USHORT Page;
    BOOLEAN HasPage;
    BOOLEAN Range;
    BOOLEAN HasUsage, HasMin, HasMax;
    BOOLEAN Alias;
    USHORT Usage, Min, Max;
} USAGE;

typedef struct LOCALS {
    ULONG Count;
    USAGE Usage[MAX_USAGES];
    BOOLEAN InSet, SetStarted;
    USHORT StringIndex, StringMin, StringMax;
    BOOLEAN StringRange;
    USHORT DesignatorIndex, DesignatorMin, DesignatorMax;
    BOOLEAN DesignatorRange;
} LOCALS;

typedef struct TLC {
    USHORT Caps[3];
    USHORT Nodes;
    PUCHAR Blob;
    ULONG Length;
    USHORT Last[3]; /* next free cap slot of each type */
    USHORT NodesUsed;
} TLC;

typedef struct PARSER {
    const UCHAR *Data;
    ULONG Length;
    ULONG Offset;
    ULONG Error;
    TLC *Tlc;
    ULONG Tlcs;
    REPORT *Reports;
    ULONG ReportCount, ReportCapacity;
    BOOLEAN UsesIds;
    GLOBALS Global;
    GLOBALS Stack[MAX_DEPTH];
    ULONG StackDepth;
    LOCALS *Local;
} PARSER;

static NTSTATUS fail(PARSER *p, ULONG error, NTSTATUS status)
{
    p->Error = error;
    return status;
}

static PVOID zalloc(SIZE_T size)
{
    PVOID p = ExAllocatePool2(POOL_FLAG_NON_PAGED, size ? size : 1, KI_TAG('H', 'i', 'd', 'P'));
    if (p)
        memset(p, 0, size ? size : 1);
    return p;
}

/* the item at the parser's offset; FALSE at the end, an error status in *status. */
static BOOLEAN next_item(PARSER *p, ITEM *item, NTSTATUS *status)
{
    *status = STATUS_SUCCESS;
    if (p->Offset >= p->Length)
        return FALSE;
    UCHAR prefix = p->Data[p->Offset];
    if (prefix == 0xFE) {
        *status = fail(p, 18, STATUS_INVALID_PARAMETER_MIX);
        return FALSE;
    }
    static const UCHAR sizes[4] = {0, 1, 2, 4};
    ULONG size = sizes[prefix & 3];
    if (p->Offset + 1 + size > p->Length) {
        static const ULONG errors[5] = {0, 6, 7, 0, 8};
        *status = fail(p, errors[size], STATUS_BUFFER_TOO_SMALL);
        return FALSE;
    }
    const UCHAR *d = p->Data + p->Offset + 1;
    item->Prefix = prefix;
    item->Size = size;
    item->Unsigned = 0;
    for (ULONG i = 0; i < size; i++)
        item->Unsigned |= (ULONG)d[i] << (8 * i);
    if (size == 1)
        item->Signed = (CHAR)item->Unsigned;
    else if (size == 2)
        item->Signed = (SHORT)item->Unsigned;
    else
        item->Signed = (LONG)item->Unsigned;
    p->Offset += 1 + size;
    return TRUE;
}

/* ------------------------------------------------------------------ pass 1 */

static NTSTATUS count_pass(PARSER *p, ULONG *tlcs)
{
    ITEM item;
    NTSTATUS status;
    LONG depth = 0;
    ULONG usages = 0, count = 0;
    TLC counts = {};
    p->Offset = 0;
    while (next_item(p, &item, &status)) {
        switch (item.Prefix & 0xFC) {
        case 0x08:
        case 0x18:
            usages++;
            break;
        case 0xA8:
            if (item.Unsigned == 1) {
                for (;;) {
                    if (!next_item(p, &item, &status))
                        return NT_SUCCESS(status) ? fail(p, 32, STATUS_INVALID_PARAMETER_MIX) : status;
                    if ((item.Prefix & 0xFC) == 0xA8) {
                        if (item.Unsigned != 0)
                            return fail(p, 34, STATUS_INVALID_PARAMETER_MIX);
                        break;
                    }
                    if ((item.Prefix & 0xFC) != 0x08)
                        return fail(p, 33, STATUS_INVALID_PARAMETER_MIX);
                    usages++;
                }
            } else {
                return fail(p, 34, STATUS_INVALID_PARAMETER_MIX);
            }
            break;
        case 0xA0:
            if (++depth == 1) {
                if (usages != 1)
                    return fail(p, 11, STATUS_INVALID_PARAMETER_MIX);
                memset(&counts, 0, sizeof(counts));
                counts.Nodes = 1;
            } else {
                counts.Nodes += (USHORT)(usages ? usages : 1);
            }
            usages = 0;
            break;
        case 0xC0:
            if (--depth < 0)
                return fail(p, 4, STATUS_INVALID_PARAMETER_MIX);
            if (!depth) {
                ULONG size = HEADER_SIZE + CAP_SIZE * (counts.Caps[0] + counts.Caps[1] + counts.Caps[2]) +
                             NODE_SIZE * counts.Nodes + CAP_SIZE;
                if (size > 0xFFFF)
                    return fail(p, 5, STATUS_INVALID_PARAMETER_MIX);
                if (p->Tlc) {
                    p->Tlc[count] = counts;
                    p->Tlc[count].Length = size;
                }
                count++;
            }
            usages = 0;
            break;
        case 0x80:
        case 0x90:
        case 0xB0: {
            if (!depth)
                return fail(p, 27, STATUS_INVALID_PARAMETER_MIX);
            if (item.Size == 0 || item.Size == 4)
                return fail(p, 18, STATUS_INVALID_PARAMETER_MIX);
            ULONG type = (item.Prefix & 0xFC) == 0x80 ? T_INPUT : (item.Prefix & 0xFC) == 0x90 ? T_OUTPUT : T_FEATURE;
            if (usages)
                counts.Caps[type] += (USHORT)usages;
            else if (!(item.Unsigned & 1))
                return fail(p, 10, STATUS_INVALID_PARAMETER_MIX);
            usages = 0;
            break;
        }
        case 0x84:
            if (!depth)
                return fail(p, 19, STATUS_INVALID_PARAMETER_MIX);
            p->UsesIds = TRUE;
            break;
        }
    }
    if (!NT_SUCCESS(status))
        return status;
    if (depth)
        return fail(p, 4, STATUS_INVALID_PARAMETER_MIX);
    if (!count)
        return fail(p, 26, STATUS_NO_DATA_DETECTED);
    *tlcs = count;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ pass 2 */

static PUCHAR cap_at(TLC *t, ULONG index)
{
    return t->Blob + HEADER_SIZE + CAP_SIZE * index;
}

static PUCHAR node_at(TLC *t, ULONG index)
{
    ULONG caps = t->Caps[0] + t->Caps[1] + t->Caps[2];
    return t->Blob + HEADER_SIZE + CAP_SIZE * caps + NODE_SIZE * index;
}

static void put16(PUCHAR p, ULONG v)
{
    p[0] = (UCHAR)v;
    p[1] = (UCHAR)(v >> 8);
}

static void put32(PUCHAR p, ULONG v)
{
    put16(p, v);
    put16(p + 2, v >> 16);
}

static USHORT get16(const UCHAR *p)
{
    return (USHORT)(p[0] | p[1] << 8);
}

static ULONG usage_count(const USAGE *u)
{
    return u->Range ? (ULONG)u->Max - u->Min + 1 : 1;
}

static void clear_locals(LOCALS *l, BOOLEAN keep_strings)
{
    l->Count = 0;
    l->InSet = FALSE;
    l->SetStarted = FALSE;
    if (!keep_strings) {
        l->StringIndex = l->StringMin = l->StringMax = 0;
        l->StringRange = FALSE;
        l->DesignatorIndex = l->DesignatorMin = l->DesignatorMax = 0;
        l->DesignatorRange = FALSE;
    }
}

static USAGE *push_usage(PARSER *p)
{
    LOCALS *l = p->Local;
    if (l->Count >= MAX_USAGES)
        return nullptr;
    USAGE *u = &l->Usage[l->Count++];
    memset(u, 0, sizeof(*u));
    if (l->InSet) {
        u->Alias = l->SetStarted;
        l->SetStarted = TRUE;
    }
    return u;
}

static USHORT page_of(PARSER *p, const USAGE *u)
{
    return u->HasPage ? u->Page : p->Global.UsagePage;
}

static NTSTATUS local_item(PARSER *p, const ITEM *item)
{
    LOCALS *l = p->Local;
    USAGE *top = l->Count ? &l->Usage[l->Count - 1] : nullptr;
    USAGE *u;
    switch (item->Prefix) {
    case 0x09:
    case 0x0A:
    case 0x0B:
        if (top && !top->Usage && !top->Min && !top->Max && !l->InSet)
            u = top;
        else if (!(u = push_usage(p)))
            return fail(p, 16, STATUS_INSUFFICIENT_RESOURCES);
        u->Usage = (USHORT)item->Unsigned;
        u->HasUsage = TRUE;
        if (item->Size == 4) {
            u->Page = (USHORT)(item->Unsigned >> 16);
            u->HasPage = TRUE;
        }
        return STATUS_SUCCESS;
    case 0x19:
    case 0x1A:
    case 0x1B:
        if (top && !top->HasUsage && !top->HasMin)
            u = top;
        else if (!(u = push_usage(p)))
            return fail(p, 16, STATUS_INSUFFICIENT_RESOURCES);
        u->Range = TRUE;
        u->Min = (USHORT)item->Unsigned;
        u->HasMin = TRUE;
        if (item->Size == 4) {
            u->Page = (USHORT)(item->Unsigned >> 16);
            u->HasPage = TRUE;
        }
        return STATUS_SUCCESS;
    case 0x29:
    case 0x2A:
    case 0x2B:
        if (top && !top->HasUsage && !top->HasMax)
            u = top;
        else if (!(u = push_usage(p)))
            return fail(p, 16, STATUS_INSUFFICIENT_RESOURCES);
        u->Range = TRUE;
        u->Max = (USHORT)item->Unsigned;
        u->HasMax = TRUE;
        if (item->Size == 4) {
            u->Page = (USHORT)(item->Unsigned >> 16);
            u->HasPage = TRUE;
        }
        return STATUS_SUCCESS;
    case 0x39:
        l->DesignatorIndex = (USHORT)item->Unsigned;
        l->DesignatorRange = FALSE;
        return STATUS_SUCCESS;
    case 0x49:
        l->DesignatorMin = (USHORT)item->Unsigned;
        l->DesignatorRange = TRUE;
        return STATUS_SUCCESS;
    case 0x59:
        l->DesignatorMax = (USHORT)item->Unsigned;
        l->DesignatorRange = TRUE;
        return STATUS_SUCCESS;
    case 0x79:
        l->StringIndex = (USHORT)item->Unsigned;
        l->StringRange = FALSE;
        return STATUS_SUCCESS;
    case 0x89:
        l->StringMin = (USHORT)item->Unsigned;
        l->StringRange = TRUE;
        return STATUS_SUCCESS;
    case 0x99:
        l->StringMax = (USHORT)item->Unsigned;
        l->StringRange = TRUE;
        return STATUS_SUCCESS;
    case 0xA9:
        l->InSet = item->Unsigned == 1;
        l->SetStarted = FALSE;
        return STATUS_SUCCESS;
    }
    return fail(p, 18, STATUS_ILLEGAL_INSTRUCTION);
}

static void add_token(GLOBALS *g, const ITEM *item)
{
    ULONG slot = g->Tokens < 4 ? g->Tokens++ : 3;
    g->Token[slot].Token = item->Prefix;
    if (item->Size)
        g->Token[slot].Data = item->Unsigned;
}

static NTSTATUS global_item(PARSER *p, const ITEM *item)
{
    GLOBALS *g = &p->Global;
    switch (item->Prefix) {
    case 0x05:
    case 0x06:
        g->UsagePage = (USHORT)item->Unsigned;
        return STATUS_SUCCESS;
    case 0x15:
    case 0x16:
    case 0x17:
        g->LogicalMin = item->Signed;
        return STATUS_SUCCESS;
    case 0x25:
    case 0x26:
    case 0x27:
        g->LogicalMax = item->Signed;
        return STATUS_SUCCESS;
    case 0x35:
    case 0x36:
    case 0x37:
        g->PhysicalMin = item->Signed;
        return STATUS_SUCCESS;
    case 0x45:
    case 0x46:
    case 0x47:
        g->PhysicalMax = item->Signed;
        return STATUS_SUCCESS;
    case 0x55:
    case 0x56:
    case 0x57:
        g->UnitExp = item->Signed;
        return STATUS_SUCCESS;
    case 0x65:
    case 0x66:
    case 0x67:
        g->Unit = item->Unsigned;
        return STATUS_SUCCESS;
    case 0x75:
        g->ReportSize = item->Unsigned;
        return STATUS_SUCCESS;
    case 0x95:
    case 0x96:
        g->ReportCount = item->Unsigned;
        return STATUS_SUCCESS;
    case 0xA4:
        if (p->StackDepth >= MAX_DEPTH)
            return fail(p, 18, STATUS_INVALID_PARAMETER_MIX);
        p->Stack[p->StackDepth++] = *g;
        return STATUS_SUCCESS;
    case 0xB4:
        if (!p->StackDepth)
            return fail(p, 18, STATUS_INVALID_PARAMETER_MIX);
        *g = p->Stack[--p->StackDepth];
        return STATUS_SUCCESS;
    }
    add_token(g, item);
    return STATUS_SUCCESS;
}

static REPORT *new_report(PARSER *p, UCHAR id, UCHAR collection)
{
    if (p->ReportCount == p->ReportCapacity) {
        ULONG capacity = p->ReportCapacity ? p->ReportCapacity * 2 : 8;
        REPORT *grown = zalloc(sizeof(REPORT) * capacity);
        if (!grown)
            return nullptr;
        if (p->Reports) {
            memcpy(grown, p->Reports, sizeof(REPORT) * p->ReportCount);
            ExFreePool(p->Reports);
        }
        p->Reports = grown;
        p->ReportCapacity = capacity;
    }
    REPORT *r = &p->Reports[p->ReportCount++];
    memset(r, 0, sizeof(*r));
    r->Id = id;
    r->Collection = collection;
    r->Bits[0] = r->Bits[1] = r->Bits[2] = 8;
    return r;
}

static NTSTATUS report_id(PARSER *p, const ITEM *item, ULONG tlc, ULONG first_report)
{
    if (!item->Unsigned)
        return fail(p, 20, STATUS_INVALID_PARAMETER);
    UCHAR id = (UCHAR)item->Unsigned;
    for (ULONG i = 0; i < p->ReportCount; i++) {
        if (p->Reports[i].Claimed && p->Reports[i].Id == id) {
            if (p->Reports[i].Collection != tlc + 1)
                return fail(p, 19, STATUS_INVALID_PARAMETER);
            p->Global.Report = (LONG)i;
            return STATUS_SUCCESS;
        }
    }
    REPORT *first = &p->Reports[first_report];
    if (!first->Claimed) {
        first->Claimed = TRUE;
        first->Id = id;
        p->Global.Report = (LONG)first_report;
        return STATUS_SUCCESS;
    }
    REPORT *r = new_report(p, id, (UCHAR)(tlc + 1));
    if (!r)
        return fail(p, 3, STATUS_INSUFFICIENT_RESOURCES);
    r->Claimed = TRUE;
    p->Global.Report = (LONG)(r - p->Reports);
    return STATUS_SUCCESS;
}

/* the fields every cap of a main item shares. */
static void fill_cap(PARSER *p, TLC *t, PUCHAR cap, const USAGE *u, ULONG bits, ULONG count, ULONG start,
                     ULONG data, ULONG flags, USHORT link)
{
    LOCALS *l = p->Local;
    GLOBALS *g = &p->Global;
    USHORT page = page_of(p, u);
    put16(cap + 0, page);
    cap[2] = p->Reports[g->Report].Claimed ? p->Reports[g->Report].Id : 0;
    cap[3] = (UCHAR)(start % 8);
    put16(cap + 4, bits);
    put16(cap + 6, count);
    put16(cap + 8, start / 8);
    put16(cap + 10, bits * count);
    put32(cap + 12, data);
    put16(cap + 16, start / 8 + (start % 8 + bits * count + 7) / 8);
    PUCHAR node = node_at(t, link);
    put16(cap + 18, link);
    put16(cap + 20, get16(node + 2));
    put16(cap + 22, get16(node + 0));
    if (l->StringRange)
        flags |= CAP_STRING_RANGE;
    if (l->DesignatorRange)
        flags |= CAP_DESIGNATOR_RANGE;
    flags |= g->Tokens << 28;
    put32(cap + 24, flags);
    for (ULONG i = 0; i < g->Tokens; i++) {
        cap[28 + 8 * i] = g->Token[i].Token;
        put32(cap + 32 + 8 * i, g->Token[i].Data);
    }
    USHORT umin = u->Range ? u->Min : u->Usage;
    USHORT umax = u->Range ? u->Max : u->Usage;
    put16(cap + 60, umin);
    put16(cap + 62, umax);
    put16(cap + 64, l->StringRange ? l->StringMin : l->StringIndex);
    put16(cap + 66, l->StringRange ? l->StringMax : l->StringIndex);
    put16(cap + 68, l->DesignatorRange ? l->DesignatorMin : l->DesignatorIndex);
    put16(cap + 70, l->DesignatorRange ? l->DesignatorMax : l->DesignatorIndex);
    put32(cap + 96, g->Unit);
    put32(cap + 100, (ULONG)g->UnitExp);
    if (page == 1 && umin <= 0x83 && umax >= 0x81) {
        ULONG power = 0;
        if (umin <= 0x81 && umax >= 0x81)
            power |= 1;
        if (umin <= 0x82 && umax >= 0x82)
            power |= 2;
        if (umin <= 0x83 && umax >= 0x83)
            power |= 0x80000000;
        put32(t->Blob + 12, *(ULONG *)(t->Blob + 12) | power);
    }
}

static NTSTATUS main_item(PARSER *p, const ITEM *item, ULONG type, TLC *t, USHORT link)
{
    LOCALS *l = p->Local;
    GLOBALS *g = &p->Global;
    if (g->Report < 0)
        return fail(p, 22, STATUS_INVALID_PARAMETER_MIX);
    REPORT *r = &p->Reports[g->Report];
    if (p->UsesIds && !r->Claimed)
        return fail(p, 22, STATUS_INVALID_PARAMETER_MIX);
    ULONG data = item->Unsigned & (item->Size == 1 ? 0xFF : 0xFFFF);
    BOOLEAN constant = data & 1, variable = (data & 2) != 0;
    ULONG size = g->ReportSize, count = g->ReportCount;
    ULONG start = r->Bits[type];
    r->Bits[type] = (r->Bits[type] + size * count) & 0xFFFF;
    ULONG first = (type == T_INPUT ? 0 : type == T_OUTPUT ? t->Caps[0] : t->Caps[0] + t->Caps[1]);
    ULONG limit = first + t->Caps[type];
    ULONG flags = (data & 4) ? 0 : CAP_ABSOLUTE;

    if (!variable) {
        if (constant)
            return STATUS_SUCCESS; /* the usages carry over to the next main item */
        if (l->Count && l->Usage[l->Count - 1].Alias)
            return fail(p, 64, STATUS_INVALID_PARAMETER_MIX);
        PUCHAR last = nullptr;
        for (LONG i = (LONG)l->Count - 1; i >= 0; i--) {
            USAGE *u = &l->Usage[i];
            if (u->Range && u->Max < u->Min)
                return fail(p, 18, STATUS_ILLEGAL_INSTRUCTION);
            if (t->Last[type] >= limit)
                return fail(p, 18, STATUS_ILLEGAL_INSTRUCTION);
            last = cap_at(t, t->Last[type]++);
            fill_cap(p, t, last, u, size, count, start, data,
                     flags | CAP_ARRAY_MORE | CAP_BUTTON | (u->Range ? CAP_RANGE : 0), link);
            put32(last + 76, (ULONG)g->LogicalMin);
            put32(last + 80, (ULONG)g->LogicalMax);
        }
        if (last)
            put32(last + 24, *(ULONG *)(last + 24) & ~CAP_ARRAY_MORE);
        clear_locals(l, FALSE);
        return STATUS_SUCCESS;
    }

    USAGE *top = l->Count ? &l->Usage[l->Count - 1] : nullptr;
    if (!top || (!top->Usage && !top->Min && !top->Max && !top->Range)) {
        if (constant) {
            l->Count = 0;
            return STATUS_SUCCESS;
        }
        if (!top)
            return fail(p, 10, STATUS_INVALID_PARAMETER_MIX);
    }
    if (top->Range && top->Max < top->Min)
        return fail(p, 18, STATUS_ILLEGAL_INSTRUCTION);
    if (!count)
        return STATUS_SUCCESS;

    /* usages past the field count go, last declared first; aliases share their primary's fields. */
    ULONG total = 0;
    for (ULONG i = 0; i < l->Count; i++)
        if (!l->Usage[i].Alias)
            total += usage_count(&l->Usage[i]);
    while (total > count && l->Count) {
        USAGE *u = &l->Usage[l->Count - 1];
        if (u->Alias) {
            l->Count--;
            continue;
        }
        ULONG n = usage_count(u);
        if (n > total - count) {
            u->Max = (USHORT)(u->Max - (total - count));
            total = count;
        } else {
            l->Count--;
            total -= n;
        }
    }

    ULONG field[MAX_USAGES], fields[MAX_USAGES];
    ULONG at = 0, last_primary = 0;
    for (ULONG i = 0; i < l->Count; i++) {
        if (l->Usage[i].Alias) {
            field[i] = field[last_primary];
            fields[i] = fields[last_primary];
            continue;
        }
        field[i] = at;
        fields[i] = usage_count(&l->Usage[i]);
        at += fields[i];
        last_primary = i;
    }
    /* the last declared usage takes the remaining fields. */
    ULONG extra = count > total ? count - total : 0;
    for (LONG i = (LONG)l->Count - 1; i >= 0 && extra; i--)
        if (i == (LONG)last_primary || (l->Usage[i].Alias && field[i] == field[last_primary]))
            fields[i] += extra;

    BOOLEAN first_cap = TRUE;
    for (LONG i = (LONG)l->Count - 1; i >= 0; i--) {
        USAGE *u = &l->Usage[i];
        if (t->Last[type] >= limit)
            return fail(p, 18, STATUS_ILLEGAL_INSTRUCTION);
        PUCHAR cap = cap_at(t, t->Last[type]++);
        ULONG cap_flags = flags | (u->Range ? CAP_RANGE : 0) | (u->Alias ? CAP_ALIAS : 0);
        if (size == 1)
            cap_flags |= CAP_BUTTON;
        if (constant && first_cap)
            cap_flags |= CAP_CONSTANT;
        first_cap = FALSE;
        fill_cap(p, t, cap, u, size, fields[i], start + size * field[i], data, cap_flags, link);
        if (size != 1) {
            cap[76] = (data & 0x40) != 0;
            put32(cap + 80, (ULONG)g->LogicalMin);
            put32(cap + 84, (ULONG)g->LogicalMax);
            put32(cap + 88, (ULONG)g->PhysicalMin);
            put32(cap + 92, (ULONG)g->PhysicalMax);
        }
    }
    clear_locals(l, FALSE);
    return STATUS_SUCCESS;
}

/* data indices, per report type from 0; an array's caps count from its first declared usage. */
static void assign_indices(TLC *t)
{
    ULONG first = 0;
    for (ULONG type = 0; type < 3; type++) {
        ULONG next = 0;
        for (ULONG i = first; i < t->Last[type];) {
            ULONG end = i;
            while (end + 1 < t->Last[type] && (*(ULONG *)(cap_at(t, end) + 24) & CAP_ARRAY_MORE))
                end++;
            for (LONG c = (LONG)end; c >= (LONG)i; c--) {
                PUCHAR cap = cap_at(t, c);
                ULONG span = get16(cap + 62) - get16(cap + 60);
                put16(cap + 72, next);
                put16(cap + 74, next + span);
                next += span + 1;
            }
            i = end + 1;
        }
        first += t->Caps[type];
    }
}

static NTSTATUS close_tlc(PARSER *p, TLC *t, ULONG tlc)
{
    ULONG longest[3] = {};
    for (ULONG i = 0; i < p->ReportCount; i++) {
        REPORT *r = &p->Reports[i];
        if (r->Collection != tlc + 1)
            continue;
        for (ULONG type = 0; type < 3; type++) {
            if (r->Bits[type] % 8)
                return fail(p, 9, STATUS_INVALID_PARAMETER_MIX);
            if (r->Bits[type] / 8 > longest[type])
                longest[type] = r->Bits[type] / 8;
        }
    }
    ULONG first = 0;
    for (ULONG type = 0; type < 3; type++) {
        PUCHAR info = t->Blob + 16 + 8 * type;
        put16(info + 0, first);
        put16(info + 2, t->Caps[type]);
        put16(info + 4, t->Last[type]);
        put16(info + 6, longest[type] == 1 ? 0 : longest[type]);
        first += t->Caps[type];
    }
    put16(t->Blob + 40, CAP_SIZE * first);
    put16(t->Blob + 42, t->NodesUsed);
    assign_indices(t);
    return STATUS_SUCCESS;
}

static NTSTATUS fill_pass(PARSER *p)
{
    ITEM item;
    NTSTATUS status;
    LONG depth = 0;
    LONG tlc = -1;
    ULONG first_report = 0;
    USHORT stack[MAX_DEPTH];
    LOCALS *l = p->Local;
    TLC *t = nullptr;
    p->Offset = 0;
    p->Global.Report = -1;
    while (next_item(p, &item, &status)) {
        ULONG type = (item.Prefix >> 2) & 3;
        if (type == 1) {
            if (item.Prefix == 0x85 && depth > 0) {
                status = report_id(p, &item, (ULONG)tlc, first_report);
                if (!NT_SUCCESS(status))
                    return status;
                continue;
            }
            if (item.Prefix == 0x85)
                return fail(p, 19, STATUS_INVALID_PARAMETER_MIX);
            status = global_item(p, &item);
            if (!NT_SUCCESS(status))
                return status;
            continue;
        }
        if (type == 2) {
            status = local_item(p, &item);
            if (!NT_SUCCESS(status))
                return status;
            continue;
        }
        if (type == 3)
            return fail(p, 18, STATUS_ILLEGAL_INSTRUCTION);
        switch (item.Prefix & 0xFC) {
        case 0xA0: {
            USAGE *u = l->Count ? &l->Usage[l->Count - 1] : nullptr;
            USHORT page = u ? page_of(p, u) : p->Global.UsagePage;
            USHORT usage = u ? (u->Range ? u->Min : u->Usage) : 0;
            if (depth == 0) {
                if (tlc >= 0 && p->UsesIds && !p->Reports[first_report].Claimed)
                    return fail(p, 21, STATUS_INVALID_PARAMETER_MIX);
                t = &p->Tlc[++tlc];
                REPORT *r = new_report(p, 0, (UCHAR)(tlc + 1));
                if (!r)
                    return fail(p, 3, STATUS_INSUFFICIENT_RESOURCES);
                first_report = p->ReportCount - 1;
                p->Global.Report = (LONG)first_report;
                memcpy(t->Blob, "HidP KDR", 8);
                put16(t->Blob + 8, usage);
                put16(t->Blob + 10, page);
                ULONG at = 0;
                for (ULONG k = 0; k < 3; k++) {
                    t->Last[k] = (USHORT)at;
                    at += t->Caps[k];
                }
                PUCHAR node = node_at(t, 0);
                put16(node + 0, usage);
                put16(node + 2, page);
                put32(node + 12, item.Unsigned & 0xFF);
                t->NodesUsed = 1;
                stack[0] = 0;
            } else {
                if (depth >= MAX_DEPTH || t->NodesUsed >= t->Nodes)
                    return fail(p, 3, STATUS_INVALID_PARAMETER_MIX);
                USHORT parent = stack[depth - 1];
                USHORT index = t->NodesUsed++;
                PUCHAR node = node_at(t, index), up = node_at(t, parent);
                put16(node + 0, usage);
                put16(node + 2, page);
                put16(node + 4, parent);
                put16(node + 8, get16(up + 10));
                put32(node + 12, (item.Unsigned & 0xFF) | (u && u->Alias ? 0x100 : 0));
                put16(up + 10, index);
                put16(up + 6, get16(up + 6) + 1);
                stack[depth] = index;
            }
            depth++;
            clear_locals(l, FALSE);
            break;
        }
        case 0xC0:
            if (--depth < 0)
                return fail(p, 4, STATUS_INVALID_PARAMETER_MIX);
            if (!depth) {
                status = close_tlc(p, t, (ULONG)tlc);
                if (!NT_SUCCESS(status))
                    return status;
            }
            clear_locals(l, FALSE);
            break;
        case 0x80:
        case 0x90:
        case 0xB0: {
            if (item.Prefix == 0x83 || item.Prefix == 0x93 || item.Prefix == 0xB3 || !item.Size)
                return fail(p, 18, STATUS_ILLEGAL_INSTRUCTION);
            ULONG kind = (item.Prefix & 0xFC) == 0x80 ? T_INPUT : (item.Prefix & 0xFC) == 0x90 ? T_OUTPUT : T_FEATURE;
            status = main_item(p, &item, kind, t, stack[depth - 1]);
            if (!NT_SUCCESS(status))
                return status;
            break;
        }
        default:
            return fail(p, 18, STATUS_ILLEGAL_INSTRUCTION);
        }
    }
    return status;
}

void HidpFreeParse(HIDP_PARSE *parse)
{
    for (ULONG i = 0; i < parse->Collections; i++)
        if (parse->Collection[i].Preparsed)
            ExFreePool(parse->Collection[i].Preparsed);
    if (parse->Collection)
        ExFreePool(parse->Collection);
    if (parse->ReportId)
        ExFreePool(parse->ReportId);
    parse->Collection = nullptr;
    parse->ReportId = nullptr;
    parse->Collections = parse->ReportIds = 0;
}

NTSTATUS HidpParseDescriptor(const UCHAR *descriptor, ULONG length, HIDP_PARSE *out)
{
    memset(out, 0, sizeof(*out));
    PARSER *p = zalloc(sizeof(*p));
    if (!p)
        return STATUS_INSUFFICIENT_RESOURCES;
    p->Data = descriptor;
    p->Length = length;
    ULONG tlcs = 0;
    NTSTATUS status = count_pass(p, &tlcs);
    if (NT_SUCCESS(status)) {
        p->Tlc = zalloc(sizeof(TLC) * tlcs);
        p->Local = zalloc(sizeof(LOCALS));
        if (!p->Tlc || !p->Local)
            status = fail(p, 3, STATUS_INSUFFICIENT_RESOURCES);
    }
    if (NT_SUCCESS(status))
        status = count_pass(p, &tlcs);
    for (ULONG i = 0; NT_SUCCESS(status) && i < tlcs; i++)
        if (!(p->Tlc[i].Blob = zalloc(p->Tlc[i].Length)))
            status = fail(p, 3, STATUS_INSUFFICIENT_RESOURCES);
    if (NT_SUCCESS(status))
        status = fill_pass(p);
    if (NT_SUCCESS(status)) {
        out->Collection = zalloc(sizeof(HIDP_COLLECTION) * tlcs);
        out->ReportId = zalloc(sizeof(HIDP_REPORT_ID) * p->ReportCount);
        if (!out->Collection || !out->ReportId)
            status = STATUS_INSUFFICIENT_RESOURCES;
    }
    if (NT_SUCCESS(status)) {
        out->Collections = tlcs;
        for (ULONG i = 0; i < tlcs; i++) {
            TLC *t = &p->Tlc[i];
            HIDP_COLLECTION *c = &out->Collection[i];
            c->Usage = get16(t->Blob + 8);
            c->UsagePage = get16(t->Blob + 10);
            c->InputLength = get16(t->Blob + 16 + 6);
            c->OutputLength = get16(t->Blob + 24 + 6);
            c->FeatureLength = get16(t->Blob + 32 + 6);
            c->PreparsedLength = (USHORT)t->Length;
            c->Preparsed = t->Blob;
            t->Blob = nullptr;
        }
        /* the table counts bytes: with the id byte for numbered reports, the payload alone for report 0. */
        out->ReportIds = p->ReportCount;
        for (ULONG i = 0; i < p->ReportCount; i++) {
            REPORT *r = &p->Reports[i];
            HIDP_REPORT_ID *o = &out->ReportId[i];
            o->ReportId = r->Claimed ? r->Id : 0;
            o->Collection = r->Collection;
            USHORT *lengths[3] = {&o->InputLength, &o->OutputLength, &o->FeatureLength};
            for (ULONG type = 0; type < 3; type++) {
                ULONG bytes = r->Bits[type] / 8;
                *lengths[type] = (USHORT)(o->ReportId ? (bytes == 1 ? 0 : bytes) : bytes - 1);
            }
        }
    } else {
        HidpFreeParse(out);
    }
    out->BreakOffset = p->Offset;
    out->ErrorCode = p->Error;
    if (p->Tlc) {
        for (ULONG i = 0; i < tlcs; i++)
            if (p->Tlc[i].Blob)
                ExFreePool(p->Tlc[i].Blob);
        ExFreePool(p->Tlc);
    }
    if (p->Local)
        ExFreePool(p->Local);
    if (p->Reports)
        ExFreePool(p->Reports);
    ExFreePool(p);
    return status;
}
