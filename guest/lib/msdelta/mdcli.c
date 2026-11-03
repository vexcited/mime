/*
 * the metadata of managed code. the tables are read as ECMA-335 lays them out, and the transforms move the numbers in them
 * and in the code of the methods to where the target has them.
 */
#include "internal.h"

enum { CLI_U16, CLI_U32, CLI_RVA, CLI_STRING, CLI_BLOB, CLI_GUID, CLI_TABLE, CLI_CODED };

typedef struct COLUMN {
    uint8_t Kind, Param;
} COLUMN;

typedef struct TABLE_SCHEMA {
    uint8_t Count;
    COLUMN Columns[9];
} TABLE_SCHEMA;

/* the columns of each table. a table index column names the table, a coded index column the kind of code, a blob the kind of signature. */
static const TABLE_SCHEMA g_tables[45] = {
    /* Module */ {5, {{0, 0}, {3, 0}, {5, 0}, {5, 0}, {5, 0}}},
    /* TypeRef */ {3, {{7, 11}, {3, 0}, {3, 0}}},
    /* TypeDef */ {6, {{1, 0}, {3, 0}, {3, 0}, {7, 0}, {6, 4}, {6, 6}}},
    /* FieldPtr */ {1, {{6, 4}}},
    /* Field */ {3, {{0, 0}, {3, 0}, {4, 3}}},
    /* MethodPtr */ {1, {{6, 6}}},
    /* MethodDef */ {6, {{2, 0}, {0, 0}, {0, 0}, {3, 0}, {4, 1}, {6, 8}}},
    /* ParamPtr */ {1, {{6, 8}}},
    /* Param */ {3, {{0, 0}, {0, 0}, {3, 0}}},
    /* InterfaceImpl */ {2, {{6, 2}, {7, 0}}},
    /* MemberRef */ {3, {{7, 5}, {3, 0}, {4, 2}}},
    /* Constant */ {3, {{0, 0}, {7, 1}, {4, 0}}},
    /* CustomAttribute */ {3, {{7, 2}, {7, 10}, {4, 6}}},
    /* FieldMarshal */ {2, {{7, 3}, {4, 7}}},
    /* DeclSecurity */ {3, {{0, 0}, {7, 4}, {4, 8}}},
    /* ClassLayout */ {3, {{0, 0}, {1, 0}, {6, 2}}},
    /* FieldLayout */ {2, {{1, 0}, {6, 4}}},
    /* StandAloneSig */ {1, {{4, 9}}},
    /* EventMap */ {2, {{6, 2}, {6, 20}}},
    /* EventPtr */ {1, {{6, 20}}},
    /* Event */ {3, {{0, 0}, {3, 0}, {7, 0}}},
    /* PropertyMap */ {2, {{6, 2}, {6, 23}}},
    /* PropertyPtr */ {1, {{6, 23}}},
    /* Property */ {3, {{0, 0}, {3, 0}, {4, 4}}},
    /* MethodSemantics */ {3, {{0, 0}, {6, 6}, {7, 6}}},
    /* MethodImpl */ {3, {{6, 2}, {7, 7}, {7, 7}}},
    /* ModuleRef */ {1, {{3, 0}}},
    /* TypeSpec */ {1, {{4, 5}}},
    /* ImplMap */ {4, {{0, 0}, {7, 8}, {3, 0}, {6, 26}}},
    /* FieldRVA */ {2, {{2, 0}, {6, 4}}},
    /* EncLog */ {2, {{1, 0}, {1, 0}}},
    /* EncMap */ {1, {{1, 0}}},
    /* Assembly */ {9, {{1, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 0}, {4, 10}, {3, 0}, {3, 0}}},
    /* AssemblyProcessor */ {1, {{1, 0}}},
    /* AssemblyOS */ {3, {{1, 0}, {1, 0}, {1, 0}}},
    /* AssemblyRef */ {9, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 0}, {4, 11}, {3, 0}, {3, 0}, {4, 12}}},
    /* AssemblyRefProcessor */ {2, {{1, 0}, {6, 35}}},
    /* AssemblyRefOS */ {4, {{1, 0}, {1, 0}, {1, 0}, {6, 35}}},
    /* File */ {3, {{1, 0}, {3, 0}, {4, 12}}},
    /* ExportedType */ {5, {{1, 0}, {1, 0}, {3, 0}, {3, 0}, {7, 9}}},
    /* ManifestResource */ {4, {{1, 0}, {1, 0}, {3, 0}, {7, 9}}},
    /* NestedClass */ {2, {{6, 2}, {6, 2}}},
    /* GenericParam */ {4, {{0, 0}, {0, 0}, {7, 12}, {3, 0}}},
    /* MethodSpec */ {2, {{7, 7}, {4, 0}}},
    /* GenericParamConstraint */ {2, {{6, 42}, {7, 0}}},
};

typedef struct CODED_SCHEMA {
    uint8_t Bits, Count;
    uint8_t Tables[22];
} CODED_SCHEMA;

