/*
 * the small library the console tools of System32 share: the command line, buffered text output that reaches a
 * console or a redirected handle in the right encoding, a text formatter, line input and a few string helpers.
 *
 * the library owns the entry point. a tool defines `int tool_main(void)` and reads tu_argc and tu_argv, which hold the
 * words of the command line the way the c runtime cuts it. the value returned is the exit code.
 */
#ifndef MIME_TOOLUTIL_H
#define MIME_TOOLUTIL_H

#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>
#include <mime/ntapi.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ the process */

extern int tu_argc;
extern WCHAR **tu_argv;
/* the command line after the program name, without the blanks that follow the name. */
extern const WCHAR *tu_tail;

int tool_main(void);

/* flushes the output and ends the process. */
DECLSPEC_NORETURN void tu_exit(int code);

void *tu_alloc(SIZE_T bytes);
void *tu_realloc(void *memory, SIZE_T bytes);
void tu_free(void *memory);
WCHAR *tu_dup(const WCHAR *text);
WCHAR *tu_dup_n(const WCHAR *text, SIZE_T count);

/* ------------------------------------------------------------------ growing text */

typedef struct TU_STR {
    WCHAR *data;
    SIZE_T length;
    SIZE_T capacity;
} TU_STR;

void tu_str_reserve(TU_STR *s, SIZE_T extra);
void tu_str_append(TU_STR *s, const WCHAR *text, SIZE_T count);
void tu_str_puts(TU_STR *s, const WCHAR *text);
void tu_str_putc(TU_STR *s, WCHAR c);
void tu_str_vformat(TU_STR *s, const WCHAR *format, va_list arguments);
void tu_str_format(TU_STR *s, const WCHAR *format, ...);
/* the text is always null terminated. */
const WCHAR *tu_str_text(TU_STR *s);
void tu_str_free(TU_STR *s);

/*
 * the formatter knows %d %i %u %x %X %c %s %S %p %%, the flags - + space 0, a width and precision (also *), and the sizes
 * h l ll I64 z. %s is a wide string, %S an ansi one.
 */

/* ------------------------------------------------------------------ output */

#define TU_STDOUT 0
#define TU_STDERR 1

void tu_print(const WCHAR *format, ...);
void tu_eprint(const WCHAR *format, ...);
void tu_write(int stream, const WCHAR *text, SIZE_T count);
void tu_puts(const WCHAR *text);
/* bytes of the output's own encoding, kept in order with the text. */
void tu_write_bytes(const BYTE *data, SIZE_T count);
void tu_flush(void);
/* the output handle is a console. */
BOOL tu_is_console(int stream);
/* the characters of a line of the console, 80 when the output is no console. */
int tu_console_width(void);

/* the system's text for an error code without the final line break, or an unknown message. */
void tu_error_text(DWORD error, TU_STR *out);

/* ------------------------------------------------------------------ input */

typedef struct TU_READER {
    HANDLE handle;
    BYTE *data;
    SIZE_T size;
    SIZE_T capacity;
    SIZE_T start;
    /* the position in the input of the first byte of the buffer. */
    UINT64 base;
    BOOL end;
    BOOL failed;
    BOOL first;
} TU_READER;

void tu_reader_init(TU_READER *r, HANDLE handle);
/* the next line without its line break; the text lives until the next call. false at the end of the input. */
BOOL tu_reader_line(TU_READER *r, const BYTE **line, SIZE_T *length);
void tu_reader_free(TU_READER *r);

/* ------------------------------------------------------------------ strings */

BOOL tu_eq(const WCHAR *a, const WCHAR *b);
BOOL tu_eq_n(const WCHAR *a, const WCHAR *b, SIZE_T count);
/* a starts with b, compared without case; rest gets the text after it. */
BOOL tu_prefix(const WCHAR *a, const WCHAR *b, const WCHAR **rest);
/* the text holds only the digits of a decimal number that fits. */
BOOL tu_parse_u32(const WCHAR *text, DWORD *value);
BOOL tu_parse_int(const WCHAR *text, LONG *value);
/* decimal, or hexadecimal with a 0x prefix. */
BOOL tu_parse_number(const WCHAR *text, DWORD *value);
/* the pattern holds * and ?; the comparison ignores case. */
BOOL tu_wild(const WCHAR *pattern, const WCHAR *text);
WCHAR tu_upper(WCHAR c);
WCHAR tu_lower(WCHAR c);
BOOL tu_is_space(WCHAR c);
BOOL tu_is_digit(WCHAR c);

/* the environment variable as text in a new allocation, or null. */
WCHAR *tu_getenv(const WCHAR *name);

/* ------------------------------------------------------------------ tables */

#define TU_FMT_TABLE 0
#define TU_FMT_LIST 1
#define TU_FMT_CSV 2

typedef struct TU_COLUMN {
    const WCHAR *title;
    int width; /* zero fits the widest cell. */
    BOOL right;
} TU_COLUMN;

typedef struct TU_TABLE {
    const TU_COLUMN *columns;
    int count;
    WCHAR ***rows;
    SIZE_T rows_count;
    SIZE_T rows_capacity;
} TU_TABLE;

void tu_table_init(TU_TABLE *t, const TU_COLUMN *columns, int count);
/* the cells are copied. */
void tu_table_row(TU_TABLE *t, const WCHAR *const *cells);
void tu_table_free(TU_TABLE *t);
/* a table has its header over rows of ====, a list has a label per cell with a blank line before each row, a csv
   file quotes every cell. */
void tu_table_print(const TU_TABLE *t, int format, BOOL no_header);
/* the format named by text, or -1. */
int tu_format_by_name(const WCHAR *text);

#define TU_ARRAY(a) (sizeof(a) / sizeof((a)[0]))

#endif
