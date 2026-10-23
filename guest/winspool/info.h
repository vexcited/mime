/* builders for spooler information structures. */
#ifndef MIME_WINSPOOL_INFO_H
#define MIME_WINSPOOL_INFO_H

enum { INFO_PORT, INFO_MONITOR, INFO_PROCESSOR, INFO_DATATYPE, INFO_DRIVER };

PCWSTR InfoProvider(void);
BOOL InfoGetPrinter(int index, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed, BOOL ansi);
BOOL InfoEnumPrinters(const int *indexes, int count, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed,
                      LPDWORD returned, BOOL ansi);
BOOL InfoEnumContainers(LPBYTE buffer, DWORD size, LPDWORD needed, LPDWORD returned, BOOL ansi);
BOOL InfoDriverLevel(DWORD level);
BOOL InfoGetDriver(int index, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed, BOOL ansi);
int InfoCount(DWORD kind);
BOOL InfoEnumNames(DWORD kind, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed, LPDWORD returned, BOOL ansi);
BOOL InfoEnumForms(DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed, LPDWORD returned, BOOL ansi);
BOOL InfoGetForm(const SpoolPaper *paper, DWORD level, LPBYTE buffer, DWORD size, LPDWORD needed, BOOL ansi);

#endif
