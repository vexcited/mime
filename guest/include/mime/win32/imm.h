/* input method manager api, imm32.dll. */
#ifndef MIME_WIN32_IMM_H
#define MIME_WIN32_IMM_H

#include <mime/win32/winuser.h>

#ifdef MIME_BUILD_IMM32
#define IMMAPI DECLSPEC_EXPORT
#else
#define IMMAPI DECLSPEC_IMPORT
#endif

DECLARE_HANDLE(HIMC);
DECLARE_HANDLE(HIMCC);

typedef struct tagCOMPOSITIONFORM {
    DWORD dwStyle;
    POINT ptCurrentPos;
    RECT rcArea;
} COMPOSITIONFORM, *PCOMPOSITIONFORM, *LPCOMPOSITIONFORM;

typedef struct tagCANDIDATEFORM {
    DWORD dwIndex;
    DWORD dwStyle;
    POINT ptCurrentPos;
    RECT rcArea;
} CANDIDATEFORM, *PCANDIDATEFORM, *LPCANDIDATEFORM;

typedef struct tagCANDIDATELIST {
    DWORD dwSize;
    DWORD dwStyle;
    DWORD dwCount;
    DWORD dwSelection;
    DWORD dwPageStart;
    DWORD dwPageSize;
    DWORD dwOffset[1];
} CANDIDATELIST, *PCANDIDATELIST, *LPCANDIDATELIST;

/* composition and candidate window styles. */
#define CFS_DEFAULT 0x0000
#define CFS_RECT 0x0001
#define CFS_POINT 0x0002
#define CFS_FORCE_POSITION 0x0020
#define CFS_CANDIDATEPOS 0x0040
#define CFS_EXCLUDE 0x0080

/* ImmGetCompositionString indexes. */
#define GCS_COMPREADSTR 0x0001
#define GCS_COMPREADATTR 0x0002
#define GCS_COMPREADCLAUSE 0x0004
#define GCS_COMPSTR 0x0008
#define GCS_COMPATTR 0x0010
#define GCS_COMPCLAUSE 0x0020
#define GCS_CURSORPOS 0x0080
#define GCS_DELTASTART 0x0100
#define GCS_RESULTREADSTR 0x0200
#define GCS_RESULTREADCLAUSE 0x0400
#define GCS_RESULTSTR 0x0800
#define GCS_RESULTCLAUSE 0x1000

#define IMM_ERROR_NODATA (-1)
#define IMM_ERROR_GENERAL (-2)

/* composition string character attributes. */
#define ATTR_INPUT 0x00
#define ATTR_TARGET_CONVERTED 0x01
#define ATTR_CONVERTED 0x02
#define ATTR_TARGET_NOTCONVERTED 0x03
#define ATTR_INPUT_ERROR 0x04
#define ATTR_FIXEDCONVERTED 0x05

/* ImmNotifyIME actions and indexes. */
#define NI_OPENCANDIDATE 0x0010
#define NI_CLOSECANDIDATE 0x0011
#define NI_SELECTCANDIDATESTR 0x0012
#define NI_CHANGECANDIDATELIST 0x0013
#define NI_FINALIZECONVERSIONRESULT 0x0014
#define NI_COMPOSITIONSTR 0x0015
#define NI_SETCANDIDATE_PAGESTART 0x0016
#define NI_SETCANDIDATE_PAGESIZE 0x0017
#define NI_IMEMENUSELECTED 0x0018
#define CPS_COMPLETE 0x0001
#define CPS_CONVERT 0x0002
#define CPS_REVERT 0x0003
#define CPS_CANCEL 0x0004

/* ImmAssociateContextEx flags. */
#define IACE_CHILDREN 0x0001
#define IACE_DEFAULT 0x0010
#define IACE_IGNORENOCONTEXT 0x0020

/* ImmGetProperty indexes. */
#define IGP_GETIMEVERSION ((DWORD)-4)
#define IGP_PROPERTY 0x00000004
#define IGP_CONVERSION 0x00000008
#define IGP_SENTENCE 0x0000000c
#define IGP_UI 0x00000010
#define IGP_SETCOMPSTR 0x00000014
#define IGP_SELECT 0x00000018

#ifndef VK_PROCESSKEY
#define VK_PROCESSKEY 0xE5
#endif

