/* the storage engine behind the jet api: instances, sessions, databases, tables, indexes, records and versions. */
#pragma once
#include "jet.h"

#define ESE_MAX_TRANSACTION_LEVEL 7
#define ESE_MAX_SESSIONS_DEFAULT 16
#define ESE_PAGE_SIZE_DEFAULT 8192

typedef struct VAL {
    ULONG cb;
    BYTE *data;
} VAL;

/* the values of one column of a record, more than one for a multi valued column. */
typedef struct CELL {
    ULONG count;
    VAL *vals;
    BOOL set;
} CELL;

struct SESSION;
struct TABLE;
struct RECORD;

typedef struct VERSION {
    struct VERSION *older;
    ULONG64 commit;
    struct SESSION *owner;
    ULONG level;
    BOOL deleted;
    CELL *cells;
    ULONG ncells;
} VERSION;

typedef struct RECORD {
    BYTE *pk;
    ULONG pkcb;
    VERSION *head;
    struct SESSION *lock;
    ULONG lock_level;
    BOOL listed;
} RECORD;

typedef struct COLUMN {
    JET_COLUMNID id;
    WCHAR name[JET_cbNameMost + 1];
    JET_COLTYP coltyp;
    ULONG cbMax;
    JET_GRBIT grbit;
    USHORT cp, langid, country, collate;
    BYTE *def;
    ULONG defcb;
    BOOL deleted;
    LONG autoinc;
    ULONG version_counter;
} COLUMN;

typedef struct SEGMENT {
    JET_COLUMNID id;
    BOOL desc;
} SEGMENT;

typedef struct ENTRY {
    BYTE *key;
    ULONG cb;
    RECORD *rec;
} ENTRY;

typedef struct CONDITION {
    JET_COLUMNID id;
    BOOL must_be_null;
} CONDITION;

typedef struct INDEX {
    WCHAR name[JET_cbNameMost + 1];
    JET_GRBIT grbit;
    SEGMENT segs[16];
    ULONG nsegs;
    ULONG lcid, map_flags, keymost, varsegmac, density;
    CONDITION cond[12];
    ULONG ncond;
    JET_TUPLELIMITS tuples;
    BOOL primary, sequential, deleted;
    ENTRY *entries;
    ULONG count, cap;
    ULONG64 objid;
    ULONG pgno;
} INDEX;

typedef struct TABLE {
    struct TABLE *next;
    struct DB *db;
    WCHAR name[JET_cbNameMost + 1];
    COLUMN **cols;
    ULONG ncols, ncap;
    INDEX **idx;
    ULONG nidx, nicap;
    INDEX *primary;
    ULONG64 next_id;
    ULONG64 objid;
    ULONG pgno, pages, density;
    ULONG refs;
    BOOL temp, deleted, is_template;
    WCHAR template_name[JET_cbNameMost + 1];
    ULONG last_fixed, last_var, last_tagged;
    ULONG64 created, updated;
    ULONG64 lv_bytes;
    ULONG class_;
    struct CURSOR *creator;
    ULONG stat_records, stat_pages;
} TABLE;

typedef struct DB {
    struct DB *next;
    struct INSTANCE *inst;
    WCHAR path[MAX_PATH];
    TABLE *tables;
    ULONG pagesize, state;
    BOOL readonly, exclusive, dirty, attached, created_here;
    ULONG64 dbtime, next_objid;
    ULONG ifmp, refs;
    HANDLE file;
    JET_SIGNATURE sign;
    ULONG version, update;
    ULONG64 header_dbtime;
    ULONG max_pages;
    BYTE header[512];
    BOOL header_valid;
} DB;

typedef struct DBH {
    struct DBH *next;
    DB *db;
    JET_DBID id;
    struct SESSION *sess;
    ULONG refs;
} DBH;

enum { U_VERSION = 1, U_LOCK, U_ESCROW, U_CREATE_TABLE, U_DELETE_TABLE, U_ADD_COLUMN, U_DELETE_COLUMN, U_CREATE_INDEX,
       U_DELETE_INDEX, U_RENAME_TABLE, U_RENAME_COLUMN, U_RENAME_INDEX };

typedef struct UNDO {
    ULONG kind, level;
    TABLE *table;
    RECORD *rec;
    VERSION *ver;
    JET_COLUMNID col;
    LONG delta;
    void *data;
    WCHAR name[JET_cbNameMost + 1];
} UNDO;

typedef struct CURSOR {
    struct CURSOR *next;
    struct SESSION *sess;
    DBH *dbh;
    TABLE *table;
    INDEX *index;
    ULONG pos;
    RECORD *rec;
    BYTE *ekey;
    ULONG ekeycb;
    BYTE *mkey;
    ULONG mkeycb;
    ULONG mkey_segs;
    BOOL mkey_wild, mkey_null_pending;
    BYTE *rkey;
    ULONG rkeycb;
    BOOL has_range;
    BOOL range_upper, range_inclusive;
    ULONG prep;
    VERSION *copy;
    RECORD *copy_rec;
    BOOL copy_new;
    ULONG64 copy_stamp;
    JET_LS ls;
    BOOL sequential, temp_sorted;
    ULONG upd_level;
    ULONG64 temp_id;
    ULONG unique_dups;
    BOOL set_by_index_range;
    ULONG64 keystamp;
    ULONG mkey_flags;
    BYTE *bmcache;
    BOOL ddl, readonly, copy_dirty;
    JET_GRBIT open_grbit;
} CURSOR;