/* the kinds of coded index, with the table that each tag stands for, 64 for a tag that stands for none. */
static const CODED_SCHEMA g_coded[13] = {
    {2, 3, {2, 1, 27}},
    {2, 3, {4, 8, 23}},
    {5, 22, {6, 4, 1, 2, 8, 9, 10, 0, 14, 23, 20, 17, 26, 27, 32, 35, 38, 39, 40, 42, 44, 43}},
    {1, 2, {4, 8}},
    {2, 3, {2, 6, 32}},
    {3, 5, {2, 1, 26, 6, 27}},
    {1, 2, {20, 23}},
    {1, 2, {6, 10}},
    {1, 2, {4, 6}},
    {2, 3, {38, 35, 39}},
    {3, 5, {64, 64, 6, 10, 64}},
    {2, 4, {0, 26, 35, 1}},
    {1, 2, {2, 6}},
};

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t rd64(const uint8_t *p)
{
    return rd32(p) | (uint64_t)rd32(p + 4) << 32;
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    wr16(p, (uint16_t)v);
    wr16(p + 2, (uint16_t)(v >> 16));
}

/* ------------------------------------------------------------------ the layout of the tables */

static bool wide_column(const MD_CLI *cli, const COLUMN *c)
{
    switch (c->Kind) {
    case CLI_U16:
        return false;
    case CLI_U32:
    case CLI_RVA:
        return true;
    case CLI_STRING:
        return cli->WideStrings;
    case CLI_BLOB:
        return cli->WideBlobs;
    case CLI_GUID:
        return cli->WideGuids;
    case CLI_TABLE:
        return cli->Rows[c->Param] >= 0x10000;
    default:
        return (0x10000u >> g_coded[c->Param].Bits) <= cli->MaxCodedRows[c->Param];
    }
}

static bool cli_layout(MD_CLI *cli)
{
    cli->LayoutOk = false;
    for (unsigned c = 0; c < 13; c++) {
        uint32_t most = 0;
        for (unsigned i = 0; i < g_coded[c].Count; i++) {
            unsigned table = g_coded[c].Tables[i];
            if (table != 64 && cli->Rows[table] > most)
                most = cli->Rows[table];
        }
        if ((0xFFFFFFFFu >> g_coded[c].Bits) < most)
            return false;
        cli->MaxCodedRows[c] = most;
    }
    uint32_t cursor = 24;
    for (unsigned t = 0; t < 64; t++)
        if (cli->Valid >> t & 1)
            cursor += 4;
    for (unsigned t = 0; t < 64; t++) {
        cli->RowSize[t] = 0;
        if (!cli->Rows[t])
            continue;
        if (t >= 45 || !g_tables[t].Count)
            return false;
        uint32_t size = 0;
        for (unsigned c = 0; c < g_tables[t].Count; c++) {
            cli->ColumnOffset[t][c] = size;
            cli->WideColumn[t][c] = wide_column(cli, &g_tables[t].Columns[c]);
            size += cli->WideColumn[t][c] ? 4 : 2;
        }
        cli->RowSize[t] = size;
    }
    for (unsigned t = 0; t < 64; t++) {
        if (!cli->Rows[t])
            continue;
        if (cursor > cli->Tables[1] || (cli->Tables[1] - cursor) / cli->RowSize[t] < cli->Rows[t])
            return false;
        cli->TableOffset[t] = cursor + cli->Tables[0];
        cursor += cli->Rows[t] * cli->RowSize[t];
    }
    cli->LayoutOk = true;
    return true;
}

bool MdCliFromImage(MD_CLI *cli, const uint8_t *image, uint32_t size, uint32_t offset, uint32_t length)
{
    memset(cli, 0, sizeof(*cli));
    cli->Offset = offset;
    cli->Size = length;
    cli->Present = offset || length;
    if (length < 0x10 || (uint64_t)offset + length > size || rd32(image + offset) != 0x424A5342)
        return true;
    uint32_t version = rd32(image + offset + 12);
    if (version > 0xFF || length < ((version + 3) & ~3u) + 20)
        return true;
    uint32_t at = offset + ((version + 3) & ~3u) + 20;
    unsigned streams = rd16(image + at - 2);
    for (unsigned i = 0; i < streams; i++) {
        if (at + 8 > offset + length)
            return true;
        uint32_t start = rd32(image + at), stream_size = rd32(image + at + 4);
        if (start > length || length - start < stream_size)
            return true;
        uint32_t name = at + 8, n = 0;
        while (name + n < offset + length && image[name + n])
            n++;
        if (name + n >= offset + length)
            return true;
        const char *text = (const char *)image + name;
        uint32_t *slot = nullptr;
        if (n == 8 && !memcmp(text, "#Strings", 8))
            slot = cli->Strings;
        else if (n == 3 && !memcmp(text, "#US", 3))
            slot = cli->UserStrings;
        else if (n == 5 && !memcmp(text, "#Blob", 5))
            slot = cli->Blobs;
        else if (n == 5 && !memcmp(text, "#GUID", 5))
            slot = cli->Guids;
        else if (n == 2 && (!memcmp(text, "#~", 2) || !memcmp(text, "#-", 2)))
            slot = cli->Tables;
        if (slot) {
            if (slot[0])
                return true;
            slot[0] = offset + start;
            slot[1] = stream_size;
        }
        at = (name + n + 1 + 3) & ~3u;
    }
    if (!cli->Tables[0] || cli->Tables[1] < 24 || (uint64_t)cli->Tables[0] + cli->Tables[1] > size)
        return true;
    const uint8_t *header = image + cli->Tables[0];
    cli->WideStrings = header[6] & 1;
    cli->WideGuids = (header[6] & 2) != 0;
    cli->WideBlobs = (header[6] & 4) != 0;
    cli->Valid = rd64(header + 8);
    uint32_t cursor = 24;
    for (unsigned t = 0; t < 64; t++) {
        if (!(cli->Valid >> t & 1))
            continue;
        if (cli->Tables[1] - cursor < 4)
            return true;
        cli->Rows[t] = rd32(header + cursor);
        cursor += 4;
    }
    cli_layout(cli);
    return true;
}

