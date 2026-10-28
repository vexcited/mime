/* temporary stand-ins, replaced file by file. */
#include "avifil32_private.h"

HRESULT AviWaveCreate(IUnknown *outer, REFIID riid, void **object) { *object = nullptr; return E_NOTIMPL; }
HRESULT AviAcmStreamCreate(IUnknown *outer, REFIID riid, void **object) { *object = nullptr; return E_NOTIMPL; }
HRESULT AviEditStreamCreate(IUnknown *outer, REFIID riid, void **object) { *object = nullptr; return E_NOTIMPL; }
HRESULT WINAPI AVIBuildFilterA(LPSTR f, LONG c, BOOL s) { return E_NOTIMPL; }
HRESULT WINAPI AVIBuildFilterW(LPWSTR f, LONG c, BOOL s) { return E_NOTIMPL; }
HRESULT WINAPI AVIClearClipboard(void) { return E_NOTIMPL; }
HRESULT WINAPI AVIGetFromClipboard(PAVIFILE *f) { return E_NOTIMPL; }
HRESULT WINAPI AVIMakeFileFromStreams(PAVIFILE *f, int n, PAVISTREAM *s) { return E_NOTIMPL; }
HRESULT WINAPI AVIMakeStreamFromClipboard(UINT f, HANDLE g, PAVISTREAM *s) { return E_NOTIMPL; }
HRESULT WINAPI AVIPutFileOnClipboard(PAVIFILE f) { return E_NOTIMPL; }
INT_PTR WINAPI AVISaveOptions(HWND w, UINT f, int c, PAVISTREAM *s, LPAVICOMPRESSOPTIONS *o) { return 0; }
HRESULT WINAPI AVISaveOptionsFree(int c, LPAVICOMPRESSOPTIONS *o) { return S_OK; }
HRESULT WINAPI CreateEditableStream(PAVISTREAM *e, PAVISTREAM s) { return E_NOTIMPL; }
HRESULT WINAPI EditStreamClone(PAVISTREAM s, PAVISTREAM *r) { return E_NOTIMPL; }
HRESULT WINAPI EditStreamCopy(PAVISTREAM s, LONG *a, LONG *b, PAVISTREAM *r) { return E_NOTIMPL; }
HRESULT WINAPI EditStreamCut(PAVISTREAM s, LONG *a, LONG *b, PAVISTREAM *r) { return E_NOTIMPL; }
HRESULT WINAPI EditStreamPaste(PAVISTREAM s, LONG *a, LONG *b, PAVISTREAM p, LONG c, LONG d) { return E_NOTIMPL; }
HRESULT WINAPI EditStreamSetInfoA(PAVISTREAM s, AVISTREAMINFOA *i, LONG n) { return E_NOTIMPL; }
HRESULT WINAPI EditStreamSetInfoW(PAVISTREAM s, AVISTREAMINFOW *i, LONG n) { return E_NOTIMPL; }
HRESULT WINAPI EditStreamSetNameA(PAVISTREAM s, LPCSTR n) { return E_NOTIMPL; }
HRESULT WINAPI EditStreamSetNameW(PAVISTREAM s, LPCWSTR n) { return E_NOTIMPL; }