typedef struct SESSION {
    struct SESSION *next;
    struct INSTANCE *inst;
    ULONG level;
    ULONG64 snapshot;
    UNDO *undo;
    ULONG nundo, capundo;
    ULONG marks[ESE_MAX_TRANSACTION_LEVEL + 2];
    BOOL read_only_trx[ESE_MAX_TRANSACTION_LEVEL + 2];
    DBH *dbs;
    CURSOR *cursors;
    JET_API_PTR context;
    LONG counters[8];
    BOOL implicit;
    ULONG implicit_depth;
    ULONG64 id;
} SESSION;

#define ESE_PARAM_COUNT 192

typedef struct PARAMS {
    JET_API_PTR values[ESE_PARAM_COUNT];
    WCHAR *texts[ESE_PARAM_COUNT];
    BOOL set[ESE_PARAM_COUNT];
} PARAMS;

typedef struct PARAM_DEFAULT {
    BYTE kind;
    ULONG64 value;
    const WCHAR *text;
} PARAM_DEFAULT;

extern const PARAM_DEFAULT g_param_defaults[];
extern const ULONG g_param_count;
const WCHAR *ese_text_param(const PARAMS *params, ULONG id);
void ese_copy_params(PARAMS *to, const PARAMS *from);
void ese_free_params(PARAMS *params);

typedef struct INSTANCE {
    struct INSTANCE *next;
    WCHAR name[64], display[64];
    BOOL initialized, legacy, stopping;
    PARAMS params;
    DB *dbs;
    SESSION *sessions;
    ULONG64 clock;
    ULONG next_ifmp;
    BOOL backup_active;
    ULONG backup_file_count;
    HANDLE backup_file;
    DB *backup_db;
    WCHAR display_name[64];
    BOOL recovery;
    ULONG64 dbtime;
} INSTANCE;

/* the engine, used by the api layer. */
extern CRITICAL_SECTION g_ese_lock;
extern INSTANCE *g_instances;
extern PARAMS g_defaults;
extern ULONG g_global_pagesize;
extern BOOL g_global_pagesize_set;
extern BOOL g_any_initialized;

void *ese_alloc(SIZE_T size);
void *ese_realloc(void *p, SIZE_T size);
void ese_free(void *p);
BYTE *ese_dup(const void *p, ULONG cb);
INSTANCE *ese_instance(JET_INSTANCE handle);
SESSION *ese_session(JET_SESID handle);
CURSOR *ese_cursor(JET_SESID sesid, JET_TABLEID tableid, JET_ERR *err);
void ese_init_params(PARAMS *params);
ULONG ese_page_size(INSTANCE *inst);
ULONG ese_key_most(ULONG pagesize);

/* sessions and transactions */
SESSION *ese_new_session(INSTANCE *inst);
void ese_free_session(SESSION *sess);
JET_ERR ese_begin(SESSION *sess, BOOL read_only);
JET_ERR ese_commit(SESSION *sess, JET_GRBIT grbit);
JET_ERR ese_rollback(SESSION *sess, BOOL all);
UNDO *ese_log(SESSION *sess, ULONG kind);
BOOL ese_visible_session_snapshot(SESSION *sess, ULONG64 *snapshot);

/* databases */
JET_ERR ese_create_db(SESSION *sess, LPCWSTR path, ULONG pagesize, JET_GRBIT grbit, JET_DBID *dbid);
JET_ERR ese_attach_db(SESSION *sess, LPCWSTR path, JET_GRBIT grbit, ULONG max_pages);
JET_ERR ese_detach_db(SESSION *sess, LPCWSTR path, JET_GRBIT grbit);
JET_ERR ese_open_db(SESSION *sess, LPCWSTR path, JET_DBID *dbid, JET_GRBIT grbit);
JET_ERR ese_close_db(SESSION *sess, JET_DBID dbid);
DBH *ese_dbh(SESSION *sess, JET_DBID dbid);
JET_ERR ese_save_db(DB *db, BOOL final);
JET_ERR ese_load_db(DB *db, LPCWSTR path);
JET_ERR ese_read_header(DB *db, LPCWSTR path);
void ese_free_db(DB *db);
JET_ERR ese_flush_if_needed(SESSION *sess, DB *db);
TABLE *ese_find_table(DB *db, LPCWSTR name);

/* tables, columns and indexes */
JET_ERR ese_create_table(SESSION *sess, DBH *dbh, LPCWSTR name, ULONG pages, ULONG density, TABLE **out);
JET_ERR ese_delete_table(SESSION *sess, DBH *dbh, LPCWSTR name);
JET_ERR ese_rename_table(SESSION *sess, DBH *dbh, LPCWSTR from, LPCWSTR to);
JET_ERR ese_add_column(SESSION *sess, TABLE *table, LPCWSTR name, const JET_COLUMNDEF *def, const void *dv, ULONG dvcb,
                       JET_COLUMNID *id);
