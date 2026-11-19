/*
 * writes the nls files of System32 that the kernel maps for NtGetNlsSectionPtr and NtInitializeNlsFiles, in the
 * layout of windows 11 26300, from the tables of gen: C_<page>.NLS of every single and double byte code page,
 * l_intl.nls, the case tables, and locale.nls and the norm*.nls of the normalization forms, whole.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef int32_t INT32;
typedef const wchar_t *PCWSTR;
#define RTL_NUMBER_OF(a) (sizeof(a) / sizeof((a)[0]))

#include "nls_cpdata.h"
#include "nls_case.h"
#include "nls_cpfit.h"
#include "nls_files.h"

static const char *g_directory;
/* set by --plain: the files as the tables of nls_cpdata.h alone give them, which tools/nls/gencpfit.py compares. */
static int g_plain;

static void write_file(const char *name, const void *data, size_t size)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", g_directory, name);
    FILE *file = fopen(path, "wb");
    if (!file || fwrite(data, 1, size, file) != size || fclose(file)) {
        fprintf(stderr, "nlsc: cannot write %s\n", path);
        exit(1);
    }
}

/* ------------------------------------------------------------------ code pages */

/* the characters the control codes of an oem code page show as. */
static const UINT16 g_glyphs[32] = {
    0x0000, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022, 0x25D8, 0x25CB, 0x25D9,
    0x2642, 0x2640, 0x266A, 0x266B, 0x263C, 0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7,
    0x25AC, 0x21A8, 0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC,
};

static UINT16 single_to_wide(const NLS_CPDATA *p, unsigned b)
{
    return g_nls_to_wide[p->ToWide + b];
}

static int is_lead(const NLS_CPDATA *p, unsigned b)
{
    return p->Kind != NLS_KIND_SBCS && g_nls_lead_row[p->LeadRow + b] != 0;
}

static UINT16 pair_to_wide(const NLS_CPDATA *p, unsigned lead, unsigned trail)
{
    unsigned row = g_nls_lead_row[p->LeadRow + lead];
    if (!row || trail < p->TrailMin || trail > p->TrailMax)
        return 0xFFFF;
    return g_nls_rows[p->Rows + (row - 1) * (p->TrailMax - p->TrailMin + 1) + (trail - p->TrailMin)];
}

static long wide_to_code(const NLS_CPDATA *p, unsigned c)
{
    unsigned block = g_nls_dir[p->Dir + (c >> 8)];
    if (block == 0xFFFF)
        return -1;
    unsigned v = p->Kind == NLS_KIND_SBCS ? g_nls_pages8[block * 256 + (c & 255)] : g_nls_pages16[block * 256 + (c & 255)];
    return (!v && c) ? -1 : (long)v;
}

/*
 * a file is words: the header of 13, the distance to the table of the characters, the 256 characters of the bytes,
 * the glyphs of an oem page behind their count, the count of lead byte ranges with the tables of the lead bytes,
 * then 0 and a byte for each character, or 4 and a word for each on a double byte page.
 */
static const NLS_CPFIT *fit_of(unsigned id)
{
    for (size_t i = 0; i < RTL_NUMBER_OF(g_nls_cpfit); i++)
        if (g_nls_cpfit[i].Id == id)
            return &g_nls_cpfit[i];
    return nullptr;
}

static void code_page(const NLS_CPDATA *p)
{
    const NLS_CPFIT *fit = g_plain ? nullptr : fit_of(p->Id);
    /* windows has no file for the page. */
    if (!g_plain && !fit)
        return;
    static UINT16 words[13 + 1 + 256 + 1 + 256 + 1 + 256 + 256 * 256 + 1 + 65536];
    size_t n = 0;
    int dbcs = p->Kind == NLS_KIND_DBCS;
    long fallback = wide_to_code(p, p->UnicodeDefault);
    words[n++] = 13;
    words[n++] = p->Id;
    words[n++] = dbcs ? 2 : 1;
    words[n++] = p->DefaultChar;
    words[n++] = p->UnicodeDefault;
    words[n++] = p->DefaultChar > 0xFF ? pair_to_wide(p, p->DefaultChar >> 8, p->DefaultChar & 255)
                                       : single_to_wide(p, p->DefaultChar);
    words[n++] = fallback < 0 ? p->DefaultChar : (UINT16)fallback;
    for (unsigned i = 0; i < 12; i += 2)
        words[n++] = (UINT16)(p->LeadBytes[i] | p->LeadBytes[i + 1] << 8);
    size_t distance = n++;
    for (unsigned b = 0; b < 256; b++)
        words[n++] = is_lead(p, b) ? 0 : single_to_wide(p, b);
    if (p->Flags & (NLS_CP_GLYPHS | NLS_CP_GLYPH_DEL)) {
        words[n++] = 256;
        for (unsigned b = 0; b < 256; b++) {
            UINT16 c = is_lead(p, b) ? 0 : single_to_wide(p, b);
            if ((p->Flags & NLS_CP_GLYPHS) && b && b < 0x20)
                c = g_glyphs[b];
            if (b == 0x7F)
                c = 0x2302;
            words[n++] = c;
        }
    } else {
        words[n++] = 0;
    }
    if (dbcs) {
        unsigned ranges = 0, leads = 0;
        while (ranges < 6 && p->LeadBytes[ranges * 2])
            ranges++;
        words[n++] = (UINT16)ranges;
        size_t offsets = n;
        n += 256;
        for (unsigned lead = 0; lead < 256; lead++) {
            if (!is_lead(p, lead)) {
                words[offsets + lead] = 0;
                continue;
            }
            words[offsets + lead] = (UINT16)(256 * ++leads);
            for (unsigned trail = 0; trail < 256; trail++) {
                UINT16 c = pair_to_wide(p, lead, trail);
                words[n++] = c == 0xFFFF ? p->UnicodeDefault : c;
            }
        }
        for (unsigned i = 0; fit && i < fit->PairCount; i++) {
            const UINT16 *pair = &g_nls_cpfit_pairs[(fit->Pairs + i) * 2];
            words[offsets + words[offsets + (pair[0] >> 8)] + (pair[0] & 255)] = pair[1];
        }
    } else {
        words[n++] = 0;
    }
    words[distance] = (UINT16)(n - distance);
    words[n++] = dbcs ? 4 : 0;
    size_t bytes = n * 2;
    if (dbcs) {
        for (unsigned c = 0; c < 65536; c++) {
            long code = wide_to_code(p, c);
            words[n++] = code < 0 ? p->DefaultChar : (UINT16)code;
        }
        for (unsigned i = 0; fit && i < fit->CodeCount; i++)
            words[n - 65536 + g_nls_cpfit_codes[(fit->Codes + i) * 2]] = g_nls_cpfit_codes[(fit->Codes + i) * 2 + 1];
        bytes = n * 2;
    } else {
        UINT8 *out = (UINT8 *)(words + n);
        for (unsigned c = 0; c < 65536; c++) {
            long code = wide_to_code(p, c);
            out[c] = code < 0 ? (UINT8)p->DefaultChar : (UINT8)code;
        }
        for (unsigned i = 0; fit && i < fit->CodeCount; i++)
            out[g_nls_cpfit_codes[(fit->Codes + i) * 2]] = (UINT8)g_nls_cpfit_codes[(fit->Codes + i) * 2 + 1];
        bytes += 65536;
    }
    char name[32];
    snprintf(name, sizeof(name), "C_%03u.NLS", p->Id);
    write_file(name, words, bytes);
}

