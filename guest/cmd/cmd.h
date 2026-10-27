/*
 * cmd.exe: the command interpreter.
 *
 * a line goes through these stages, as in the documented order of the real interpreter:
 *   1. percent expansion of the whole line when it is read: %% %0..%9 %* %~mods %name% %name:a=b% %name:~s,l%
 *   2. parsing into a tree of statements: quotes, ^ escapes, & && || | ( ) redirections, if, for and rem
 *   3. for variable substitution when a statement runs, then delayed expansion (!name!), then the redirections
 *   4. dispatch to an internal command, a batch file or a program
 */
#ifndef MIME_CMD_H
#define MIME_CMD_H

#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>
#include <rtl.h>
#include <stdarg.h>

#define NUM(a) RTL_NUMBER_OF(a)
#ifndef SW_SHOWNORMAL
#define SW_SHOWNORMAL 1
#define SW_SHOWMAXIMIZED 3
#define SW_SHOWMINNOACTIVE 7
#endif
#ifndef SYMBOLIC_LINK_FLAG_DIRECTORY
#define SYMBOLIC_LINK_FLAG_DIRECTORY 1
#endif

static inline WCHAR *wcspbrk(const WCHAR *s, const WCHAR *set)
{
    for (; *s; s++)
        for (const WCHAR *c = set; *c; c++)
            if (*s == *c)
                return (WCHAR *)s;
    return nullptr;
}

static inline void lower_inplace(WCHAR *s)
{
    for (; *s; s++)
        *s = towlower(*s);
}
#define LINE_LIMIT 8191
#define SETLOCAL_LIMIT 32

/* ------------------------------------------------------------------ memory and strings */

void *xalloc(SIZE_T bytes);
void *xrealloc(void *p, SIZE_T bytes);
void xfree(void *p);
WCHAR *xdup(const WCHAR *s);
WCHAR *xdupn(const WCHAR *s, SIZE_T n);

typedef struct STR {
    WCHAR *p;
    SIZE_T len, cap;
} STR;

void str_init(STR *s);
void str_free(STR *s);
void str_reset(STR *s);
void str_addc(STR *s, WCHAR c);
void str_addn(STR *s, const WCHAR *t, SIZE_T n);
void str_adds(STR *s, const WCHAR *t);
void str_fmt(STR *s, const WCHAR *fmt, ...);
WCHAR *str_take(STR *s);
const WCHAR *str_get(STR *s);

BOOL is_space(WCHAR c);
BOOL is_digit(WCHAR c);
WCHAR *skip_space(const WCHAR *p);
WCHAR upcase(WCHAR c);
BOOL starts_with_i(const WCHAR *s, const WCHAR *prefix);
void trim_right(STR *s);
LONG parse_long(const WCHAR *s, const WCHAR **end);
BOOL all_numeric_long(const WCHAR *s, LONG *value);
/* removes every quote character, the way file names are taken from tokens. */
void strip_quotes_inplace(WCHAR *s);

/* ------------------------------------------------------------------ state */

typedef struct IO {
    HANDLE h[3];
    BOOL con[3];
} IO;

typedef struct LOCALENV {
    WCHAR *block;
    SIZE_T chars;
    WCHAR *cwd;
    BOOL ext, delayed;
} LOCALENV;

typedef struct BATCH {
    WCHAR *path;
    WCHAR **tok; /* the arguments, %1 is tok[0 + shifted]. */
    int ntok;
    int shifted;
    WCHAR *name; /* %0 as written */
    WCHAR *args; /* %* */
    LONGLONG offset;
    struct BATCH *parent;
    LOCALENV *locals[SETLOCAL_LIMIT];
    int nlocals;
    BOOL echo_saved;
    BOOL ended;
} BATCH;

typedef struct FORCTX {
    struct FORCTX *up;
    WCHAR names[32];
    WCHAR *values[32];
    BOOL defined[32];
    int count;
} FORCTX;

typedef struct STATE {
    BOOL ext;          /* command extensions */
    BOOL delayed;      /* delayed expansion */
    BOOL echo;
    BOOL quiet;        /* /q */
    BOOL unicode_out;  /* /u */
    BOOL single;       /* /c, /s, /r: one command and exit */
    BOOL interactive;
    BOOL verify;
    BOOL goto_pending; /* a goto or exit /b unwinds the statements of the current batch */
    DWORD errorlevel;
    DWORD out_cp;
    BATCH *batch;
    FORCTX *forctx;
    WCHAR *cmdline;
    WCHAR *self;       /* path of this executable */
    int pushd_count;
    WCHAR *pushd[64];
    volatile LONG ctrlc;
    BOOL abort_all;
    int nest;          /* depth of call levels, for the recursion limit */
    WORD default_color;
    BOOL in_pipe_child;
    BOOL nul_node;     /* the last line read was empty, so the prompt needs no blank line before it */
    BOOL call_flag;    /* the command about to run is the target of call */
    DWORD in_cp;
    BOOL cmd_script;   /* the outermost batch file is a .cmd, whose commands set ERRORLEVEL on success too */
} STATE;