bool MdCliFromBits(MD_BITS *bits, MD_CLI *cli)
{
    memset(cli, 0, sizeof(*cli));
    if (!MdBitsRead(bits, 1))
        return !bits->Failed;
    uint32_t v[15];
    for (unsigned i = 0; i < 15; i++)
        v[i] = (uint32_t)MdBitsRead(bits, 32);
    cli->Offset = v[0];
    cli->Size = v[1];
    cli->Base = v[2];
    cli->Strings[0] = v[5];
    cli->Strings[1] = v[6];
    cli->UserStrings[0] = v[7];
    cli->UserStrings[1] = v[8];
    cli->Blobs[0] = v[9];
    cli->Blobs[1] = v[10];
    cli->Guids[0] = v[11];
    cli->Guids[1] = v[12];
    cli->Tables[0] = v[13];
    cli->Tables[1] = v[14];
    cli->WideStrings = MdBitsRead(bits, 1) != 0;
    cli->WideGuids = MdBitsRead(bits, 1) != 0;
    cli->WideBlobs = MdBitsRead(bits, 1) != 0;
    cli->Valid = MdBitsRead(bits, 32);
    cli->Valid |= MdBitsRead(bits, 32) << 32;
    for (unsigned t = 0; t < 64; t++)
        if (cli->Valid >> t & 1)
            cli->Rows[t] = (uint32_t)MdBitsRead(bits, 32);
    cli->Present = true;
    if (bits->Failed)
        return false;
    cli_layout(cli);
    return true;
}

/* ------------------------------------------------------------------ the map */

void MdCliMapFree(MD_CLI_MAP *map)
{
    MdRiftFree(&map->Strings);
    MdRiftFree(&map->UserStrings);
    MdRiftFree(&map->Blobs);
    MdRiftFree(&map->Guids);
    for (unsigned t = 0; t < 64; t++)
        MdRiftFree(&map->Tables[t]);
}

bool MdCliMapFromBits(MD_BITS *bits, MD_CLI_MAP *map)
{
    memset(map, 0, sizeof(*map));
    if (!MdBitsRead(bits, 1))
        return !bits->Failed;
    MD_INT_FORMAT formats[4];
    unsigned made = 0;
    bool ok = true;
    for (; ok && made < 4; made++)
        ok = MdIntFormatRead(bits, &formats[made]);
    ok = ok && MdRiftReadWith(bits, &map->Strings, &formats[0], &formats[1]) &&
         MdRiftReadWith(bits, &map->UserStrings, &formats[0], &formats[1]) &&
         MdRiftReadWith(bits, &map->Blobs, &formats[0], &formats[1]) &&
         MdRiftReadWith(bits, &map->Guids, &formats[2], &formats[3]);
    for (unsigned t = 0; ok && t < 64; t++)
        ok = MdRiftReadWith(bits, &map->Tables[t], &formats[2], &formats[3]);
    for (unsigned i = 0; i < made; i++)
        MdHuffmanFree(&formats[i].Huffman);
    if (!ok)
        MdCliMapFree(map);
    return ok;
}

/* a row or a heap position moved by the stretch of the map that holds it, as a number of 32 bits. */
static uint32_t map32(const MD_RIFT *rift, uint64_t value)
{
    return (uint32_t)MdRiftMap(rift, (int64_t)value);
}

static uint32_t map_coded(const MD_CLI_MAP *map, unsigned kind, uint32_t value)
{
    unsigned bits = g_coded[kind].Bits;
    uint32_t tag = value & ((1u << bits) - 1);
    if (tag >= g_coded[kind].Count)
        return value;
    unsigned table = g_coded[kind].Tables[tag];
    if (table == 64)
        return value;
    uint32_t index = value >> bits;
    if (map->Tables[table].Count)
        index = map32(&map->Tables[table], index);
    return tag | index << bits;
}

/* ------------------------------------------------------------------ the code of the methods */

/*
 * the operand of each opcode of the language: its length, 0x40 when it is a token, 0x80 for the switch whose operand is
 * a count and the targets that follow it.
 */
