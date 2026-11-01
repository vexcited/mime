/*
 * mstask.dll internals shared by its source files.
 */
#ifndef MIME_MSTASK_PRIVATE_H
#define MIME_MSTASK_PRIVATE_H

#include <mime/win32/mstask.h>
#include <rtl.h>

/* the number of objects alive, for DllCanUnloadNow. */
extern LONG g_mstask_objects;

HRESULT MstaskFailure(void);
LPWSTR MstaskDuplicate(LPCWSTR text);

/* job.c */
HRESULT JobCreate(LPCWSTR path, REFIID riid, void **object);
HRESULT JobLoad(LPCWSTR path, REFIID riid, void **object);
void JobForgetAccount(LPCWSTR path);

/* schedule.c */
HRESULT ScheduleCreate(REFIID riid, void **object);
HRESULT MstaskTasksFolder(WCHAR folder[MAX_PATH]);

#endif