extern STATE g;
extern IO g_io;

/* ------------------------------------------------------------------ output and input */

void io_init_std(void);
void io_set(IO *io, int fd, HANDLE h);
void out_n(const WCHAR *s, SIZE_T n);
void out_s(const WCHAR *s);
void out_fmt(const WCHAR *fmt, ...);
void err_fmt(const WCHAR *fmt, ...);
void err_s(const WCHAR *s);
void write_to(HANDLE h, BOOL console, const WCHAR *s, SIZE_T n);
/* a line from standard input without its line break, FALSE at the end of input. */
BOOL read_stdin_line(STR *line, BOOL prompt_echo);
BOOL read_key(WCHAR *c);
void print_win_error(DWORD error);
void msg_not_recognized(const WCHAR *name);
void msg_syntax(void);
void msg_unexpected(const WCHAR *what);
void msg_file_not_found(void);
void msg_error(const WCHAR *text);

/* ------------------------------------------------------------------ environment */

BOOL env_get(const WCHAR *name, STR *value);
WCHAR *env_dup(const WCHAR *name);
BOOL env_defined(const WCHAR *name);
BOOL env_set(const WCHAR *name, const WCHAR *value);
void env_setup(void);
BOOL env_dynamic(const WCHAR *name, STR *value);
BOOL env_print_all(const WCHAR *prefix);
LOCALENV *env_snapshot(void);
void env_restore(LOCALENV *snap);
void env_free_snapshot(LOCALENV *snap);
void drive_dir_set(const WCHAR *dir);
BOOL drive_dir_get(WCHAR drive, STR *dir);

/* ------------------------------------------------------------------ expansion */

BOOL expand_percent(const WCHAR *in, SIZE_T n, STR *out);
BOOL expand_delayed(const WCHAR *in, STR *out);
void expand_forvars(const WCHAR *in, STR *out);
BOOL expand_var_spec(const WCHAR *start, WCHAR term, STR *out, SIZE_T *consumed, BOOL *defined);
FORCTX *forctx_push(void);
void forctx_pop(FORCTX *ctx);
void forctx_set(FORCTX *ctx, WCHAR name, const WCHAR *value);
void forctx_clear(FORCTX *ctx);

/* ------------------------------------------------------------------ syntax tree */

typedef enum NODE_KIND {
    N_CMD,
    N_IF,
    N_FOR,
    N_BLOCK,
    N_SEQ,
    N_AND,
    N_OR,
    N_PIPE,
    N_QUIET,
    N_EMPTY
} NODE_KIND;

typedef enum REDIR_KIND { R_OUT, R_APPEND, R_IN, R_DUP } REDIR_KIND;

typedef struct REDIR {
    struct REDIR *next;
    int fd;
    REDIR_KIND kind;
    int dup_fd;
    WCHAR *target;
} REDIR;

typedef enum IF_KIND { IF_ERRORLEVEL, IF_EXIST, IF_DEFINED, IF_CMDEXT, IF_STRING, IF_COMPARE } IF_KIND;
typedef enum CMP_OP { CMP_EQU, CMP_NEQ, CMP_LSS, CMP_LEQ, CMP_GTR, CMP_GEQ } CMP_OP;

#define FOR_L 1
#define FOR_D 2
#define FOR_R 4
#define FOR_F 8

typedef struct NODE {
    NODE_KIND kind;
    REDIR *redirs;
    WCHAR *src; /* the statement as it was written, after percent expansion */
    /* N_CMD */
    WCHAR *text; /* command and arguments with the escapes removed */
    /* N_IF */
    IF_KIND if_kind;
    CMP_OP op;
    BOOL negate, icase;
    WCHAR *lhs, *rhs;
    /* N_FOR */
    int for_flags;
    WCHAR *for_option; /* /f options or /r root */
    WCHAR for_var;
    WCHAR *for_set;
    struct NODE *a, *b, *c; /* sub statements: left and right, body and else */
} NODE;

typedef struct LINESRC LINESRC;
struct LINESRC {
    /* the next physical line after expansion, FALSE at the end. */
    BOOL (*next)(LINESRC *src, STR *line);
    void *context;
};

NODE *parse_line(const WCHAR *line, LINESRC *more, BOOL colon_is_token, int *error);
void node_free(NODE *n);

/* ------------------------------------------------------------------ execution */

int exec_node(NODE *n, BOOL top);
int exec_text(const WCHAR *line, BOOL top);
int exec_command_line(const WCHAR *line);
int run_cmd_node(NODE *n);
int spawn_external(const WCHAR *program, const WCHAR *command_line, DWORD flags, BOOL wait, const WCHAR *title,
                   WORD show, const WCHAR *directory, DWORD *exit_code, BOOL gui_nowait_ok);
