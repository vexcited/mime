#include "tool.h"

void tu_table_init(TU_TABLE *t, const TU_COLUMN *columns, int count)
{
    memset(t, 0, sizeof(*t));
    t->columns = columns;
    t->count = count;
}

void tu_table_row(TU_TABLE *t, const WCHAR *const *cells)
{
    if (t->rows_count == t->rows_capacity) {
        t->rows_capacity = t->rows_capacity ? t->rows_capacity * 2 : 64;
        t->rows = tu_realloc(t->rows, t->rows_capacity * sizeof(WCHAR **));
    }
    WCHAR **row = tu_alloc(t->count * sizeof(WCHAR *));
    for (int i = 0; i < t->count; i++)
        row[i] = tu_dup(cells[i] ? cells[i] : L"");
    t->rows[t->rows_count++] = row;
}

void tu_table_free(TU_TABLE *t)
{
    for (SIZE_T r = 0; r < t->rows_count; r++) {
        for (int i = 0; i < t->count; i++)
            tu_free(t->rows[r][i]);
        tu_free(t->rows[r]);
    }
    tu_free(t->rows);
    memset(t, 0, sizeof(*t));
}

int tu_format_by_name(const WCHAR *text)
{
    if (tu_eq(text, L"table"))
        return TU_FMT_TABLE;
    if (tu_eq(text, L"list"))
        return TU_FMT_LIST;
    if (tu_eq(text, L"csv"))
        return TU_FMT_CSV;
    return -1;
}

static void put_cell(TU_STR *out, const WCHAR *cell, int width, BOOL right, BOOL last)
{
    int n = (int)wcslen(cell);
    if (width && n > width)
        n = width;
    if (right && width)
        for (int i = n; i < width; i++)
            tu_str_putc(out, L' ');
    tu_str_append(out, cell, n);
    if (!right && !last && width)
        for (int i = n; i < width; i++)
            tu_str_putc(out, L' ');
}

void tu_table_print(const TU_TABLE *t, int format, BOOL no_header)
{
    TU_STR out = {};
    if (format == TU_FMT_CSV) {
        if (!no_header) {
            for (int i = 0; i < t->count; i++) {
                if (i)
                    tu_str_putc(&out, L',');
                tu_str_format(&out, L"\"%s\"", t->columns[i].title);
            }
            tu_str_putc(&out, L'\n');
        }
        for (SIZE_T r = 0; r < t->rows_count; r++) {
            for (int i = 0; i < t->count; i++) {
                if (i)
                    tu_str_putc(&out, L',');
                tu_str_putc(&out, L'"');
                for (const WCHAR *p = t->rows[r][i]; *p; p++) {
                    if (*p == L'"')
                        tu_str_putc(&out, L'"');
                    tu_str_putc(&out, *p);
                }
                tu_str_putc(&out, L'"');
            }
            tu_str_putc(&out, L'\n');
        }
    } else if (format == TU_FMT_LIST) {
        int label = 0;
        for (int i = 0; i < t->count; i++) {
            int n = (int)wcslen(t->columns[i].title) + 1;
            if (n > label)
                label = n;
        }
        for (SIZE_T r = 0; r < t->rows_count; r++) {
            tu_str_putc(&out, L'\n');
            for (int i = 0; i < t->count; i++) {
                tu_str_format(&out, L"%s:", t->columns[i].title);
                for (int k = (int)wcslen(t->columns[i].title) + 1; k < label + 1; k++)
                    tu_str_putc(&out, L' ');
                tu_str_format(&out, L"%s\n", t->rows[r][i]);
            }
        }
    } else {
        int *widths = tu_alloc(t->count * sizeof(int));
        for (int i = 0; i < t->count; i++) {
            widths[i] = t->columns[i].width;
            if (!widths[i]) {
                widths[i] = (int)wcslen(t->columns[i].title);
                for (SIZE_T r = 0; r < t->rows_count; r++) {
                    int n = (int)wcslen(t->rows[r][i]);
                    if (n > widths[i])
                        widths[i] = n;
                }
            }
        }
        if (!no_header) {
            for (int i = 0; i < t->count; i++) {
                if (i)
                    tu_str_putc(&out, L' ');
                put_cell(&out, t->columns[i].title, widths[i], t->columns[i].right, i == t->count - 1);
            }
            tu_str_putc(&out, L'\n');
            for (int i = 0; i < t->count; i++) {
                if (i)
                    tu_str_putc(&out, L' ');
                for (int k = 0; k < widths[i]; k++)
                    tu_str_putc(&out, L'=');
            }
            tu_str_putc(&out, L'\n');
        }
        for (SIZE_T r = 0; r < t->rows_count; r++) {
            for (int i = 0; i < t->count; i++) {
                if (i)
                    tu_str_putc(&out, L' ');
                put_cell(&out, t->rows[r][i], widths[i], t->columns[i].right, i == t->count - 1);
            }
            tu_str_putc(&out, L'\n');
        }
        tu_free(widths);
    }
    tu_write(TU_STDOUT, out.data ? out.data : L"", out.length);
    tu_str_free(&out);
}
