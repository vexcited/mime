/*
 * runtime library shared by the kernel and ntdll. both export these
 * functions, exactly like ntoskrnl.exe and ntdll.dll do on windows.
 */
#ifndef MIME_RTL_H
#define MIME_RTL_H

#include <mime/nt.h>
#include <stdbool.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t max);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
int _stricmp(const char *a, const char *b);
int _strnicmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *hay, const char *needle);
size_t wcslen(const wchar_t *s);
size_t wcsnlen(const wchar_t *s, size_t max);
int wcscmp(const wchar_t *a, const wchar_t *b);
int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n);
int _wcsicmp(const wchar_t *a, const wchar_t *b);
int _wcsnicmp(const wchar_t *a, const wchar_t *b, size_t n);
wchar_t *wcscpy(wchar_t *dst, const wchar_t *src);
wchar_t *wcscat(wchar_t *dst, const wchar_t *src);
wchar_t *wcschr(const wchar_t *s, wchar_t c);
wchar_t *wcsrchr(const wchar_t *s, wchar_t c);
wchar_t *wcsstr(const wchar_t *hay, const wchar_t *needle);
wchar_t towupper(wchar_t c);
wchar_t towlower(wchar_t c);
int toupper(int c);
int tolower(int c);

int _vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int _snprintf(char *buf, size_t size, const char *fmt, ...);
int _vsnwprintf(wchar_t *buf, size_t count, const wchar_t *fmt, va_list ap);
int _snwprintf(wchar_t *buf, size_t count, const wchar_t *fmt, ...);
int mime_vformat(char *buf, size_t size, const char *fmt, va_list ap);
int mime_format(char *buf, size_t size, const char *fmt, ...);

void NTAPI RtlInitUnicodeString(PUNICODE_STRING dst, PCWSTR src);
void NTAPI RtlInitAnsiString(PANSI_STRING dst, PCSTR src);
void NTAPI RtlInitString(PSTRING dst, PCSTR src);
NTSTATUS NTAPI RtlInitUnicodeStringEx(PUNICODE_STRING dst, PCWSTR src);
LONG NTAPI RtlCompareUnicodeString(PCUNICODE_STRING a, PCUNICODE_STRING b, BOOLEAN case_insensitive);
LONG NTAPI RtlCompareUnicodeStrings(PCWCH a, SIZE_T alen, PCWCH b, SIZE_T blen, BOOLEAN case_insensitive);
BOOLEAN NTAPI RtlEqualUnicodeString(PCUNICODE_STRING a, PCUNICODE_STRING b, BOOLEAN case_insensitive);
BOOLEAN NTAPI RtlPrefixUnicodeString(PCUNICODE_STRING prefix, PCUNICODE_STRING s, BOOLEAN case_insensitive);
void NTAPI RtlCopyUnicodeString(PUNICODE_STRING dst, PCUNICODE_STRING src);
NTSTATUS NTAPI RtlAppendUnicodeToString(PUNICODE_STRING dst, PCWSTR src);
NTSTATUS NTAPI RtlAppendUnicodeStringToString(PUNICODE_STRING dst, PCUNICODE_STRING src);
WCHAR NTAPI RtlUpcaseUnicodeChar(WCHAR c);
WCHAR NTAPI RtlDowncaseUnicodeChar(WCHAR c);
NTSTATUS NTAPI RtlUpcaseUnicodeString(PUNICODE_STRING dst, PCUNICODE_STRING src, BOOLEAN allocate);
NTSTATUS NTAPI RtlUnicodeStringToAnsiString(PANSI_STRING dst, PCUNICODE_STRING src, BOOLEAN allocate);
NTSTATUS NTAPI RtlAnsiStringToUnicodeString(PUNICODE_STRING dst, PCANSI_STRING src, BOOLEAN allocate);
BOOLEAN NTAPI RtlCreateUnicodeString(PUNICODE_STRING s, PCWSTR source);
void NTAPI RtlFreeUnicodeString(PUNICODE_STRING s);
void NTAPI RtlFreeAnsiString(PANSI_STRING s);
NTSTATUS NTAPI RtlIntegerToUnicodeString(ULONG value, ULONG base, PUNICODE_STRING s);
NTSTATUS NTAPI RtlUnicodeStringToInteger(PCUNICODE_STRING s, ULONG base, PULONG value);
NTSTATUS NTAPI RtlMultiByteToUnicodeN(PWCH dst, ULONG dst_bytes, PULONG written, const CHAR *src, ULONG src_bytes);
NTSTATUS NTAPI RtlUnicodeToMultiByteN(PCHAR dst, ULONG dst_bytes, PULONG written, PCWCH src, ULONG src_bytes);
NTSTATUS NTAPI RtlUTF8ToUnicodeN(PWSTR dst, ULONG dst_bytes, PULONG written, PCCH src, ULONG src_bytes);
NTSTATUS NTAPI RtlUnicodeToUTF8N(PCHAR dst, ULONG dst_bytes, PULONG written, PCWCH src, ULONG src_bytes);
ULONG NTAPI RtlUnicodeStringToAnsiSize(PCUNICODE_STRING s);
NTSTATUS NTAPI RtlHashUnicodeString(PCUNICODE_STRING s, BOOLEAN case_insensitive, ULONG algorithm, PULONG hash);
void NTAPI RtlTimeToTimeFields(PLARGE_INTEGER time, PVOID fields);
PIMAGE_NT_HEADERS NTAPI RtlImageNtHeader(PVOID base);
PVOID NTAPI RtlImageDirectoryEntryToData(PVOID base, BOOLEAN mapped, USHORT entry, PULONG size);
PVOID NTAPI RtlImageRvaToVa(PIMAGE_NT_HEADERS nt, PVOID base, ULONG rva, PIMAGE_SECTION_HEADER *last);
PIMAGE_SECTION_HEADER NTAPI RtlImageRvaToSection(PIMAGE_NT_HEADERS nt, PVOID base, ULONG rva);

/* allocation hooks: the kernel uses pool, ntdll the process heap. */
PVOID MimeRtlAllocate(SIZE_T size);
void MimeRtlFree(PVOID p);

#endif