IMMAPI HIMC WINAPI ImmCreateContext(void);
IMMAPI BOOL WINAPI ImmDestroyContext(HIMC himc);
IMMAPI HIMC WINAPI ImmGetContext(HWND hwnd);
IMMAPI BOOL WINAPI ImmReleaseContext(HWND hwnd, HIMC himc);
IMMAPI HIMC WINAPI ImmAssociateContext(HWND hwnd, HIMC himc);
IMMAPI BOOL WINAPI ImmAssociateContextEx(HWND hwnd, HIMC himc, DWORD flags);
IMMAPI LONG WINAPI ImmGetCompositionStringW(HIMC himc, DWORD index, LPVOID buffer, DWORD size);
IMMAPI LONG WINAPI ImmGetCompositionStringA(HIMC himc, DWORD index, LPVOID buffer, DWORD size);
IMMAPI BOOL WINAPI ImmSetCompositionStringW(HIMC himc, DWORD index, LPVOID comp, DWORD comp_size, LPVOID read,
                                            DWORD read_size);
IMMAPI BOOL WINAPI ImmGetCompositionWindow(HIMC himc, LPCOMPOSITIONFORM form);
IMMAPI BOOL WINAPI ImmSetCompositionWindow(HIMC himc, LPCOMPOSITIONFORM form);
IMMAPI BOOL WINAPI ImmGetCandidateWindow(HIMC himc, DWORD index, LPCANDIDATEFORM form);
IMMAPI BOOL WINAPI ImmSetCandidateWindow(HIMC himc, LPCANDIDATEFORM form);
IMMAPI BOOL WINAPI ImmGetCompositionFontW(HIMC himc, LPLOGFONTW font);
IMMAPI BOOL WINAPI ImmSetCompositionFontW(HIMC himc, LPLOGFONTW font);
IMMAPI BOOL WINAPI ImmGetOpenStatus(HIMC himc);
IMMAPI BOOL WINAPI ImmSetOpenStatus(HIMC himc, BOOL open);
IMMAPI BOOL WINAPI ImmGetConversionStatus(HIMC himc, LPDWORD conversion, LPDWORD sentence);
IMMAPI BOOL WINAPI ImmSetConversionStatus(HIMC himc, DWORD conversion, DWORD sentence);
IMMAPI BOOL WINAPI ImmNotifyIME(HIMC himc, DWORD action, DWORD index, DWORD value);
IMMAPI UINT WINAPI ImmGetVirtualKey(HWND hwnd);
IMMAPI HWND WINAPI ImmGetDefaultIMEWnd(HWND hwnd);
IMMAPI BOOL WINAPI ImmIsIME(HKL hkl);
IMMAPI DWORD WINAPI ImmGetProperty(HKL hkl, DWORD index);
IMMAPI BOOL WINAPI ImmDisableIME(DWORD thread);
IMMAPI BOOL WINAPI ImmIsUIMessageW(HWND ime, UINT msg, WPARAM wparam, LPARAM lparam);
IMMAPI UINT WINAPI ImmGetDescriptionW(HKL hkl, LPWSTR buffer, UINT size);
IMMAPI UINT WINAPI ImmGetIMEFileNameW(HKL hkl, LPWSTR buffer, UINT size);
IMMAPI LRESULT WINAPI ImmEscapeW(HKL hkl, HIMC himc, UINT escape, LPVOID data);
IMMAPI DWORD WINAPI ImmGetCandidateListW(HIMC himc, DWORD index, LPCANDIDATELIST list, DWORD size);
IMMAPI DWORD WINAPI ImmGetCandidateListCountW(HIMC himc, LPDWORD count);
IMMAPI DWORD WINAPI ImmGetGuideLineW(HIMC himc, DWORD index, LPWSTR buffer, DWORD size);
IMMAPI BOOL WINAPI ImmConfigureIMEW(HKL hkl, HWND hwnd, DWORD mode, LPVOID data);
IMMAPI BOOL WINAPI ImmSimulateHotKey(HWND hwnd, DWORD id);
IMMAPI LRESULT WINAPI ImmRequestMessageW(HIMC himc, WPARAM wparam, LPARAM lparam);
IMMAPI BOOL WINAPI ImmGenerateMessage(HIMC himc);

#endif