int run_batch_file(const WCHAR *path, const WCHAR *name, const WCHAR *args, BOOL is_call);
BOOL find_program(const WCHAR *name, STR *path, int *kind);
enum { KIND_NONE, KIND_EXE, KIND_BATCH, KIND_DIR, KIND_OTHER };
void set_errorlevel(DWORD code);
WCHAR *unparse_for_child(const WCHAR *src);
int run_command_capture(const WCHAR *command, STR *output);
BOOL ask_terminate_batch(void);
void ctrlc_poll(void);

/* ------------------------------------------------------------------ batch */

BATCH *batch_begin(const WCHAR *path, const WCHAR *name, const WCHAR *args);
void batch_end(BATCH *b);
void batch_unwind_locals(BATCH *b);
BOOL batch_read_line(BATCH *b, STR *line);
BOOL batch_goto(BATCH *b, const WCHAR *label_text);
void batch_shift(BATCH *b, int from);
int run_batch_loop(BATCH *b);
const WCHAR *batch_arg(BATCH *b, int n);

/* ------------------------------------------------------------------ internal commands */

typedef int (*CMDFN)(const WCHAR *args, NODE *node);
typedef struct CMDDEF {
    const WCHAR *name;
    CMDFN fn;
} CMDDEF;
const CMDDEF *find_internal(const WCHAR *name);
int internal_goto(const WCHAR *args, NODE *node);
int internal_call(const WCHAR *args, NODE *node);
int internal_exit(const WCHAR *args, NODE *node);
int internal_shift(const WCHAR *args, NODE *node);
int internal_setlocal(const WCHAR *args, NODE *node);
int internal_endlocal(const WCHAR *args, NODE *node);
int internal_set(const WCHAR *args, NODE *node);
int internal_echo(const WCHAR *args, NODE *node);
int internal_cd(const WCHAR *args, NODE *node);
int internal_pushd(const WCHAR *args, NODE *node);
int internal_popd(const WCHAR *args, NODE *node);
int internal_md(const WCHAR *args, NODE *node);
int internal_rd(const WCHAR *args, NODE *node);
int internal_del(const WCHAR *args, NODE *node);
int internal_copy(const WCHAR *args, NODE *node);
int internal_move(const WCHAR *args, NODE *node);
int internal_ren(const WCHAR *args, NODE *node);
int internal_type(const WCHAR *args, NODE *node);
int internal_dir(const WCHAR *args, NODE *node);
int internal_cls(const WCHAR *args, NODE *node);
int internal_title(const WCHAR *args, NODE *node);
int internal_ver(const WCHAR *args, NODE *node);
int internal_path(const WCHAR *args, NODE *node);
int internal_prompt(const WCHAR *args, NODE *node);
int internal_start(const WCHAR *args, NODE *node);
int internal_color(const WCHAR *args, NODE *node);
int internal_verify(const WCHAR *args, NODE *node);
int internal_vol(const WCHAR *args, NODE *node);
int internal_mklink(const WCHAR *args, NODE *node);
int internal_assoc(const WCHAR *args, NODE *node);
int internal_ftype(const WCHAR *args, NODE *node);
int internal_pause(const WCHAR *args, NODE *node);
int internal_date(const WCHAR *args, NODE *node);
int internal_time(const WCHAR *args, NODE *node);
int internal_break(const WCHAR *args, NODE *node);
int internal_rem(const WCHAR *args, NODE *node);
int exec_if(NODE *n);
int exec_for(NODE *n);
int set_arithmetic(const WCHAR *expr, BOOL show);
void print_prompt(void);
BOOL change_directory(const WCHAR *path, BOOL switch_drive);
BOOL get_cwd(STR *out);
void set_console_color(WORD attr);

/* paths */
void path_split_pattern(const WCHAR *spec, STR *dir, STR *pattern);
BOOL wild_match(const WCHAR *pattern, const WCHAR *name);
BOOL has_wildcards(const WCHAR *s);
void format_number_commas(ULONGLONG v, STR *out);
void format_date_time(const FILETIME *ft, STR *out);
void format_date(const SYSTEMTIME *st, BOOL with_day, STR *out);
void format_time_short(const SYSTEMTIME *st, STR *out);
int compare_names(const WCHAR *a, const WCHAR *b);
WIN32_FIND_DATAW *find_all_sorted(const WCHAR *spec, SIZE_T *count);
WCHAR **list_matching(const WCHAR *spec, SIZE_T *count, BOOL with_folders);
void sort_pointers(void **items, SIZE_T count, int (*cmp)(const void *, const void *));

/* the tokens of an argument line, split on blanks and ,;= with quotes kept. */
typedef struct ARGS {
    WCHAR **v;
    int n;
} ARGS;
void args_split(const WCHAR *line, ARGS *a, BOOL equals_delim);
void args_free(ARGS *a);
BOOL is_switch(const WCHAR *tok);

#endif
