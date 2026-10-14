/*
 * wininet internals: the HINTERNET objects shared by the handle, option and http code.
 */
#pragma once
/* not every processor build defines it, and the exports need it. */
#ifndef MIME_BUILD_WININET
#define MIME_BUILD_WININET
#endif
#include <mime/win32/wininet.h>

/* option slots for INTERNET_OPTION_CONNECT_TIMEOUT through INTERNET_OPTION_DATA_RECEIVE_TIMEOUT. */
#define INET_TIMEOUT_FIRST INTERNET_OPTION_CONNECT_TIMEOUT
#define INET_TIMEOUT_LAST INTERNET_OPTION_DATA_RECEIVE_TIMEOUT
#define INET_TIMEOUT_COUNT (INET_TIMEOUT_LAST - INET_TIMEOUT_FIRST + 1)

typedef struct {
    PWSTR Name;
    PWSTR Value;
} INET_HEADER;

typedef struct INET_OBJECT {
    LONG volatile Refs;
    DWORD Type;
    HINTERNET Handle;
    struct INET_OBJECT *Parent;
    DWORD Flags;
    DWORD_PTR Context;
    INTERNET_STATUS_CALLBACK Callback;
    DWORD Timeouts[INET_TIMEOUT_COUNT];
    /* session. */
    PWSTR Agent;
    DWORD AccessType;
    /* connection. */
    PWSTR Server;
    PWSTR User;
    PWSTR Password;
    INTERNET_PORT Port;
    DWORD Service;
    /* request. */
    PWSTR Verb;
    PWSTR Object;
    PWSTR Version;
    PWSTR Referrer;
    INET_HEADER *Headers;
    DWORD HeaderCount;
    DWORD HeaderCapacity;
} INET_OBJECT;

PWSTR InetDuplicate(LPCWSTR text);
void InetFree(void *memory);
/* the referenced object of a handle, or NULL with ERROR_INVALID_HANDLE. */
INET_OBJECT *InetReference(HINTERNET handle);
void InetRelease(INET_OBJECT *object);
/* the referenced object of a handle of the given type, else NULL with the error set. */
INET_OBJECT *InetReferenceType(HINTERNET handle, DWORD type);
BOOL InetFail(DWORD error);
