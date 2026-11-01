/* helpers shared by the files of the api. */
#pragma once
#include "ese.h"

void api_wide(LPCSTR text, WCHAR *out, int size);
JET_ERR api_ansi_out(const WCHAR *text, char *out, ULONG cb);
SESSION *api_session(JET_SESID sesid, JET_ERR *err);
JET_ERR api_set_param(JET_INSTANCE *handle, JET_SESID sesid, ULONG id, JET_API_PTR value, const void *text, BOOL wide);
JET_ERR api_init(JET_INSTANCE *handle, JET_GRBIT grbit);
JET_ERR api_ddl_begin(SESSION *sess, BOOL *implicit);
JET_ERR api_ddl_end(SESSION *sess, BOOL implicit, JET_ERR err);
CURSOR *api_ddl_cursor(JET_SESID handle, JET_TABLEID table, JET_ERR *err);
JET_ERR api_create_index(JET_SESID handle, JET_TABLEID table, LPCWSTR name, JET_GRBIT grbit, const WCHAR *key,
                         ULONG cbkey, ULONG density, ULONG lcid, ULONG map_flags, ULONG varsegmac,
                         const JET_TUPLELIMITS *tuples, const JET_CONDITIONALCOLUMN *cond, ULONG ncond, BOOL cond_wide,
                         ULONG keymost);
JET_ERR api_one_index(SESSION *sess, TABLE *table, BOOL wide, ULONG version, BYTE *base, ULONG index);
JET_ERR api_create_table_spec(SESSION *sess, DBH *dbh, BOOL wide, ULONG version, void *spec);
JET_ERR api_enumerate(CURSOR *cur, ULONG count, JET_ENUMCOLUMNID *ids, ULONG *out_count, JET_ENUMCOLUMN **out,
                      void *(WINAPI *alloc)(void *, void *, ULONG), void *context, ULONG max_data, JET_GRBIT grbit);
JET_ERR api_goto_secondary(CURSOR *cur, void *skey, ULONG skey_cb, void *pkey, ULONG pkey_cb, JET_GRBIT grbit);
JET_ERR api_record_position(CURSOR *cur, JET_RECPOS *pos);
JET_ERR api_goto_position(CURSOR *cur, JET_RECPOS *pos);
BOOL ese_valid_name(LPCWSTR name);
extern DB g_temp_db;