/* ------------------------------------------------------------------ case tables */

/*
 * a case table is its size in words, then three levels: a word for each high byte that leads to sixteen words for the
 * next four bits, which lead to sixteen differences for the last four. the blocks come in the order of the characters
 * behind the one of no change, which stands for every block no run touches.
 */
static size_t case_table(UINT16 *out, const NLS_CASE_RANGE *ranges, size_t count)
{
    static UINT16 low[4097][16];
    static unsigned low_of[4096], middle[257][16], middle_of[256];
    unsigned low_count = 1, middle_count = 1;
    memset(low[0], 0, sizeof(low[0]));
    memset(middle[0], 0, sizeof(middle[0]));
    memset(low_of, 0, sizeof(low_of));
    /* a block is written when a run holds one of its characters, even a run of no change. */
    for (size_t i = 0; i < count; i++) {
        for (unsigned c = ranges[i].First; c <= ranges[i].Last; c += ranges[i].Step) {
            if (!low_of[c >> 4]) {
                memset(low[low_count], 0, sizeof(low[0]));
                low_of[c >> 4] = low_count++;
            }
            low[low_of[c >> 4]][c & 15] = ranges[i].Delta;
        }
    }
    for (unsigned high = 0; high < 256; high++) {
        unsigned found = memcmp(middle[0], &low_of[high * 16], sizeof(middle[0])) ? middle_count : 0;
        if (found == middle_count)
            memcpy(middle[middle_count++], &low_of[high * 16], sizeof(middle[0]));
        middle_of[high] = found;
    }
    size_t n = 0;
    unsigned middle_base = 256, low_base = 256 + middle_count * 16;
    out[n++] = (UINT16)(1 + low_base + low_count * 16);
    for (unsigned high = 0; high < 256; high++)
        out[n++] = (UINT16)(middle_base + middle_of[high] * 16);
    for (unsigned i = 0; i < middle_count; i++)
        for (unsigned k = 0; k < 16; k++)
            out[n++] = (UINT16)(low_base + middle[i][k] * 16);
    for (unsigned i = 0; i < low_count; i++)
        for (unsigned k = 0; k < 16; k++)
            out[n++] = low[i][k];
    return n;
}

static void case_tables(void)
{
    static UINT16 words[1 + 2 * (1 + 256 + 256 * 16 + 4096 * 16)];
    size_t n = 0;
    words[n++] = 1;
    n += case_table(words + n, g_nls_case_upper, RTL_NUMBER_OF(g_nls_case_upper));
    n += case_table(words + n, g_nls_case_lower, RTL_NUMBER_OF(g_nls_case_lower));
    write_file("l_intl.nls", words, n * 2);
}

/* ------------------------------------------------------------------ whole files */

/* locale.nls and the normalization forms are the bytes of windows: every part of them is read by the system. */
static void whole_files(void)
{
    for (size_t i = 0; i < RTL_NUMBER_OF(g_nls_files); i++)
        write_file(g_nls_files[i].Name, g_nls_files[i].Words, g_nls_files[i].Size);
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--plain")) {
        g_plain = 1;
        argv++;
        argc--;
    }
    if (argc != 2) {
        fprintf(stderr, "usage: nlsc [--plain] <directory>\n");
        return 1;
    }
    g_directory = argv[1];
    for (size_t i = 0; i < RTL_NUMBER_OF(g_nls_codepages); i++)
        if (g_nls_codepages[i].Kind == NLS_KIND_SBCS || g_nls_codepages[i].Kind == NLS_KIND_DBCS)
            code_page(&g_nls_codepages[i]);
    case_tables();
    if (!g_plain)
        whole_files();
    return 0;
}