JET_ERR ese_delete_column(SESSION *sess, TABLE *table, LPCWSTR name, JET_GRBIT grbit);
JET_ERR ese_rename_column(SESSION *sess, TABLE *table, LPCWSTR from, LPCWSTR to);
JET_ERR ese_create_index(SESSION *sess, TABLE *table, LPCWSTR name, JET_GRBIT grbit, const WCHAR *key, ULONG cbkey,
                         ULONG density, ULONG lcid, ULONG map_flags, ULONG varsegmac, const JET_TUPLELIMITS *tuples,
                         const JET_CONDITIONALCOLUMN *cond, ULONG ncond, BOOL cond_wide, ULONG keymost);
JET_ERR ese_delete_index(SESSION *sess, TABLE *table, LPCWSTR name);
COLUMN *ese_find_column(TABLE *table, LPCWSTR name);
COLUMN *ese_column_by_id(TABLE *table, JET_COLUMNID id);
INDEX *ese_find_index(TABLE *table, LPCWSTR name);
void ese_free_table(TABLE *table);

/* keys */
BOOL ese_fixed_size(JET_COLTYP type, ULONG *size);
ULONG ese_key_from_value(const COLUMN *col, const INDEX *idx, BOOL desc, const void *data, ULONG cb, BOOL null,
                         JET_GRBIT grbit, BYTE *out, ULONG room);
JET_ERR ese_index_keys(TABLE *table, INDEX *idx, const VERSION *ver, BYTE ***keys, ULONG **cbs, ULONG *count);
void ese_free_keys(BYTE **keys, ULONG *cbs, ULONG count);
int ese_compare_entry(const BYTE *a, ULONG acb, const BYTE *pa, ULONG pacb, const BYTE *b, ULONG bcb, const BYTE *pb,
                      ULONG pbcb);
LONG ese_entry_search(INDEX *idx, const BYTE *key, ULONG cb, const BYTE *pk, ULONG pkcb, BOOL *found);

/* records and cursors */
JET_ERR ese_open_cursor(SESSION *sess, DBH *dbh, TABLE *table, JET_GRBIT grbit, CURSOR **out);
void ese_close_cursor(CURSOR *cur);
JET_ERR ese_dup_cursor(CURSOR *cur, CURSOR **out);
VERSION *ese_visible(RECORD *rec, SESSION *sess);
VERSION *ese_current_version(CURSOR *cur);
JET_ERR ese_prepare(CURSOR *cur, ULONG prep);
JET_ERR ese_update(CURSOR *cur, void *bookmark, ULONG cb, ULONG *actual, JET_GRBIT grbit);
JET_ERR ese_delete(CURSOR *cur);
JET_ERR ese_set_column(CURSOR *cur, JET_COLUMNID id, const void *data, ULONG cb, JET_GRBIT grbit,
                       const JET_SETINFO *info);
JET_ERR ese_retrieve_column(CURSOR *cur, JET_COLUMNID id, void *data, ULONG cb, ULONG *actual, JET_GRBIT grbit,
                            const JET_RETINFO *info);
JET_ERR ese_escrow(CURSOR *cur, JET_COLUMNID id, void *delta, ULONG deltacb, void *old, ULONG oldcb, ULONG *oldactual,
                   JET_GRBIT grbit);
JET_ERR ese_move(CURSOR *cur, LONG rows, JET_GRBIT grbit);
JET_ERR ese_make_key(CURSOR *cur, const void *data, ULONG cb, JET_GRBIT grbit);
JET_ERR ese_seek(CURSOR *cur, JET_GRBIT grbit);
JET_ERR ese_set_index_range(CURSOR *cur, JET_GRBIT grbit);
JET_ERR ese_set_current_index(CURSOR *cur, LPCWSTR name, JET_GRBIT grbit, ULONG tag);
JET_ERR ese_get_bookmark(CURSOR *cur, void *buf, ULONG cb, ULONG *actual);
JET_ERR ese_goto_bookmark(CURSOR *cur, const void *buf, ULONG cb);
JET_ERR ese_index_count(CURSOR *cur, ULONG *count, ULONG maximum);
ULONG ese_visible_count(CURSOR *cur, INDEX *idx, ULONG limit);
ULONG ese_record_estimate(TABLE *table);
void ese_gc_record(TABLE *table, RECORD *rec, SESSION *sess);
void ese_free_cells(TABLE *table, VERSION *ver);
VERSION *ese_new_version(TABLE *table);
VERSION *ese_copy_version(TABLE *table, const VERSION *from);
const CELL *ese_cell(const VERSION *ver, ULONG slot);
CELL *ese_cell_w(VERSION *ver, ULONG slot);
ULONG ese_slot(TABLE *table, JET_COLUMNID id);
ULONG64 ese_next_stamp(INSTANCE *inst);
void ese_drop_record(TABLE *table, RECORD *rec);
void ese_remove_entries_of(TABLE *table, INDEX *idx, RECORD *rec, const VERSION *ver);
BOOL ese_ansi(const COLUMN *col);