static const uint8_t g_one_byte[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x04, 0x08, 0x04, 0x08, 0x00, 0x00, 0x00, 0x44, 0x44, 0x04, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44,
    0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x00, 0x00, 0x00, 0x44, 0x00, 0x44, 0x44, 0x44, 0x44, 0x44,
    0x44, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x44, 0x00, 0x44,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x44, 0x44, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* the opcodes that follow 0xFE are listed up to 0x1F, the others have no operand. */
static const uint8_t g_two_byte[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x44, 0x00, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x44, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00,
};

/* the file offset and the sizes of the body of a method: the header and the header with the code. */
static uint32_t method_body(const MD_PE *pe, uint32_t rva, uint32_t *total, uint32_t *header)
{
    *total = *header = 0;
    uint32_t offset = MdPeOffset(pe, rva, 0);
    if (!offset)
        return 0;
    uint32_t room = MdPeMaxSize(pe, rva);
    if (!room)
        return 0;
    const uint8_t *p = pe->Image + offset;
    uint32_t head, code;
    if ((p[0] & 3) == 2) {
        head = 1;
        code = p[0] >> 2;
    } else if ((p[0] & 3) == 3) {
        if (room < 12)
            return 0;
        head = 4u * (p[1] >> 4);
        if (head < 12 || head > room)
            return 0;
        code = rd32(p + 4);
    } else
        return 0;
    if (code > room - head)
        return 0;
    *header = head;
    *total = code + head;
    return offset;
}

static uint32_t method_rva(const MD_PE *pe, const MD_CLI *cli, uint32_t row)
{
    const uint8_t *at = pe->Image + cli->TableOffset[6] + cli->RowSize[6] * row + cli->ColumnOffset[6][0];
    return cli->WideColumn[6][0] ? rd32(at) : rd16(at);
}

static bool method_rows_ok(const MD_PE *pe, const MD_CLI *cli)
{
    if (!cli->LayoutOk || cli->Rows[6] < 2)
        return false;
    return (uint64_t)cli->TableOffset[6] + (uint64_t)cli->RowSize[6] * cli->Rows[6] <= pe->Size;
}

bool MdPeCli(const MD_PE *pe, MD_CLI *cli)
{
    memset(cli, 0, sizeof(*cli));
    if (!pe->DirOffset[14] || pe->DirSize[14] < 0x48)
        return true;
    const uint8_t *cor = pe->Image + pe->DirOffset[14];
    uint32_t rva = rd32(cor + 8), length = rd32(cor + 12);
    uint32_t offset = MdPeOffset(pe, rva, length);
    if (!offset)
        return MdCliFromImage(cli, pe->Image, pe->Size, 0, 0);
    return MdCliFromImage(cli, pe->Image, pe->Size, offset, length);
}

void MdPeMarkCliMethods(const MD_PE *pe, const MD_CLI *cli, uint8_t *hint)
{
    if (!method_rows_ok(pe, cli))
        return;
    for (uint32_t row = 0; row + 1 < cli->Rows[6]; row++) {
        uint32_t total, header;
        uint32_t offset = method_body(pe, method_rva(pe, cli, row) + cli->Base, &total, &header);
        if (offset && (uint64_t)offset + total <= pe->Size)
            memset(hint + offset, 3, total);
    }
}

void MdPeCliDisassemble(MD_PE *pe, const MD_CLI *cli, const MD_CLI_MAP *map)
{
    if (!method_rows_ok(pe, cli))
        return;
    for (uint32_t row = 0; row + 1 < cli->Rows[6]; row++) {
        uint32_t total, header;
        uint32_t offset = method_body(pe, method_rva(pe, cli, row) + cli->Base, &total, &header);
        if (!offset || total == header || (uint64_t)offset + total > pe->Size)
            continue;
        uint8_t *p = pe->Image + offset + header;
        uint32_t left = total - header;
        while (left) {
            uint8_t info;
            if (*p == 0xFE) {
                p++;
                if (--left == 0)
                    break;
                info = *p < 32 ? g_two_byte[*p] : 0;
            } else
                info = g_one_byte[*p];
            left--;
            uint8_t *operand = p + 1;
            uint32_t length = info & 0x3F;
            if (info == 0x80)
                length = 4;
            if (length > left)
                break;
            p = operand + length;
            left -= length;
            if (info & 0x40) {
                uint32_t token = rd32(operand);
                uint32_t table = token >> 24;
                if (table >= 0x40) {
                    if (table == 0x70)
                        wr32(operand, map32(&map->UserStrings, token & 0xFFFFFF) | 0x70000000u);
                } else {
                    uint32_t row = token & 0xFFFFFF;
                    if (map->Tables[table].Count)
                        row = map32(&map->Tables[table], row);
                    wr32(operand, row | table << 24);
                }
            } else if (info == 0x80) {
                uint32_t targets = rd32(operand);
                if (left >> 2 < targets)
                    break;
                p += 4u * targets;
                left -= 4u * targets;
            }
            if (!left)
                break;
        }
    }
}

/* ------------------------------------------------------------------ signatures */

/*
 * the blobs that hold signatures are scanned for the numbers of types in them, which are rewritten where they fit. the
 * scans below follow what the format's own tool does with each kind of blob, which is less than the grammar of a
 * signature allows: a scan ends where it meets what it does not know, and a type that takes more than one element
 * leaves the rest of it to be read as the next parameters.
 */
typedef struct SIG {
    uint8_t *End;
    const MD_CLI_MAP *Map;
} SIG;

/* a compressed number whose first byte is at p. false when it does not fit before the end. */
static bool number_at(const SIG *s, const uint8_t *p, uint32_t *value, unsigned *width)
{
    uint8_t b = *p;
    if (!(b & 0x80)) {
        *value = b;
        *width = 1;
    } else if (b & 0x40) {
        if ((b & 0x20) || s->End - (p + 1) < 3)
            return false;
        *value = (uint32_t)(b & 0x1F) << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
        *width = 4;
    } else {
        if (s->End == p + 1)
            return false;
        *value = (uint32_t)(b & 0x3F) << 8 | p[1];
        *width = 2;
    }
    return true;
}

static uint8_t *skip_number(const SIG *s, uint8_t *p)
{
    uint32_t v;
    unsigned w;
    if (p == s->End || !number_at(s, p, &v, &w))
        return nullptr;
    return p + w;
}

/* a type token of a signature at p, moved to where the target has the type. the token keeps its width. */
static uint8_t *token_at(const SIG *s, uint8_t *p)
{
    uint32_t v;
    unsigned w;
    if (p == s->End || !number_at(s, p, &v, &w))
        return nullptr;
    uint32_t tag = v & 3, mapped = v;
    if (tag == 0 || tag == 1)
        mapped = 4 * map32(&s->Map->Tables[2], v >> 2) | tag;
    else if (tag == 2)
        mapped = 4 * map32(&s->Map->Tables[27], v >> 2) | 2;
    if (mapped < 0x80) {
        p[0] = (uint8_t)(mapped & 0x7F);
    } else if (mapped >= 0x4000) {
        if (mapped < 0x20000000 && w >= 4) {
            p[0] = (uint8_t)(mapped >> 24 | 0xC0);
            p[1] = (uint8_t)(mapped >> 16);
            p[2] = (uint8_t)(mapped >> 8);
            p[3] = (uint8_t)mapped;
        }
    } else if (w >= 2) {
        p[0] = (uint8_t)(mapped >> 8 | 0x80);
        p[1] = (uint8_t)mapped;
    }
    return p + w;
}

/* the shapes of arrays that were met, one after the other: the rank, the sizes and the lower bounds. */
static uint8_t *skip_shapes(const SIG *s, uint8_t *p, unsigned arrays)
{
    while (arrays--) {
        if (!(p = skip_number(s, p)))
            return nullptr;
        for (unsigned part = 0; part < 2; part++) {
            uint32_t count;
            unsigned width;
            if (p == s->End || !number_at(s, p, &count, &width))
                return nullptr;
            p += width;
            while (count--)
                if (!(p = skip_number(s, p)))
                    return nullptr;
        }
    }
    return p;
}

static bool is_mark(uint8_t b)
{
    return (b & 0xF0) == 0x40;
}

static bool is_modifier(uint8_t b)
{
    return b == 0x1F || b == 0x20;
}

/* the elements that need nothing: the simple types. */
static bool is_simple(uint8_t b)
{
    return b < 0x1A && (uint8_t)(b - 14) > 9;
}

/* the modifiers after a pointer or an array of one element, up to what follows them. false ends the scan. */
static bool pointer_modifiers(const SIG *s, uint8_t **at)
{
    uint8_t *p = *at;
    for (;;) {
        if (p == s->End)
            return false;
        if (is_mark(*p)) {
            p++;
            continue;
        }
        if (!is_modifier(*p))
            break;
        if (!(p = token_at(s, p + 1)))
            return false;
    }
    *at = p;
    return true;
}

/* the first type of a blob that holds one: a field, a type, a local variable. the scan ends at the first token. */
static void scan_one_type(const SIG *s, uint8_t *p, bool skip_first_mod_marks)
{
    (void)skip_first_mod_marks;
    for (;;) {
        if (p == s->End)
            return;
        uint8_t b = *p++;
        if (is_simple(b))
            return;
        if (b == 17 || b == 18) {
            token_at(s, p);
            return;
        }
        if (b <= 0x1E && (0x50084000u >> b & 1))
            return;
        if (b == 20 || b == 21)
            continue;
        if (b != 15 && b != 29)
            return;
        if (!pointer_modifiers(s, &p))
            return;
        if (*p == 1)
            return;
    }
}

static void scan_field(const SIG *s, uint8_t *p)
{
    p++;
    for (;;) {
        while (p != s->End && is_mark(*p))
            p++;
        if (p == s->End)
            return;
        if (!is_modifier(*p)) {
            scan_one_type(s, p, false);
            return;
        }
        if (!(p = token_at(s, p + 1)))
            return;
    }
}

static void scan_local_variables(const SIG *s, uint8_t *p)
{
    p++;
    if (p == s->End)
        return;
    if (!(p = skip_number(s, p)) || p == s->End)
        return;
    while (p != s->End && is_mark(*p))
        p++;
    if (p == s->End)
        return;
    if (*p == 16)
        p++;
    scan_one_type(s, p, false);
}

/* a stretch of elements, as the parameters of a property or what follows the first one: no end but the blob's. */
static void scan_elements(const SIG *s, uint8_t *p)
{
    unsigned arrays = 0, saved = 0;
    bool generic = false;
    for (;;) {
        if (p == s->End)
            return;
        uint8_t b = *p++;
        if (!b)
            return;
        if (is_simple(b)) {
            /* nothing to move. */
        } else if (b == 17 || b == 18) {
            if (p == s->End || !(p = token_at(s, p)))
                return;
        } else if (b == 19 || b == 30) {
            if (p == s->End || !(p = skip_number(s, p)))
                return;
        } else if (b == 14 || b == 28) {
            /* nothing to move. */
        } else if (b == 21) {
            generic = true;
            continue;
        } else if (b == 20) {
            saved = ++arrays;
            continue;
        } else if (b == 15 || b == 29) {
            for (;;) {
                if (p == s->End)
                    return;
                if (is_mark(*p)) {
                    p++;
                    continue;
                }
                if (!is_modifier(*p))
                    break;
                if (!(p = token_at(s, p + 1)))
                    return;
            }
            arrays = saved;
            if (*p != 1)
                continue;
            p++;
        } else {
            return;
        }
        if (!generic) {
            if (!(p = skip_shapes(s, p, arrays)))
                return;
            arrays = 0;
        }
    }
}

static void scan_typespec(const SIG *s, uint8_t *p)
{
    for (;;) {
        uint8_t b;
        do {
            if (p == s->End)
                return;
            b = *p++;
            if (is_simple(b))
                return;
            if (b == 17 || b == 18) {
                token_at(s, p);
                return;
            }
            if (b <= 0x1E && (0x50084000u >> b & 1))
                return;
        } while (b == 21);
        if (b != 15) {
            if (b == 20)
                continue;
            if (b != 29)
                return;
        }
        if (!pointer_modifiers(s, &p))
            return;
        if (*p == 1)
            return;
    }
}

static void scan_property(const SIG *s, uint8_t *p)
{
    p++;
    if (p == s->End)
        return;
    uint32_t count;
    unsigned width;
    if (!number_at(s, p, &count, &width))
        return;
    p += width;
    /* the type of the property. */
    unsigned arrays = 0, saved = 0;
    bool generic = false;
    for (;;) {
        if (p == s->End)
            return;
        uint8_t b = *p++;
        if (!b)
            return;
        if (is_simple(b) || b == 14 || b == 28) {
            break;
        } else if (b == 17 || b == 18) {
            if (p == s->End || !(p = token_at(s, p)))
                return;
            break;
        } else if (b == 19 || b == 30) {
            if (p == s->End || !(p = skip_number(s, p)))
                return;
            break;
        } else if (b == 21) {
            generic = true;
        } else if (b == 20) {
            saved = ++arrays;
        } else if (b == 15 || b == 29) {
            if (!pointer_modifiers(s, &p))
                return;
            arrays = saved;
            if (*p == 1) {
                p++;
                break;
            }
        } else {
            return;
        }
    }
    if (!generic && !(p = skip_shapes(s, p, arrays)))
        return;
    /* then the parameters, of which the first one takes the rest of the blob. */
    for (;;) {
        if (count-- == 0)
            return;
        for (;;) {
            while (p != s->End && is_mark(*p))
                p++;
            if (p != s->End && is_modifier(*p)) {
                if (!(p = token_at(s, p + 1)))
                    return;
                continue;
            }
            break;
        }
        if (p != s->End) {
            if (*p == 22 || *p == 1)
                continue;
            if (*p == 16)
                p++;
        }
        scan_elements(s, p);
        return;
    }
}

static void scan_method(const SIG *s, uint8_t *p)
{
    if (p == s->End)
        return;
    if (*p == 6) {
        scan_field(s, p);
        return;
    }
    p++;
    if (p == s->End)
        return;
    uint32_t count;
    unsigned width;
    if (!number_at(s, p, &count, &width))
        return;
    p += width;
    for (;;) {
        /* the marks and modifiers before the type of a parameter. */
        bool done = false;
        while (p != s->End) {
            if (is_mark(*p)) {
                p++;
                continue;
            }
            if (!is_modifier(*p)) {
                if (*p == 22 || *p == 1) {
                    done = true;
                } else if (*p == 16)
                    p++;
                break;
            }
            if (p + 1 == s->End || !(p = token_at(s, p + 1)))
                return;
        }
        if (!done) {
            unsigned arrays = 0;
            bool generic = false, end_of_type = false;
            while (!end_of_type) {
                if (p == s->End)
                    return;
                uint8_t b = *p++;
                if (!b)
                    return;
                if (is_simple(b) || b == 14 || b == 28) {
                    end_of_type = true;
                } else if (b == 17 || b == 18) {
                    if (p == s->End || !(p = token_at(s, p)))
                        return;
                    end_of_type = true;
                } else if (b == 19 || b == 30 || b == 21) {
                    generic = generic || b == 21;
                    if (p == s->End)
                        return;
                    uint8_t first = *p++;
                    if (first < 0x80) {
                    } else if (!(first & 0x40)) {
                        if (p == s->End)
                            return;
                        p++;
                    } else if (!(first & 0x20) && s->End - p >= 3) {
                        p += 3;
                    } else
                        return;
                    end_of_type = true;
                } else if (b == 20) {
                    arrays++;
                } else if (b == 15 || b == 29) {
                    unsigned saved = arrays;
                    for (;;) {
                        if (p == s->End)
                            return;
                        if (is_mark(*p)) {
                            p++;
                            continue;
                        }
                        if (!is_modifier(*p))
                            break;
                        if (p + 1 == s->End || !(p = token_at(s, p + 1)))
                            return;
                    }
                    arrays = saved;
                    if (*p == 1) {
                        p++;
                        end_of_type = true;
                    }
                } else {
                    return;
                }
            }
            if (!generic && !(p = skip_shapes(s, p, arrays)))
                return;
        }
        if (count-- == 0)
            return;
    }
}

/* ------------------------------------------------------------------ the tables */

typedef struct BLOB_USE {
    uint32_t Offset;
    uint8_t Kind;
} BLOB_USE;

static int blob_compare(const BLOB_USE *a, const BLOB_USE *b)
{
    return a->Offset < b->Offset ? -1 : a->Offset > b->Offset;
}

/* a stable sort by position of the uses of the blobs. */
static void blobs_sort(BLOB_USE *uses, size_t count)
{
    for (size_t i = 1; i < count; i++) {
        BLOB_USE use = uses[i];
        size_t j = i;
        while (j > 0 && blob_compare(&uses[j - 1], &use) > 0) {
            uses[j] = uses[j - 1];
            j--;
        }
        uses[j] = use;
    }
}

void MdPeCliMetadata(MD_PE *pe, const MD_CLI *cli, const MD_CLI_MAP *map, const MD_RIFT *used, const MD_CLI *target)
{
    if (!cli->Present || !cli->LayoutOk)
        return;
    uint8_t *image = pe->Image;
    BLOB_USE *uses = nullptr;
    size_t use_count = 0, use_capacity = 0;
    for (unsigned t = 0; t < 45; t++) {
        uint32_t rows = cli->Rows[t];
        if (!rows)
            continue;
        if ((uint64_t)cli->TableOffset[t] + (uint64_t)cli->RowSize[t] * rows > pe->Size)
            continue;
        for (unsigned c = 0; c < g_tables[t].Count; c++) {
            const COLUMN *column = &g_tables[t].Columns[c];
            bool wide = cli->WideColumn[t][c];
            for (uint32_t r = 0; r < rows; r++) {
                uint8_t *at = image + cli->TableOffset[t] + cli->RowSize[t] * r + cli->ColumnOffset[t][c];
                uint32_t v = wide ? rd32(at) : rd16(at);
                uint32_t n = v;
                switch (column->Kind) {
                case CLI_RVA:
                    n = (uint32_t)MdRiftMap(used, (int64_t)(uint64_t)(v + cli->Base)) - target->Base;
                    break;
                case CLI_STRING:
                    n = map32(&map->Strings, v);
                    break;
                case CLI_BLOB:
                    if (use_count == use_capacity) {
                        use_capacity = use_capacity ? use_capacity * 2 : 1024;
                        BLOB_USE *grown = MdRealloc(uses, use_capacity * sizeof(*uses));
                        if (!grown) {
                            MsDeltaFree(uses);
                            return;
                        }
                        uses = grown;
                    }
                    uses[use_count++] = (BLOB_USE){v, column->Param};
                    n = map32(&map->Blobs, v);
                    break;
                case CLI_GUID:
                    n = map32(&map->Guids, v);
                    break;
                case CLI_TABLE:
                    if (map->Tables[column->Param].Count)
                        n = map32(&map->Tables[column->Param], v);
                    break;
                case CLI_CODED:
                    n = map_coded(map, column->Param, v);
                    break;
                default:
                    break;
                }
                if (wide)
                    wr32(at, n);
                else
                    wr16(at, (uint16_t)n);
            }
        }
    }
    blobs_sort(uses, use_count);
    uint32_t previous = 0;
    for (size_t i = 0; i < use_count; i++) {
        uint32_t offset = uses[i].Offset;
        if (!offset || offset == previous)
            continue;
        previous = offset;
        if (offset >= cli->Blobs[1])
            continue;
        uint8_t *p = image + cli->Blobs[0] + offset;
        uint32_t room = cli->Blobs[1] - offset, length, width;
        uint8_t b = p[0];
        if (!(b & 0x80)) {
            width = 1;
            length = b;
        } else if (!(b & 0x40)) {
            if (room < 2)
                continue;
            width = 2;
            length = (uint32_t)(b & 0x3F) << 8 | p[1];
        } else {
            if ((b & 0x20) || room < 4)
                continue;
            width = 4;
            length = (uint32_t)(b & 0x1F) << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
        }
        if (room - width < length || (uint64_t)cli->Blobs[0] + offset + width + length > pe->Size)
            continue;
        SIG sig = {p + width + length, map};
        uint8_t *blob = p + width;
        if (blob == sig.End)
            continue;
        switch (uses[i].Kind) {
        case 1:
        case 2:
            scan_method(&sig, blob);
            break;
        case 3:
            scan_field(&sig, blob);
            break;
        case 4:
            scan_property(&sig, blob);
            break;
        case 5:
            scan_typespec(&sig, blob);
            break;
        case 9:
            if (*blob == 7)
                scan_local_variables(&sig, blob);
            break;
        default:
            break;
        }
    }
    MsDeltaFree(uses);
}

/* ------------------------------------------------------------------ the rift table of the metadata */

static bool add_pair(MD_RIFT *out, int64_t left, int64_t right)
{
    return MdRiftAdd(out, left, right);
}

/* a heap whose entries are bytes: where each stretch of the map begins in the source and in the target. */
static bool heap_map(MD_RIFT *out, int64_t source, int64_t target, const MD_RIFT *map)
{
    if (!map->Count)
        return add_pair(out, target, source);
    size_t k = 0, last;
    do {
        last = map->Count;
        if (map->Entries[k].Left > 0)
            break;
        k++;
    } while (map->Count > k);
    if (k)
        last = k;
    const MD_RIFT_ENTRY *e = map->Entries;
    if (!add_pair(out, target - e[last - 1].Left + e[last - 1].Right, source))
        return false;
    for (; k < map->Count && (uint64_t)e[k].Left <= 0xFFFFFFFFull; k++)
        if (!add_pair(out, target + e[k].Right, source + e[k].Left))
            return false;
    return true;
}

/* the same for entries of a fixed size that count from one. */
static bool fixed_map(MD_RIFT *out, int64_t source, int64_t target, int64_t size, const MD_RIFT *map)
{
    if (!map->Count)
        return add_pair(out, target, source);
    size_t k = 0, last;
    do {
        last = map->Count;
        if (map->Entries[k].Left > 1)
            break;
        k++;
    } while (map->Count > k);
    if (k)
        last = k;
    const MD_RIFT_ENTRY *e = map->Entries;
    if (!add_pair(out, target + size * (e[last - 1].Right - e[last - 1].Left), source))
        return false;
    for (; k < map->Count && (uint64_t)e[k].Left <= 0xFFFFFFFFull; k++)
        if (!add_pair(out, target + size * (e[k].Right - 1), source + size * (e[k].Left - 1)))
            return false;
    return true;
}

/* the rows of a table whose layout is not the same in the source and the target, column by column. */
static bool table_map(MD_RIFT *out, const MD_CLI *source, const MD_CLI *target, unsigned t, int64_t zero,
                      const MD_RIFT *map)
{
    int64_t source_start = source->TableOffset[t], target_start = target->TableOffset[t];
    int64_t source_size = source->RowSize[t], target_size = target->RowSize[t];
    int64_t last;
    int64_t shift = MdRiftGetRift(map, 0, &last);
    for (int64_t row = 1; row < (int64_t)source->Rows[t]; row++) {
        if (row > last)
            shift = MdRiftGetRift(map, row, &last);
        if (!add_pair(out, target_start + target_size * (shift + row - 1), source_start + source_size * (row - 1)))
            return false;
    }
    unsigned columns = g_tables[t].Count;
    for (unsigned c = 0; c < columns; c++) {
        int64_t source_column = source_start + source->ColumnOffset[t][c];
        int64_t target_column = target_start + target->ColumnOffset[t][c];
        bool source_wide = source->WideColumn[t][c], target_wide = target->WideColumn[t][c];
        if (source_wide == target_wide)
            continue;
        bool more = columns > c + 1;
        last = 0;
        shift = MdRiftGetRift(map, 0, &last);
        for (int64_t row = 1; row < (int64_t)source->Rows[t]; row++) {
            if (row > last)
                shift = MdRiftGetRift(map, row, &last);
            int64_t target_row = target_column + target_size * (shift + row - 1);
            int64_t source_row = source_column + source_size * (row - 1);
            if (source_wide) {
                if (more && !add_pair(out, target_row + 2, source_row + 4))
                    return false;
            } else if (!more) {
                if (!add_pair(out, target_row + 2, zero))
                    return false;
            } else {
                if (!add_pair(out, target_row + 2, zero) || !add_pair(out, target_row + 4, source_row + 2))
                    return false;
            }
        }
    }
    return true;
}

bool MdPeCliCompressionRift(const MD_CLI *source, const MD_CLI *target, const MD_CLI_MAP *map, const MD_PE *transformed,
                            MD_RIFT *out)
{
    memset(out, 0, sizeof(*out));
    if (!source->Present || !target->Present)
        return true;
    /* a place in the image with two zero bytes stands for the high half of a number that grew. */
    int64_t zero = transformed->Size;
    for (uint32_t i = 0; i + 1 < transformed->Size; i++) {
        if (rd16(transformed->Image + i) == 0) {
            zero = i;
            break;
        }
    }
    bool ok = heap_map(out, source->Strings[0], target->Strings[0], &map->Strings) &&
              heap_map(out, source->UserStrings[0], target->UserStrings[0], &map->UserStrings) &&
              heap_map(out, source->Blobs[0], target->Blobs[0], &map->Blobs) &&
              fixed_map(out, source->Guids[0], target->Guids[0], 16, &map->Guids);
    for (unsigned t = 0; ok && t < 64; t++) {
        if (!source->Rows[t] || !target->Rows[t])
            continue;
        bool same = source->RowSize[t] == target->RowSize[t];
        for (unsigned c = 0; same && c < g_tables[t].Count; c++)
            same = source->WideColumn[t][c] == target->WideColumn[t][c];
        if (same)
            ok = fixed_map(out, source->TableOffset[t], target->TableOffset[t], source->RowSize[t], &map->Tables[t]);
        else
            ok = table_map(out, source, target, t, zero, &map->Tables[t]);
    }
    if (!ok) {
        MdRiftFree(out);
        return false;
    }
    MdRiftSort(out);
    return true;
}

